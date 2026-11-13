#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <vector>

namespace ids
{
    inline const juce::ParameterID time     { "time",     1 };
    inline const juce::ParameterID feedback { "feedback", 1 };
    inline const juce::ParameterID mode     { "mode",     1 };
    inline const juce::ParameterID tone     { "tone",     1 };
    inline const juce::ParameterID lowcut   { "lowcut",   1 };
    inline const juce::ParameterID drive    { "drive",    1 };
    inline const juce::ParameterID wow      { "wow",      1 };
    inline const juce::ParameterID spread   { "spread",   1 };
    inline const juce::ParameterID mix      { "mix",      1 };
}

// Albion Slapback: zero-latency single-tap echo with three characters.
//
// Per channel, per sample:
//   record  = saturate (in + feedback * echo)          -> delay line
//   echo    = filters (hermite-read (delay line, time + modulation))
//   out     = dry * dryGain + echo * wetGain
//
//   Tape    : wow + flutter + slow random drift, asymmetric soft saturation
//   BBD     : sine chorus-style modulation, extra low-pass that darkens as the
//             delay gets longer (like a real bucket-brigade clock), softer clip
//   Digital : clean; modulation and drive only if you dial them in
class AlbionSlapbackAudioProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int   maxChannels = 2;
    static constexpr float minTimeMs   = 30.0f;
    static constexpr float maxTimeMs   = 300.0f;

    AlbionSlapbackAudioProcessor();
    ~AlbionSlapbackAudioProcessor() override = default;

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
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentPreset.load(); }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts;

private:
    // Cytomic / TPT state-variable filter: stable under modulation, cheap.
    struct Svf
    {
        float ic1 = 0.0f, ic2 = 0.0f;
    };
    struct SvfCoeffs
    {
        float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f, k = 1.4142f;
        void set (float cutoff, float q, float sampleRate) noexcept;
    };

    struct Channel
    {
        std::vector<float> buffer;
        Svf lowpass, lowpass2, highpass;
        float wowPhase = 0.0f, flutterPhase = 0.0f;
    };

    std::atomic<float>* timeParam     = nullptr;
    std::atomic<float>* feedbackParam = nullptr;
    std::atomic<float>* modeParam     = nullptr;
    std::atomic<float>* toneParam     = nullptr;
    std::atomic<float>* lowcutParam   = nullptr;
    std::atomic<float>* driveParam    = nullptr;
    std::atomic<float>* wowParam      = nullptr;
    std::atomic<float>* spreadParam   = nullptr;
    std::atomic<float>* mixParam      = nullptr;

    float sampleRate = 44100.0f;
    float glideCoeff = 0.0f;     // delay-time smoothing (tape-style pitch glide)
    float driftCoeff = 0.0f;     // random drift low-pass
    int   mask = 0;
    int   writePos = 0;

    float timeSamples = 0.0f;    // smoothed
    float drift = 0.0f;
    float driftNorm = 1.0f;      // scales low-passed noise to ~unit RMS
    juce::uint32 rng = 0x1234567u;

    struct Ramp
    {
        float current = 0.0f, step = 0.0f;
        void reset (float v) noexcept              { current = v; step = 0.0f; }
        void setTarget (float t, int n) noexcept   { step = (t - current) / (float) juce::jmax (1, n); }
        float next() noexcept                      { current += step; return current; }
    };
    Ramp feedbackGain, dryGain, wetGain;

    std::array<Channel, maxChannels> channels;
    std::atomic<int> currentPreset { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AlbionSlapbackAudioProcessor)
};
