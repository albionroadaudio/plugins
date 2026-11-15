#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

namespace
{
    constexpr int chunk = 32;   // control-rate block for EQ coefficient updates

    juce::String pct (float v) { return juce::String (juce::roundToInt (v)) + " %"; }
    juce::String db (float v)
    {
        const auto r = std::round (v * 10.0f) / 10.0f;
        return (r > 0.0f ? "+" : "") + juce::String (r, 1) + " dB";
    }
}

VocalMultitoolProcessor::VocalMultitoolProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "AlbionVocalMultitoolState", createParameterLayout())
{
    denoiseParam = apvts.getRawParameterValue (ids::denoise.getParamID());
    hpfParam     = apvts.getRawParameterValue (ids::hpf.getParamID());
    comp1Param   = apvts.getRawParameterValue (ids::comp1.getParamID());
    deessParam   = apvts.getRawParameterValue (ids::deess.getParamID());
    mudParam     = apvts.getRawParameterValue (ids::mud.getParamID());
    nasalParam   = apvts.getRawParameterValue (ids::nasal.getParamID());
    airParam     = apvts.getRawParameterValue (ids::air.getParamID());
    comp2Param   = apvts.getRawParameterValue (ids::comp2.getParamID());
    outputParam  = apvts.getRawParameterValue (ids::output.getParamID());
    denoiseOnParam = apvts.getRawParameterValue (ids::denoiseOn.getParamID());
    comp1OnParam   = apvts.getRawParameterValue (ids::comp1On.getParamID());
    deessOnParam   = apvts.getRawParameterValue (ids::deessOn.getParamID());
    eqOnParam      = apvts.getRawParameterValue (ids::eqOn.getParamID());
    comp2OnParam   = apvts.getRawParameterValue (ids::comp2On.getParamID());
}

juce::AudioProcessorValueTreeState::ParameterLayout VocalMultitoolProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto pctAttr = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return pct (v); }).withLabel ("%");
    auto dbAttr  = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return db (v); }).withLabel ("dB");
    const auto& d = presets::factory[0];

    layout.add (std::make_unique<AudioParameterFloat> (ids::denoise, "Denoise",    NormalisableRange<float> (0.0f, 100.0f, 0.1f), d.denoise, pctAttr));
    layout.add (std::make_unique<AudioParameterBool>  (ids::hpf,     "High-Pass",  d.hpf));
    layout.add (std::make_unique<AudioParameterFloat> (ids::comp1,   "Compressor", NormalisableRange<float> (0.0f, 100.0f, 0.1f), d.comp1, pctAttr));
    layout.add (std::make_unique<AudioParameterFloat> (ids::deess,   "De-esser",   NormalisableRange<float> (0.0f, 100.0f, 0.1f), d.deess, pctAttr));
    layout.add (std::make_unique<AudioParameterFloat> (ids::mud,     "Mud 200 Hz",   NormalisableRange<float> (-12.0f, 0.0f, 0.1f), d.mud,   dbAttr));
    layout.add (std::make_unique<AudioParameterFloat> (ids::nasal,   "Nasal 2.6 kHz", NormalisableRange<float> (-12.0f, 0.0f, 0.1f), d.nasal, dbAttr));
    layout.add (std::make_unique<AudioParameterFloat> (ids::air,     "Air 11 kHz",   NormalisableRange<float> (0.0f, 9.0f, 0.1f),    d.air,   dbAttr));
    layout.add (std::make_unique<AudioParameterFloat> (ids::comp2,   "Glue",       NormalisableRange<float> (0.0f, 100.0f, 0.1f), d.comp2, pctAttr));
    layout.add (std::make_unique<AudioParameterFloat> (ids::output,  "Output",     NormalisableRange<float> (-12.0f, 12.0f, 0.1f), d.output, dbAttr));

    layout.add (std::make_unique<AudioParameterBool> (ids::denoiseOn, "Denoise On",    d.sectionsOn));
    layout.add (std::make_unique<AudioParameterBool> (ids::comp1On,   "Compressor On", d.sectionsOn));
    layout.add (std::make_unique<AudioParameterBool> (ids::deessOn,   "De-esser On",   d.sectionsOn));
    layout.add (std::make_unique<AudioParameterBool> (ids::eqOn,      "EQ On",         d.sectionsOn));
    layout.add (std::make_unique<AudioParameterBool> (ids::comp2On,   "Glue On",       d.sectionsOn));
    return layout;
}

