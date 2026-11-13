#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace ids
{
    static const juce::ParameterID reference { "reference", 1 };
    static const juce::ParameterID peakLed   { "peakled",   1 };
}

namespace
{
    // Second-order needle model tuned to IEC 60268-17:
    //   zeta = 0.81  -> ~1.3% overshoot
    //   wn   = 13.42 rad/s -> step response first reaches 99% at 300 ms
    constexpr float zeta = 0.81f;
    constexpr float wn   = 13.4231f;

    // A VU meter is average-responding but calibrated to read RMS for a sine:
    // mean(|sin|) = 2/pi, RMS(sin) = 1/sqrt(2), so scale by pi / (2*sqrt(2)).
    constexpr float sineRmsCalibration = juce::MathConstants<float>::pi / (2.0f * juce::MathConstants<float>::sqrt2);
}

//==============================================================================
VuMeterAudioProcessor::VuMeterAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "VuMeterState", createParameterLayout())
{
    referenceParam = apvts.getRawParameterValue (ids::reference.getParamID());
    peakLedParam   = apvts.getRawParameterValue (ids::peakLed.getParamID());
}

juce::AudioProcessorValueTreeState::ParameterLayout VuMeterAudioProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    // The level (dBFS RMS of a sine) that reads 0 VU. -18 dBFS is the common
    // EBU/SMPTE-ish alignment; -14 / -20 / -24 are also common.
    layout.add (std::make_unique<AudioParameterFloat> (
        ids::reference, "0 VU Reference", NormalisableRange<float> (-30.0f, 0.0f, 0.5f), -18.0f,
        AudioParameterFloatAttributes().withLabel ("dBFS")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ids::peakLed, "Peak LED", NormalisableRange<float> (-24.0f, 0.0f, 0.5f), -1.0f,
        AudioParameterFloatAttributes().withLabel ("dBFS")));

    return layout;
}

//==============================================================================
bool VuMeterAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

void VuMeterAudioProcessor::prepareToPlay (double sampleRate, int)
{
    dt = (float) (1.0 / sampleRate);

    for (auto& b : ballistics)  b = {};
    for (auto& v : vuRatio)     v.store (0.0f);
    for (auto& p : peak)        p.store (0.0f);

    numMetered.store (juce::jlimit (1, maxChannels, getTotalNumInputChannels()));
}

void VuMeterAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numIn  = getTotalNumInputChannels();
    const auto numOut = getTotalNumOutputChannels();
    for (auto ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    // Audio passes through untouched; we only observe it.
    const auto numSamples = buffer.getNumSamples();
    const auto channels   = juce::jmin (numIn, maxChannels);
    numMetered.store (juce::jmax (1, channels), std::memory_order_relaxed);

    const auto refGain = juce::Decibels::decibelsToGain (referenceParam->load());
    const auto k1 = wn * wn * dt;
    const auto k2 = 2.0f * zeta * wn * dt;

    for (int ch = 0; ch < channels; ++ch)
    {
        const auto* data = buffer.getReadPointer (ch);
        auto& b = ballistics[(size_t) ch];
        auto pos = b.position;
        auto vel = b.velocity;
        float blockPeak = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto rectified = std::abs (data[i]);
            blockPeak = juce::jmax (blockPeak, rectified);

            // semi-implicit Euler: stable and accurate since wn*dt << 1
            vel += k1 * (rectified * sineRmsCalibration - pos) - k2 * vel;
            pos += vel * dt;
        }

        // the physical needle rests on a pin at the left end
        if (pos < 0.0f) { pos = 0.0f; vel = juce::jmax (vel, 0.0f); }

        b.position = pos;
        b.velocity = vel;

        vuRatio[(size_t) ch].store (pos / refGain, std::memory_order_relaxed);

        // max-reading hold: stored as raw level so changing the 0 VU reference
        // re-labels it correctly instead of invalidating it
        if (pos > maxLevel[(size_t) ch].load (std::memory_order_relaxed))
            maxLevel[(size_t) ch].store (pos, std::memory_order_relaxed);

        // keep the max until the editor consumes it
        auto prev = peak[(size_t) ch].load (std::memory_order_relaxed);
        while (blockPeak > prev
               && ! peak[(size_t) ch].compare_exchange_weak (prev, blockPeak, std::memory_order_relaxed))
        {}
    }
}

//==============================================================================
juce::AudioProcessorEditor* VuMeterAudioProcessor::createEditor()
{
    return new VuMeterAudioProcessorEditor (*this);
}

void VuMeterAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void VuMeterAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VuMeterAudioProcessor();
}
