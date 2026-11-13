#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

namespace
{
    struct Voice
    {
        float satScale;    // input scaling into the saturator (higher = dirtier)
        float bias;        // asymmetry -> even harmonics
        float wowMs, wowHz;
        float flutterMs, flutterHz;
        float driftMs;
        float lowpassQ;
        bool  bucketBrigade;
    };

    //                      sat    bias  wowMs wowHz  flutMs flutHz driftMs  Q     bbd
    constexpr Voice voices[] = {
        /* Tape    */ {    0.70f, 0.08f, 1.0f, 0.60f, 0.06f,  6.5f,  0.5f, 0.707f, false },
        /* BBD     */ {    0.55f, 0.12f, 1.2f, 0.80f, 0.00f,  0.0f,  0.0f, 0.600f, true  },
        /* Digital */ {    0.25f, 0.00f, 0.6f, 0.25f, 0.00f,  0.0f,  0.0f, 0.707f, false },
    };

    constexpr float spreadAmount = 0.15f;   // +-15% of the delay time at full spread

    // Rational tanh approximation: smooth, monotonic, a few multiplies + 1 divide.
    inline float softClip (float x) noexcept
    {
        x = juce::jlimit (-3.0f, 3.0f, x);
        const auto x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }

    // Parabolic sine on phase [0, 1): ~0.1% error, no transcendental calls.
    inline float fastSin (float phase) noexcept
    {
        const auto x = 2.0f * phase - 1.0f;
        auto y = 4.0f * (x - x * std::abs (x));
        return 0.225f * (y * std::abs (y) - y) + y;
    }

    inline float hermite (const float* buf, int mask, int writePos, float delay) noexcept
    {
        const auto whole = (int) delay;              // delay is always positive
        const auto frac  = 1.0f - (delay - (float) whole);
        const auto i     = writePos - whole - 1;     // x0 sits 'frac' before x1
        const auto xm1 = buf[(i - 1) & mask];
        const auto x0  = buf[ i      & mask];
        const auto x1  = buf[(i + 1) & mask];
        const auto x2  = buf[(i + 2) & mask];
        const auto c1 = 0.5f * (x1 - xm1);
        const auto c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const auto c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * frac + c2) * frac + c1) * frac + x0;
    }

    juce::String pct (float v)   { return juce::String (juce::roundToInt (v)) + " %"; }
    juce::String hz (float v)    { return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; }
}

void AlbionSlapbackAudioProcessor::SvfCoeffs::set (float cutoff, float q, float sr) noexcept
{
    const auto g = std::tan (juce::MathConstants<float>::pi * juce::jlimit (10.0f, sr * 0.45f, cutoff) / sr);
    k  = 1.0f / q;
    a1 = 1.0f / (1.0f + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
}

//==============================================================================
AlbionSlapbackAudioProcessor::AlbionSlapbackAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "AlbionSlapbackState", createParameterLayout())
{
    timeParam     = apvts.getRawParameterValue (ids::time.getParamID());
    feedbackParam = apvts.getRawParameterValue (ids::feedback.getParamID());
    modeParam     = apvts.getRawParameterValue (ids::mode.getParamID());
    toneParam     = apvts.getRawParameterValue (ids::tone.getParamID());
    lowcutParam   = apvts.getRawParameterValue (ids::lowcut.getParamID());
    driveParam    = apvts.getRawParameterValue (ids::drive.getParamID());
    wowParam      = apvts.getRawParameterValue (ids::wow.getParamID());
    spreadParam   = apvts.getRawParameterValue (ids::spread.getParamID());
    mixParam      = apvts.getRawParameterValue (ids::mix.getParamID());
}

juce::AudioProcessorValueTreeState::ParameterLayout AlbionSlapbackAudioProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto pctAttr = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return pct (v); }).withLabel ("%");
    auto hzAttr  = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return hz (v); }).withLabel ("Hz");

    NormalisableRange<float> timeRange (minTimeMs, maxTimeMs, 0.1f);
    timeRange.setSkewForCentre (110.0f);
    layout.add (std::make_unique<AudioParameterFloat> (ids::time, "Time", timeRange, 120.0f,
        AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return juce::String (v, v < 100.0f ? 1 : 0) + " ms"; })
            .withLabel ("ms")));

    layout.add (std::make_unique<AudioParameterFloat> (ids::feedback, "Feedback",
        NormalisableRange<float> (0.0f, 80.0f, 0.1f), 10.0f, pctAttr));

    layout.add (std::make_unique<AudioParameterChoice> (ids::mode, "Mode", StringArray { "Tape", "BBD", "Digital" }, 0));

    NormalisableRange<float> toneRange (1000.0f, 18000.0f, 1.0f);
    toneRange.setSkewForCentre (4500.0f);
    layout.add (std::make_unique<AudioParameterFloat> (ids::tone, "Tone", toneRange, 6000.0f, hzAttr));

    NormalisableRange<float> lowRange (20.0f, 1000.0f, 1.0f);
    lowRange.setSkewForCentre (150.0f);
    layout.add (std::make_unique<AudioParameterFloat> (ids::lowcut, "Low Cut", lowRange, 120.0f, hzAttr));

    layout.add (std::make_unique<AudioParameterFloat> (ids::drive,  "Drive",       NormalisableRange<float> (0.0f, 100.0f, 0.1f),  30.0f, pctAttr));
    layout.add (std::make_unique<AudioParameterFloat> (ids::wow,    "Wow/Flutter", NormalisableRange<float> (0.0f, 100.0f, 0.1f),  30.0f, pctAttr));
    layout.add (std::make_unique<AudioParameterFloat> (ids::spread, "Spread",      NormalisableRange<float> (0.0f, 100.0f, 0.1f),   0.0f, pctAttr));
    layout.add (std::make_unique<AudioParameterFloat> (ids::mix,    "Mix",         NormalisableRange<float> (0.0f, 100.0f, 0.1f),  30.0f, pctAttr));

    return layout;
}