bool VocalMultitoolProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    return in == out && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
}

void VocalMultitoolProcessor::prepareToPlay (double sr, int)
{
    sampleRate = sr;

    denoiser.prepare (sr, vmt::maxChannels);
    setLatencySamples (denoiser.latency());

    // 24 dB/oct Butterworth high-pass = two biquads
    hp1.setHighpass (chain::hpfHz, 0.54119610, sr);
    hp2.setHighpass (chain::hpfHz, 1.30656296, sr);
    hp1.reset(); hp2.reset();

    clamp.prepare (sr);
    glue.prepare (sr);
    deEsser.prepare (sr);

    hpfCoeff = vmt::timeCoeff (0.015, sr);
    ctlCoeff = vmt::timeCoeff (0.020, sr);
    eqCoeffChunk = vmt::timeCoeff (0.020, sr / chunk);

    hpfMix   = hpfParam->load() > 0.5f ? 1.0 : 0.0;
    clampAmt = on (comp1OnParam) * comp1Param->load() * 0.01;
    deessAmt = on (deessOnParam) * deessParam->load() * 0.01;
    glueAmt  = on (comp2OnParam) * comp2Param->load() * 0.01;
    outGain  = vmt::dbToGain (outputParam->load());
    updateEq (true);
    for (auto* f : { &mudEq, &nasalEq, &airEq })
        f->reset();

    for (auto& m : meters)
        m.store (0.0f);
}

void VocalMultitoolProcessor::updateEq (bool force) noexcept
{
    const auto eq = on (eqOnParam);
    const double tMud = eq * mudParam->load(), tNasal = eq * nasalParam->load(), tAir = eq * airParam->load();
    if (force)
    {
        mudDb = tMud; nasalDb = tNasal; airDb = tAir;
    }
    else
    {
        const auto near = [] (double a, double b) { return std::abs (a - b) < 1.0e-3; };
        if (mudDb == tMud && nasalDb == tNasal && airDb == tAir)
            return;
        auto glide = [&] (double& v, double t) { v = near (v, t) ? t : v + eqCoeffChunk * (t - v); };   // lands exactly
        glide (mudDb, tMud);
        glide (nasalDb, tNasal);
        glide (airDb, tAir);
    }
    mudEq.setPeak (chain::mudHz, chain::mudQ, mudDb, sampleRate);
    nasalEq.setPeak (chain::nasalHz, chain::nasalQ, nasalDb, sampleRate);
    airEq.setHighShelf (chain::airHz, chain::airQ, airDb, sampleRate);
}

void VocalMultitoolProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const auto numCh = juce::jmin (buffer.getNumChannels(), getTotalNumInputChannels(), vmt::maxChannels);
    for (auto ch = numCh; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());
    if (numCh == 0)
        return;

    const auto n = buffer.getNumSamples();

    // 1. denoiser (STFT, works on the whole block)
    denoiser.process (buffer, numCh, (float) on (denoiseOnParam) * denoiseParam->load() * 0.01f);
    raise (meters[mDenoise], denoiser.lastReductionDb());

    const double tHpf   = hpfParam->load() > 0.5f ? 1.0 : 0.0;
    const double tClamp = on (comp1OnParam) * comp1Param->load() * 0.01;
    const double tDeess = on (deessOnParam) * deessParam->load() * 0.01;
    const double tGlue  = on (comp2OnParam) * comp2Param->load() * 0.01;
    const double tOut   = vmt::dbToGain (outputParam->load());

    float peakClamp = 0.0f, peakDeess = 0.0f, peakGlue = 0.0f;
    std::array<float*, vmt::maxChannels> data {};
    for (int ch = 0; ch < numCh; ++ch)
        data[(size_t) ch] = buffer.getWritePointer (ch);

    for (int start = 0; start < n; start += chunk)
    {
        updateEq (false);
        const auto end = juce::jmin (n, start + chunk);

        for (int i = start; i < end; ++i)
        {
            hpfMix   += hpfCoeff * (tHpf - hpfMix);
            clampAmt += ctlCoeff * (tClamp - clampAmt);
            deessAmt += ctlCoeff * (tDeess - deessAmt);
            glueAmt  += ctlCoeff * (tGlue - glueAmt);
            outGain  += ctlCoeff * (tOut - outGain);

            std::array<float, vmt::maxChannels> x {};
            for (int ch = 0; ch < numCh; ++ch)
                x[(size_t) ch] = data[(size_t) ch][i];

            // 2. high-pass 120 Hz (always running, crossfaded on/off)
            float peak = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
            {
                auto& v = x[(size_t) ch];
                const auto f = hp2.process (hp1.process (v, ch), ch);
                v += (float) hpfMix * (f - v);
                peak = juce::jmax (peak, std::abs (v));
            }

            // 3. clamp compressor
            const auto g1 = clamp.process (peak, (float) clampAmt);
            for (int ch = 0; ch < numCh; ++ch)
                x[(size_t) ch] *= g1;
            peakClamp = juce::jmax (peakClamp, clamp.grDb());

            // 4. de-esser
            deEsser.process (x.data(), numCh, (float) deessAmt);
            peakDeess = juce::jmax (peakDeess, deEsser.grDb());

            // 5. EQ
            peak = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
            {
                auto& v = x[(size_t) ch];
                v = airEq.process (nasalEq.process (mudEq.process (v, ch), ch), ch);
                peak = juce::jmax (peak, std::abs (v));
            }

            // 6. glue compressor, then output trim
            const auto g2 = glue.process (peak, (float) glueAmt) * (float) outGain;
            peakGlue = juce::jmax (peakGlue, glue.grDb());

            for (int ch = 0; ch < numCh; ++ch)
                data[(size_t) ch][i] = x[(size_t) ch] * g2;
        }
    }

    raise (meters[mClamp], peakClamp);
    raise (meters[mDeEss], peakDeess);
    raise (meters[mGlue],  peakGlue);
}

//==============================================================================
int VocalMultitoolProcessor::getNumPrograms()
{
    return (int) presets::factory.size();
}

const juce::String VocalMultitoolProcessor::getProgramName (int index)
{
    if (juce::isPositiveAndBelow (index, (int) presets::factory.size()))
        return presets::factory[(size_t) index].name;
    return {};
}

void VocalMultitoolProcessor::setCurrentProgram (int index)
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

    set (ids::denoise, p.denoise);
    set (ids::hpf,     p.hpf ? 1.0f : 0.0f);
    set (ids::comp1,   p.comp1);
    set (ids::deess,   p.deess);
    set (ids::mud,     p.mud);
    set (ids::nasal,   p.nasal);
    set (ids::air,     p.air);
    set (ids::comp2,   p.comp2);
    set (ids::output,  p.output);
    for (const auto* id : { &ids::denoiseOn, &ids::comp1On, &ids::deessOn, &ids::eqOn, &ids::comp2On })
        set (*id, p.sectionsOn ? 1.0f : 0.0f);
}

//==============================================================================
juce::AudioProcessorEditor* VocalMultitoolProcessor::createEditor()
{
    return new VocalMultitoolEditor (*this);
}

void VocalMultitoolProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("preset", currentPreset.load(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void VocalMultitoolProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            currentPreset.store (juce::jlimit (0, getNumPrograms() - 1, (int) state.getProperty ("preset", 0)));
            apvts.replaceState (state);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VocalMultitoolProcessor();
}
