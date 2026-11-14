#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

namespace
{
    //                         line lengths (ms)                                       input diffusers (ms)    diff
    const rvb::FdnRoom::Shape tightRoomShape {
        {{ 4.3f, 5.9f, 7.1f, 8.9f, 10.3f, 11.9f, 13.7f, 15.1f }}, {{ 1.3f, 2.1f, 3.3f, 4.7f }}, 0.60f,
        {{ 2.1f, 3.4f, 4.9f, 6.3f, 7.8f, 9.6f, 11.2f, 13.5f }},              // early reflections (ms)
        {{ 0.80f, 0.75f, 0.60f, 0.55f, 0.45f, 0.42f, 0.33f, 0.30f }},
        0.45f, 0.80f, 0.25f };                                                 // ER level, late level, mod (ms)

    const rvb::FdnRoom::Shape studioRoomShape {
        {{ 7.9f, 10.1f, 12.3f, 14.6f, 16.9f, 19.4f, 22.1f, 24.9f }}, {{ 1.7f, 2.9f, 4.1f, 5.9f }}, 0.62f,
        {{ 4.3f, 6.1f, 8.7f, 11.3f, 14.1f, 17.2f, 20.6f, 24.8f }},
        {{ 0.70f, 0.65f, 0.55f, 0.50f, 0.40f, 0.38f, 0.30f, 0.28f }},
        0.42f, 0.92f, 0.35f };

    const rvb::FdnRoom::Shape chamberShape {
        {{ 13.1f, 16.7f, 19.9f, 23.3f, 27.1f, 30.7f, 34.9f, 39.1f }}, {{ 2.3f, 3.7f, 5.3f, 7.9f }}, 0.70f,
        {{ 7.1f, 9.3f, 12.9f, 15.7f, 19.8f, 23.4f, 28.7f, 33.1f }},
        {{ 0.50f, 0.48f, 0.42f, 0.40f, 0.33f, 0.30f, 0.25f, 0.23f }},
        0.27f, 1.10f, 0.50f };

    constexpr double fadeSeconds = 0.08;

    juce::String pct (float v)  { return juce::String (juce::roundToInt (v)) + " %"; }
    juce::String hz (float v)   { return v >= 1000.0f ? juce::String (v / 1000.0f, v < 10000.0f ? 2 : 1) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; }
    juce::String secs (float v) { return juce::String (v, 2) + " s"; }
    juce::String ms (float v)   { return juce::String (juce::roundToInt (v)) + " ms"; }
}

void AlbionGuitarReverbAudioProcessor::SvfCoeffs::set (double cutoff, double q, double sr) noexcept
{
    const auto g = std::tan (juce::MathConstants<double>::pi * juce::jlimit (10.0, sr * 0.45, cutoff) / sr);
    const auto kk = 1.0 / q;
    const auto aa1 = 1.0 / (1.0 + g * (g + kk));
    k  = (float) kk;
    a1 = (float) aa1;
    a2 = (float) (g * aa1);
    a3 = (float) (g * g * aa1);
}

//==============================================================================
AlbionGuitarReverbAudioProcessor::AlbionGuitarReverbAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "AlbionGuitarReverbState", createParameterLayout()),
      tightRoom (tightRoomShape), studioRoom (studioRoomShape), chamber (chamberShape)
{
    voiceParam    = apvts.getRawParameterValue (ids::voice.getParamID());
    decayParam    = apvts.getRawParameterValue (ids::decay.getParamID());
    predelayParam = apvts.getRawParameterValue (ids::predelay.getParamID());
    lowcutParam   = apvts.getRawParameterValue (ids::lowcut.getParamID());
    highcutParam  = apvts.getRawParameterValue (ids::highcut.getParamID());
    widthParam    = apvts.getRawParameterValue (ids::width.getParamID());
    duckParam     = apvts.getRawParameterValue (ids::duck.getParamID());
    mixParam      = apvts.getRawParameterValue (ids::mix.getParamID());
}