//==============================================================================
bool AlbionSlapbackAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    // mono -> stereo is allowed so Spread can widen a mono source
    return in == out || (in == juce::AudioChannelSet::mono() && out == juce::AudioChannelSet::stereo());
}

void AlbionSlapbackAudioProcessor::prepareToPlay (double sr, int)
{
    sampleRate = (float) sr;
    glideCoeff = std::exp (-1.0f / (0.08f * sampleRate));
    driftCoeff = std::exp (-juce::MathConstants<float>::twoPi * 1.5f / sampleRate);
    // one-pole low-passed uniform noise has RMS sqrt((1-c)/(1+c)/3); undo that
    driftNorm  = 1.0f / std::sqrt ((1.0f - driftCoeff) / (1.0f + driftCoeff) / 3.0f);

    const auto maxDelay = (maxTimeMs * (1.0f + spreadAmount) + 4.0f) * 0.001f * sampleRate + 8.0f;
    const auto size = juce::nextPowerOfTwo ((int) std::ceil (maxDelay));
    mask = size - 1;
    writePos = 0;

    for (int ch = 0; ch < maxChannels; ++ch)
    {
        auto& c = channels[(size_t) ch];
        c.buffer.assign ((size_t) size, 0.0f);
        c.lowpass = c.lowpass2 = c.highpass = {};
        c.wowPhase     = ch == 0 ? 0.0f : 0.25f;   // decorrelate L/R modulation
        c.flutterPhase = ch == 0 ? 0.0f : 0.40f;
    }

    timeSamples = timeParam->load() * 0.001f * sampleRate;
    drift = 0.0f;

    feedbackGain.reset (feedbackParam->load() * 0.01f);
    const auto m = mixParam->load() * 0.01f;
    dryGain.reset (juce::jmin (1.0f, 2.0f * (1.0f - m)));
    wetGain.reset (juce::jmin (1.0f, 2.0f * m));

    setLatencySamples (0);
}

double AlbionSlapbackAudioProcessor::getTailLengthSeconds() const
{
    const auto t  = (double) timeParam->load() * 0.001 * (1.0 + spreadAmount);
    const auto fb = (double) feedbackParam->load() * 0.01;
    if (fb < 0.01)
        return t + 0.05;
    return juce::jmin (10.0, t * std::log (0.001) / std::log (fb) + 0.05);   // until -60 dB
}

void AlbionSlapbackAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numIn  = getTotalNumInputChannels();
    const auto numOut = juce::jmin (getTotalNumOutputChannels(), maxChannels);
    const int  n      = buffer.getNumSamples();
    if (n == 0 || numOut == 0 || mask == 0)
        return;

    // mono in, stereo out: copy the input across so both delay lines see it
    if (numIn == 1 && numOut == 2)
        buffer.copyFrom (1, 0, buffer, 0, 0, n);
    for (auto ch = juce::jmax (numIn, numOut); ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, n);

    // --- block-rate parameter work -------------------------------------------
    const auto modeIdx = juce::jlimit (0, 2, (int) modeParam->load());
    const auto& v      = voices[modeIdx];
    const auto timeMs  = timeParam->load();
    const auto spread  = spreadParam->load() * 0.01f * spreadAmount;
    const auto wow     = wowParam->load() * 0.01f;
    const auto msToSm  = 0.001f * sampleRate;

    const auto satG    = v.satScale * juce::Decibels::decibelsToGain (driveParam->load() * 0.18f);   // 0..18 dB
    const auto invSatG = 1.0f / satG;
    const auto satBias = softClip (v.bias);

    auto toneHz = toneParam->load();
    if (v.bucketBrigade)   // longer BBD delay -> slower clock -> darker
        toneHz = juce::jmin (toneHz, 7000.0f * std::sqrt (60.0f / timeMs));

    SvfCoeffs lp, hp;
    lp.set (toneHz, v.lowpassQ, sampleRate);
    hp.set (lowcutParam->load(), 0.707f, sampleRate);

    const auto targetTime = timeMs * msToSm;
    const auto wowDepth     = v.wowMs     * wow * msToSm;
    const auto flutterDepth = v.flutterMs * wow * msToSm;
    const auto driftDepth   = v.driftMs   * wow * msToSm;
    const auto wowInc       = v.wowHz     / sampleRate;
    const auto flutterInc   = v.flutterHz / sampleRate;
    const auto maxDelaySm   = (float) (mask - 4);

    feedbackGain.setTarget (feedbackParam->load() * 0.01f, n);
    const auto m = mixParam->load() * 0.01f;
    dryGain.setTarget (juce::jmin (1.0f, 2.0f * (1.0f - m)), n);
    wetGain.setTarget (juce::jmin (1.0f, 2.0f * m), n);

    auto svfTick = [] (Svf& s, const SvfCoeffs& c, float x, float& band) noexcept
    {
        const auto v3 = x - s.ic2;
        const auto v1 = c.a1 * s.ic1 + c.a2 * v3;
        const auto v2 = s.ic2 + c.a2 * s.ic1 + c.a3 * v3;
        s.ic1 = 2.0f * v1 - s.ic1;
        s.ic2 = 2.0f * v2 - s.ic2;
        band = v1;
        return v2;   // low-pass
    };

    float* data[maxChannels] = { buffer.getWritePointer (0), numOut > 1 ? buffer.getWritePointer (1) : nullptr };
    const float spreadSign[maxChannels] = { -1.0f, 1.0f };

    for (int i = 0; i < n; ++i)
    {
        timeSamples = targetTime + glideCoeff * (timeSamples - targetTime);

        float driftNow = 0.0f;
        if (driftDepth > 0.0f)
        {
            rng = rng * 1664525u + 1013904223u;
            const auto noise = (float) (rng >> 8) * (2.0f / 16777216.0f) - 1.0f;
            drift = noise + driftCoeff * (drift - noise);
            driftNow = drift * driftNorm * driftDepth;
        }

        const auto fb  = feedbackGain.next();
        const auto dry = dryGain.next();
        const auto wet = wetGain.next();

        for (int ch = 0; ch < numOut; ++ch)
        {
            auto& c = channels[(size_t) ch];
            auto* io = data[ch];

            c.wowPhase += wowInc;           if (c.wowPhase >= 1.0f)     c.wowPhase -= 1.0f;
            c.flutterPhase += flutterInc;   if (c.flutterPhase >= 1.0f) c.flutterPhase -= 1.0f;

            auto d = timeSamples * (1.0f + spreadSign[ch] * spread)
                   + wowDepth * (1.0f + fastSin (c.wowPhase))       // offsets keep it >= nominal
                   + flutterDepth * fastSin (c.flutterPhase)
                   + driftNow;
            d = juce::jlimit (2.0f, maxDelaySm, d);

            // --- playback head -> filters ---------------------------------------
            auto echo = hermite (c.buffer.data(), mask, writePos, d);
            float band;
            echo = svfTick (c.lowpass, lp, echo, band);
            if (v.bucketBrigade)
                echo = svfTick (c.lowpass2, lp, echo, band);
            const auto hpLow = svfTick (c.highpass, hp, echo, band);
            echo = echo - hp.k * band - hpLow;

            // --- record head: saturate input + feedback -------------------------
            const auto x = io[i];
            const auto rec = (softClip ((x + fb * echo) * satG + v.bias) - satBias) * invSatG;
            c.buffer[(size_t) writePos] = rec;

            io[i] = x * dry + echo * wet;
        }

        writePos = (writePos + 1) & mask;
    }
}

//==============================================================================
int AlbionSlapbackAudioProcessor::getNumPrograms()
{
    return (int) presets::factory.size();
}

const juce::String AlbionSlapbackAudioProcessor::getProgramName (int index)
{
    if (juce::isPositiveAndBelow (index, (int) presets::factory.size()))
        return presets::factory[(size_t) index].name;
    return {};
}

void AlbionSlapbackAudioProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) presets::factory.size()))
        return;

    currentPreset.store (index);
    const auto& p = presets::factory[(size_t) index];

    auto set = [this] (const juce::ParameterID& id, float value)
    {
        if (auto* param = apvts.getParameter (id.getParamID()))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    };

    set (ids::time,     p.timeMs);
    set (ids::feedback, p.feedbackPct);
    set (ids::mode,     (float) p.mode);
    set (ids::tone,     p.toneHz);
    set (ids::lowcut,   p.lowCutHz);
    set (ids::drive,    p.drivePct);
    set (ids::wow,      p.wowPct);
    set (ids::spread,   p.spreadPct);
    set (ids::mix,      p.mixPct);
}

//==============================================================================
juce::AudioProcessorEditor* AlbionSlapbackAudioProcessor::createEditor()
{
    return new AlbionSlapbackAudioProcessorEditor (*this);
}

void AlbionSlapbackAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("preset", currentPreset.load(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void AlbionSlapbackAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            currentPreset.store (juce::jlimit (0, getNumPrograms() - 1, (int) state.getProperty ("preset", 0)));
            apvts.replaceState (state);
        }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AlbionSlapbackAudioProcessor();
}
