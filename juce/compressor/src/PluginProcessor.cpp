#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

namespace
{
    constexpr float log2ToDb   = 6.0205999f;     // 20 * log10(2)
    constexpr float dbToLog2   = 0.16609640f;    // log2(10) / 20

    // --- FET / output-stage colour ---------------------------------------
    // y = u + c2*u^2 - c3*u^3 on a clamped input: 2nd + 3rd harmonics only,
    // so aliasing is bounded (no oversampling needed) and it costs a handful
    // of multiplies. Monotonic on [-1, 1] for these coefficients.
    constexpr float colourC2   = 0.12f;
    constexpr float colourC3   = 0.25f;
    constexpr float colourBase = 0.30f;   // drive at 0 dB GR
    constexpr float colourPerDbGr = 0.04f;   // more colour the harder it works
    constexpr float allModeColour = 1.8f;

    // "All" ratio: over-compresses (slope > 1), so loud peaks come out quieter
    // than the threshold. That's the classic all-buttons-in behaviour.
    constexpr float allModeSlope = 1.08f;
    constexpr float ratioValues[] = { 4.0f, 8.0f, 12.0f, 20.0f };

    juce::String msToText (float ms)
    {
        if (ms < 1.0f)
            return juce::String (juce::roundToInt (ms * 1000.0f)) + " us";
        return juce::String (ms, ms < 10.0f ? 2 : (ms < 100.0f ? 1 : 0)) + " ms";
    }

    juce::String dbToText (float db)
    {
        return (db > 0.0f ? "+" : "") + juce::String (db, 1) + " dB";
    }
}

//==============================================================================
Albion76AudioProcessor::Albion76AudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Albion76State", createParameterLayout())
{
    gainParam      = apvts.getRawParameterValue (ids::gain.getParamID());
    thresholdParam = apvts.getRawParameterValue (ids::threshold.getParamID());
    ratioParam     = apvts.getRawParameterValue (ids::ratio.getParamID());
    kneeParam      = apvts.getRawParameterValue (ids::knee.getParamID());
    attackParam    = apvts.getRawParameterValue (ids::attack.getParamID());
    releaseParam   = apvts.getRawParameterValue (ids::release.getParamID());
    makeupParam    = apvts.getRawParameterValue (ids::makeup.getParamID());
    mixParam       = apvts.getRawParameterValue (ids::mix.getParamID());
}

juce::AudioProcessorValueTreeState::ParameterLayout Albion76AudioProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto dbAttr = AudioParameterFloatAttributes()
                      .withStringFromValueFunction ([] (float v, int) { return dbToText (v); })
                      .withLabel ("dB");
    auto msAttr = AudioParameterFloatAttributes()
                      .withStringFromValueFunction ([] (float v, int) { return msToText (v); })
                      .withLabel ("ms");

    layout.add (std::make_unique<AudioParameterFloat> (
        ids::gain, "Gain", NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, dbAttr));

    layout.add (std::make_unique<AudioParameterFloat> (
        ids::threshold, "Threshold", NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -20.0f, dbAttr));

    layout.add (std::make_unique<AudioParameterChoice> (
        ids::ratio, "Ratio", StringArray { "4:1", "8:1", "12:1", "20:1", "All" }, 0));

    layout.add (std::make_unique<AudioParameterFloat> (
        ids::knee, "Knee", NormalisableRange<float> (0.0f, 24.0f, 0.1f), 6.0f, dbAttr));

    NormalisableRange<float> attackRange (0.02f, 50.0f, 0.01f);
    attackRange.setSkewForCentre (2.0f);
    layout.add (std::make_unique<AudioParameterFloat> (ids::attack, "Attack", attackRange, 0.5f, msAttr));

    NormalisableRange<float> releaseRange (30.0f, 2000.0f, 1.0f);
    releaseRange.setSkewForCentre (250.0f);
    layout.add (std::make_unique<AudioParameterFloat> (ids::release, "Release", releaseRange, 200.0f, msAttr));

    layout.add (std::make_unique<AudioParameterFloat> (
        ids::makeup, "Makeup", NormalisableRange<float> (-12.0f, 24.0f, 0.1f), 0.0f, dbAttr));

    layout.add (std::make_unique<AudioParameterFloat> (
        ids::mix, "Mix", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
        AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; })
            .withLabel ("%")));

    return layout;
}

//==============================================================================
bool Albion76AudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

void Albion76AudioProcessor::prepareToPlay (double sr, int)
{
    sampleRate = (float) sr;
    slowCoeff  = std::exp (-1.0f / (0.6f * sampleRate));
    dcCoeff    = 1.0f - juce::MathConstants<float>::twoPi * 5.0f / sampleRate;   // ~5 Hz

    envDb = slowDb = 0.0f;
    for (auto& d : dc) d = {};

    inGain.reset     (juce::Decibels::decibelsToGain (gainParam->load()));
    makeupGain.reset (juce::Decibels::decibelsToGain (makeupParam->load()));
    mixAmount.reset  (mixParam->load() * 0.01f);
    grMeterDb.store (0.0f);

    setLatencySamples (0);
}