juce::AudioProcessorValueTreeState::ParameterLayout AlbionGuitarReverbAudioProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto pctAttr = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return pct (v); }).withLabel ("%");
    auto hzAttr  = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return hz (v); }).withLabel ("Hz");

    StringArray voiceNames;
    for (const auto& v : voices::table)
        voiceNames.add (v.name);
    layout.add (std::make_unique<AudioParameterChoice> (ids::voice, "Voice", voiceNames, voices::defaultVoice));

    const auto& dv = voices::table[(size_t) voices::defaultVoice];

    NormalisableRange<float> decayRange (0.2f, 3.0f, 0.01f);
    decayRange.setSkewForCentre (1.0f);
    layout.add (std::make_unique<AudioParameterFloat> (ids::decay, "Decay", decayRange, dv.decay.def,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return secs (v); }).withLabel ("s")));

    layout.add (std::make_unique<AudioParameterFloat> (ids::predelay, "Pre-delay",
        NormalisableRange<float> (0.0f, maxPreDelayMs, 0.1f), dv.preDelay.def,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return ms (v); }).withLabel ("ms")));

    NormalisableRange<float> lowRange (20.0f, 600.0f, 1.0f);
    lowRange.setSkewForCentre (200.0f);
    layout.add (std::make_unique<AudioParameterFloat> (ids::lowcut, "Low Cut", lowRange, dv.lowCut.def, hzAttr));

    NormalisableRange<float> highRange (2000.0f, 12000.0f, 1.0f);
    highRange.setSkewForCentre (5000.0f);
    layout.add (std::make_unique<AudioParameterFloat> (ids::highcut, "High Cut", highRange, dv.highCut.def, hzAttr));

    layout.add (std::make_unique<AudioParameterFloat> (ids::width, "Width", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, pctAttr));
    layout.add (std::make_unique<AudioParameterFloat> (ids::duck,  "Duck",  NormalisableRange<float> (0.0f, 100.0f, 0.1f),   0.0f, pctAttr));
    layout.add (std::make_unique<AudioParameterFloat> (ids::mix,   "Mix",   NormalisableRange<float> (0.0f, 100.0f, 0.1f),  25.0f, pctAttr));

    return layout;
}

//==============================================================================
bool AlbionGuitarReverbAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    // mono -> stereo is allowed: the reverb is stereo even from a mono DI or amp mic
    return in == out || (in == juce::AudioChannelSet::mono() && out == juce::AudioChannelSet::stereo());
}

void AlbionGuitarReverbAudioProcessor::prepareToPlay (double sr, int)
{
    sampleRate = sr;
    for (auto* e : std::initializer_list<rvb::Engine*> { &tightRoom, &studioRoom, &chamber, &plate, &spring })
        e->prepare (sr);

    preDelayLine.prepare ((int) std::ceil (maxPreDelayMs * 0.001 * sr) + 4);
    preDelaySm    = predelayParam->load() * 0.001f * (float) sr;
    preDelayCoeff = (float) std::exp (-1.0 / (0.05 * sr));

    hp1 = hp2 = lp = {};
    smoothLow  = lowcutParam->load();
    smoothHigh = highcutParam->load();
    freqSmooth = (float) std::exp (-(double) chunk / (0.03 * sr));

    duckEnv     = 0.0f;
    duckAttack  = (float) std::exp (-1.0 / (0.005 * sr));
    duckRelease = (float) std::exp (-1.0 / (0.30 * sr));

    for (auto* v : { &monoIn, &wetL, &wetR, &fadeL, &fadeR })
        v->assign ((size_t) chunk, 0.0f);

    activeVoice = juce::jlimit (0, voices::count - 1, (int) voiceParam->load());
    active = &engineFor (activeVoice);
    active->setVoice (voices::table[(size_t) activeVoice], decayParam->load());
    fading = nullptr;
    fadeLen = juce::jmax (1, (int) (fadeSeconds * sr));
    fadePos = 0;

    const auto m = mixParam->load() * 0.01f;
    dryGain.reset (juce::jmin (1.0f, 2.0f * (1.0f - m)));
    wetGain.reset (juce::jmin (1.0f, 2.0f * m));
    widthGain.reset (widthParam->load() * 0.01f);

    setLatencySamples (0);
}

