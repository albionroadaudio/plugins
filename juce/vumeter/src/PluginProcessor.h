#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>

// VU Meter: transparent pass-through that measures each channel with
// IEC 60268-17 VU ballistics (99% of steady state in 300 ms, ~1.3% overshoot).
// The ballistics run per-sample on the audio thread; the editor just samples
// the resulting needle position.
class VuMeterAudioProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int maxChannels = 2;

    VuMeterAudioProcessor();
    ~VuMeterAudioProcessor() override = default;

    // --- audio ------------------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    // --- editor -----------------------------------------------------------
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // --- info -------------------------------------------------------------
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // --- programs (unused) ------------------------------------------------
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    // --- state ------------------------------------------------------------
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts;

    // --- metering, read by the editor (any thread) --------------------------
    // Needle position as a linear ratio of the 0 VU reference (1.0 == 0 VU).
    float getVuRatio (int channel) const noexcept   { return vuRatio[(size_t) channel].load (std::memory_order_relaxed); }
    // Highest absolute sample since the last call (consumes it).
    float takePeak (int channel) noexcept           { return peak[(size_t) channel].exchange (0.0f, std::memory_order_relaxed); }
    int   getNumMeteredChannels() const noexcept    { return numMetered.load (std::memory_order_relaxed); }
    float getReferenceDbfs() const noexcept         { return referenceParam->load(); }

    // Highest needle reading since the last reset, in VU (dB re 0 VU).
    // Returns false if the needle hasn't moved since the reset.
    bool getMaxReadingVu (int channel, float& vu) const noexcept
    {
        const auto level = maxLevel[(size_t) channel].load (std::memory_order_relaxed);
        if (level <= 1.0e-7f)
            return false;
        vu = juce::Decibels::gainToDecibels (level / juce::Decibels::decibelsToGain (referenceParam->load()));
        return true;
    }
    void resetMaxReadings() noexcept                { for (auto& m : maxLevel) m.store (0.0f, std::memory_order_relaxed); }
    float getPeakLedDbfs() const noexcept           { return peakLedParam->load(); }

private:
    struct Ballistics
    {
        float position = 0.0f;   // integrated, rectified level (linear, RMS-calibrated)
        float velocity = 0.0f;
    };

    std::atomic<float>* referenceParam = nullptr;   // dBFS that reads 0 VU
    std::atomic<float>* peakLedParam   = nullptr;   // dBFS that lights the peak LED

    std::array<Ballistics, maxChannels> ballistics {};
    std::array<std::atomic<float>, maxChannels> vuRatio {};
    std::array<std::atomic<float>, maxChannels> peak {};
    std::array<std::atomic<float>, maxChannels> maxLevel {};   // raw needle level, reference-independent
    std::atomic<int> numMetered { maxChannels };

    float dt = 1.0f / 44100.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VuMeterAudioProcessor)
};
