#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>

namespace ids
{
    inline const juce::ParameterID gain      { "gain",      1 };
    inline const juce::ParameterID threshold { "threshold", 1 };
    inline const juce::ParameterID ratio     { "ratio",     1 };
    inline const juce::ParameterID knee      { "knee",      1 };
    inline const juce::ParameterID attack    { "attack",    1 };
    inline const juce::ParameterID release   { "release",   1 };
    inline const juce::ParameterID makeup    { "makeup",    1 };
    inline const juce::ParameterID mix       { "mix",       1 };
}

// Albion 76: a zero-latency FET-style compressor.
//
// Signal path (per sample, stereo-linked):
//   in -> input Gain -> [peak detector -> soft-knee gain computer -> branching
//   attack/release with program-dependent release] -> gain reduction ->
//   FET/output-stage colour (polynomial, GR-dependent drive) -> DC block ->
//   Makeup -> Mix with the untouched input
//
// No oversampling, no lookahead: latency is exactly 0 samples.
class Albion76AudioProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int maxChannels = 2;

    Albion76AudioProcessor();
    ~Albion76AudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // --- factory presets, exposed as host programs too ---------------------
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentPreset.load(); }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts;

    // Largest gain reduction (positive dB) since the last call; consumes it.
    float takeGainReductionDb() noexcept { return grMeterDb.exchange (0.0f, std::memory_order_relaxed); }

private:
    struct Ramp   // per-sample linear smoothing of a block-rate target
    {
        float current = 0.0f, step = 0.0f;
        void reset (float v) noexcept               { current = v; step = 0.0f; }
        void setTarget (float t, int n) noexcept    { step = (t - current) / (float) juce::jmax (1, n); }
        float next() noexcept                       { current += step; return current; }
    };

    struct DcBlocker
    {
        float x1 = 0.0f, y1 = 0.0f;
    };

    std::atomic<float>* gainParam      = nullptr;
    std::atomic<float>* thresholdParam = nullptr;
    std::atomic<float>* ratioParam     = nullptr;
    std::atomic<float>* kneeParam      = nullptr;
    std::atomic<float>* attackParam    = nullptr;
    std::atomic<float>* releaseParam   = nullptr;
    std::atomic<float>* makeupParam    = nullptr;
    std::atomic<float>* mixParam       = nullptr;

    float sampleRate = 44100.0f;
    float slowCoeff  = 0.0f;   // program-dependency tracker (~600 ms)
    float dcCoeff    = 0.0f;

    float envDb  = 0.0f;       // smoothed gain reduction, positive dB
    float slowDb = 0.0f;       // long-term average of envDb

    Ramp inGain, makeupGain, mixAmount;
    std::array<DcBlocker, maxChannels> dc {};

    std::atomic<float> grMeterDb { 0.0f };
    std::atomic<int> currentPreset { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Albion76AudioProcessor)
};