rvb::Engine& AlbionGuitarReverbAudioProcessor::engineFor (int voiceIndex)
{
    switch (voices::table[(size_t) juce::jlimit (0, voices::count - 1, voiceIndex)].engine)
    {
        case EngineKind::TightRoom:  return tightRoom;
        case EngineKind::StudioRoom: return studioRoom;
        case EngineKind::Chamber:    return chamber;
        case EngineKind::Plate:      return plate;
        case EngineKind::Spring:     return spring;
    }
    return studioRoom;
}

double AlbionGuitarReverbAudioProcessor::getTailLengthSeconds() const
{
    return (double) decayParam->load() * 1.3 + (double) predelayParam->load() * 0.001 + 0.1;
}

void AlbionGuitarReverbAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numIn  = getTotalNumInputChannels();
    const auto numOut = juce::jmin (getTotalNumOutputChannels(), maxChannels);
    const int  n      = buffer.getNumSamples();
    if (n == 0 || numOut == 0 || active == nullptr)
        return;

    if (numIn == 1 && numOut == 2)
        buffer.copyFrom (1, 0, buffer, 0, 0, n);
    for (auto ch = juce::jmax (numIn, numOut); ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, n);

    auto* left  = buffer.getWritePointer (0);
    auto* right = numOut > 1 ? buffer.getWritePointer (1) : nullptr;
    for (int start = 0; start < n; start += chunk)
    {
        const auto len = juce::jmin (chunk, n - start);
        processChunk (left + start, right != nullptr ? right + start : nullptr, numOut, len);
    }
}

void AlbionGuitarReverbAudioProcessor::processChunk (float* left, float* right, int numOut, int n)
{
    // --- voice / engine --------------------------------------------------------
    const auto voiceIdx = juce::jlimit (0, voices::count - 1, (int) voiceParam->load());
    const auto& voice   = voices::table[(size_t) voiceIdx];
    if (voiceIdx != activeVoice)
    {
        auto& next = engineFor (voiceIdx);
        if (&next != active)
        {
            fading = active;
            fadePos = 0;
            next.reset();
            active = &next;
        }
        activeVoice = voiceIdx;
    }
    active->setVoice (voice, decayParam->load());

    // --- input: mono sum -> filters -> pre-delay ---------------------------------
    smoothLow  = lowcutParam->load()  + freqSmooth * (smoothLow  - lowcutParam->load());
    smoothHigh = highcutParam->load() + freqSmooth * (smoothHigh - highcutParam->load());
    SvfCoeffs h1, h2, l1;
    h1.set (smoothLow, 0.54119610, sampleRate);    // 4th-order Butterworth high-pass (24 dB/oct)
    h2.set (smoothLow, 1.30656296, sampleRate);
    l1.set (smoothHigh, 0.70710678, sampleRate);   // 2nd-order Butterworth low-pass (12 dB/oct)

    auto svf = [] (Svf& s, const SvfCoeffs& c, float x, float& band) noexcept
    {
        const auto v3 = x - s.ic2;
        const auto v1 = c.a1 * s.ic1 + c.a2 * v3;
        const auto v2 = s.ic2 + c.a2 * s.ic1 + c.a3 * v3;
        s.ic1 = 2.0f * v1 - s.ic1;
        s.ic2 = 2.0f * v2 - s.ic2;
        band = v1;
        return v2;
    };

    const auto targetPre = predelayParam->load() * 0.001f * (float) sampleRate;
    float* mono = monoIn.data();
    for (int i = 0; i < n; ++i)
    {
        auto x = right != nullptr ? 0.5f * (left[i] + right[i]) : left[i];
        float band;
        auto low = svf (hp1, h1, x, band);  x = x - h1.k * band - low;
        low      = svf (hp2, h2, x, band);  x = x - h2.k * band - low;
        x        = svf (lp,  l1, x, band);

        preDelayLine.write (x);
        preDelaySm = targetPre + preDelayCoeff * (preDelaySm - targetPre);
        mono[i] = preDelayLine.readFrac (preDelaySm + 1.0f);
    }

    // --- reverb ---------------------------------------------------------------
    active->process (mono, wetL.data(), wetR.data(), n);
    if (fading != nullptr)
    {
        fading->process (mono, fadeL.data(), fadeR.data(), n);
        for (int i = 0; i < n; ++i)
        {
            const auto t = juce::jmin (1.0f, (float) (fadePos + i) / (float) fadeLen);
            wetL[(size_t) i] = wetL[(size_t) i] * t + fadeL[(size_t) i] * (1.0f - t);
            wetR[(size_t) i] = wetR[(size_t) i] * t + fadeR[(size_t) i] * (1.0f - t);
        }
        fadePos += n;
        if (fadePos >= fadeLen)
            fading = nullptr;
    }

    // --- width, ducking, mix ----------------------------------------------------
    const auto m = mixParam->load() * 0.01f;
    dryGain.setTarget (juce::jmin (1.0f, 2.0f * (1.0f - m)), n);
    wetGain.setTarget (juce::jmin (1.0f, 2.0f * m), n);
    widthGain.setTarget (widthParam->load() * 0.01f, n);
    const auto duck = duckParam->load() * 0.01f;

    for (int i = 0; i < n; ++i)
    {
        const auto dl = left[i];
        const auto dr = right != nullptr ? right[i] : dl;

        const auto level = juce::jmax (std::abs (dl), std::abs (dr));
        duckEnv = level + (level > duckEnv ? duckAttack : duckRelease) * (duckEnv - level);
        // full Duck: about -16 dB on the reverb while the guitar plays at -10 dBFS or louder
        const auto duckGain = 1.0f - 0.84f * duck * juce::jmin (1.0f, duckEnv * 3.2f);

        const auto w   = widthGain.next();
        const auto mid  = 0.5f * (wetL[(size_t) i] + wetR[(size_t) i]);
        const auto side = 0.5f * (wetL[(size_t) i] - wetR[(size_t) i]) * w;
        const auto wet = wetGain.next() * duckGain;
        const auto dry = dryGain.next();

        if (numOut > 1)
        {
            left[i]  = dl * dry + (mid + side) * wet;
            right[i] = dr * dry + (mid - side) * wet;
        }
        else
        {
            left[i] = dl * dry + mid * wet;
        }
    }
}