void Albion76AudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numIn  = getTotalNumInputChannels();
    const auto numOut = getTotalNumOutputChannels();
    for (auto ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    const int n = buffer.getNumSamples();
    const int channels = juce::jmin (numIn, maxChannels);
    if (n == 0 || channels == 0)
        return;

    // --- block-rate parameter work (all the exp/pow calls live here) ---------
    const auto thrDb    = thresholdParam->load();
    const auto kneeDb   = kneeParam->load();
    const auto ratioIdx = juce::jlimit (0, 4, (int) ratioParam->load());
    const bool allMode  = ratioIdx == 4;
    const auto slope    = allMode ? allModeSlope : 1.0f - 1.0f / ratioValues[ratioIdx];
    const auto halfKnee = kneeDb * 0.5f;
    const auto invKnee2 = kneeDb > 0.0f ? 1.0f / (2.0f * kneeDb) : 0.0f;
    // below this linear level the gain computer is exactly 0 dB, so skip the log
    const auto kneeStartLin = juce::Decibels::decibelsToGain (thrDb - halfKnee, -200.0f);

    const auto attackS  = attackParam->load() * 0.001f;
    // program-dependent release: sustained heavy GR stretches it up to ~2.5x
    const auto releaseS = releaseParam->load() * 0.001f * (1.0f + 1.5f * slowDb / (slowDb + 4.0f));
    const auto aCoeff   = std::exp (-1.0f / (attackS  * sampleRate));
    const auto rCoeff   = std::exp (-1.0f / (releaseS * sampleRate));

    const auto colourDrive = colourBase * (allMode ? allModeColour : 1.0f);

    inGain.setTarget     (juce::Decibels::decibelsToGain (gainParam->load()),   n);
    makeupGain.setTarget (juce::Decibels::decibelsToGain (makeupParam->load()), n);
    mixAmount.setTarget  (mixParam->load() * 0.01f, n);

    float* l = buffer.getWritePointer (0);
    float* r = channels > 1 ? buffer.getWritePointer (1) : nullptr;

    auto env  = envDb;
    auto slow = slowDb;
    float blockMaxGr = 0.0f;
    auto dcL = dc[0], dcR = dc[1];

    auto colour = [] (float x, float k, float invK, DcBlocker& d, float dcC) noexcept
    {
        const auto u = juce::jlimit (-1.0f, 1.0f, x * k);
        const auto y = (u + u * u * (colourC2 - colourC3 * u)) * invK;
        const auto out = y - d.x1 + dcC * d.y1;
        d.x1 = y;
        d.y1 = out;
        return out;
    };

    for (int i = 0; i < n; ++i)
    {
        const auto gIn  = inGain.next();
        const auto gOut = makeupGain.next();
        const auto mix  = mixAmount.next();

        const auto dryL = l[i];
        const auto dryR = r != nullptr ? r[i] : 0.0f;
        const auto xL = dryL * gIn;
        const auto xR = dryR * gIn;

        // --- detector + gain computer (stereo-linked peak) --------------------
        const auto peak = juce::jmax (std::abs (xL), std::abs (xR));
        float target = 0.0f;
        if (peak > kneeStartLin)
        {
            const auto over = std::log2 (peak) * log2ToDb - thrDb;
            target = (over > halfKnee) ? slope * over
                                       : slope * (over + halfKnee) * (over + halfKnee) * invKnee2;
        }

        // --- branching attack/release ballistics in the dB domain -------------
        env  = target + (target > env ? aCoeff : rCoeff) * (env - target);
        slow = env + slowCoeff * (slow - env);
        blockMaxGr = juce::jmax (blockMaxGr, env);

        const auto grGain = env > 1.0e-4f ? std::exp2 (-env * dbToLog2) : 1.0f;

        // --- colour: drive rises with gain reduction, like a working FET ------
        const auto k    = juce::jmin (0.9f, colourDrive * (1.0f + colourPerDbGr * env));
        const auto invK = 1.0f / k;

        const auto wetL = colour (xL * grGain, k, invK, dcL, dcCoeff) * gOut;
        l[i] = dryL + mix * (wetL - dryL);

        if (r != nullptr)
        {
            const auto wetR = colour (xR * grGain, k, invK, dcR, dcCoeff) * gOut;
            r[i] = dryR + mix * (wetR - dryR);
        }
    }

    envDb  = env;
    slowDb = slow;
    dc[0] = dcL;
    dc[1] = dcR;

    auto prev = grMeterDb.load (std::memory_order_relaxed);
    while (blockMaxGr > prev && ! grMeterDb.compare_exchange_weak (prev, blockMaxGr, std::memory_order_relaxed))
    {}
}

//==============================================================================
int Albion76AudioProcessor::getNumPrograms()
{
    return (int) presets::factory.size();
}

const juce::String Albion76AudioProcessor::getProgramName (int index)
{
    if (juce::isPositiveAndBelow (index, (int) presets::factory.size()))
        return presets::factory[(size_t) index].name;
    return {};
}

void Albion76AudioProcessor::setCurrentProgram (int index)
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

    set (ids::gain,      p.gainDb);
    set (ids::threshold, p.thresholdDb);
    set (ids::ratio,     (float) p.ratioIndex);
    set (ids::knee,      p.kneeDb);
    set (ids::attack,    p.attackMs);
    set (ids::release,   p.releaseMs);
    set (ids::makeup,    p.makeupDb);
    set (ids::mix,       p.mixPct);
}

//==============================================================================
juce::AudioProcessorEditor* Albion76AudioProcessor::createEditor()
{
    return new Albion76AudioProcessorEditor (*this);
}

void Albion76AudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("preset", currentPreset.load(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void Albion76AudioProcessor::setStateInformation (const void* data, int sizeInBytes)
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
    return new Albion76AudioProcessor();
}