//==============================================================================
int AlbionGuitarReverbAudioProcessor::currentVoice() const
{
    return juce::jlimit (0, voices::count - 1, (int) voiceParam->load());
}

void AlbionGuitarReverbAudioProcessor::selectVoice (int voiceIndex)
{
    voiceIndex = juce::jlimit (0, voices::count - 1, voiceIndex);
    const auto& v = voices::table[(size_t) voiceIndex];

    auto set = [this] (const juce::ParameterID& id, float value)
    {
        if (auto* param = apvts.getParameter (id.getParamID()))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 (value));
            param->endChangeGesture();
        }
    };

    set (ids::voice,    (float) voiceIndex);
    set (ids::decay,    v.decay.def);
    set (ids::predelay, v.preDelay.def);
    set (ids::lowcut,   v.lowCut.def);
    set (ids::highcut,  v.highCut.def);
}

int AlbionGuitarReverbAudioProcessor::getNumPrograms()
{
    return (int) presets::factory.size();
}

const juce::String AlbionGuitarReverbAudioProcessor::getProgramName (int index)
{
    if (juce::isPositiveAndBelow (index, (int) presets::factory.size()))
        return presets::factory[(size_t) index].name;
    return {};
}

void AlbionGuitarReverbAudioProcessor::setCurrentProgram (int index)
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

    set (ids::voice,    (float) p.voice);
    set (ids::decay,    p.decayS);
    set (ids::predelay, p.preDelayMs);
    set (ids::lowcut,   p.lowCutHz);
    set (ids::highcut,  p.highCutHz);
    set (ids::width,    p.widthPct);
    set (ids::duck,     p.duckPct);
    set (ids::mix,      p.mixPct);
}

//==============================================================================
juce::AudioProcessorEditor* AlbionGuitarReverbAudioProcessor::createEditor()
{
    return new AlbionGuitarReverbAudioProcessorEditor (*this);
}

void AlbionGuitarReverbAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("preset", currentPreset.load(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void AlbionGuitarReverbAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
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
    return new AlbionGuitarReverbAudioProcessor();
}
