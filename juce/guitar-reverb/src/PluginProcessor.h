#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "ReverbEngines.h"
#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace ids
{
    inline const juce::ParameterID voice    { "voice",    1 };
    inline const juce::ParameterID decay    { "decay",    1 };
    inline const juce::ParameterID predelay { "predelay", 1 };
    inline const juce::ParameterID lowcut   { "lowcut",   1 };
    inline const juce::ParameterID highcut  { "highcut",  1 };
    inline const juce::ParameterID width    { "width",    1 };
    inline const juce::ParameterID duck     { "duck",     1 };
    inline const juce::ParameterID mix      { "mix",      1 };
}

// Albion Road Audio Guitar Reverb: bass and electric guitar reverb.
//
//   mono sum -> steep low cut (24 dB/oct) -> high cut (12 dB/oct) -> pre-delay
//            -> engine (tight room / studio room / chamber FDN, Dattorro plate, spring tank)
//            -> width (M/S) -> ducking (by the dry signal) -> mix
//
// The filters sit *before* the reverb, as on a mix-engineer's reverb send.
// Zero latency. Switching to a voice with a different engine crossfades.
class AlbionGuitarReverbAudioProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int   maxChannels  = 2;
    static constexpr float maxPreDelayMs = 100.0f;

    AlbionGuitarReverbAudioProcessor();
    ~AlbionGuitarReverbAudioProcessor() override = default;

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

    // Select a voice and move Decay / Pre-delay / Low Cut / High Cut to its defaults
    // (what clicking a voice button does).
    void selectVoice (int voiceIndex);
    int currentVoice() const;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts;

private:
    struct Svf { float ic1 = 0.0f, ic2 = 0.0f; };
    struct SvfCoeffs
    {
        float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f, k = 1.4142f;
        void set (double cutoff, double q, double sampleRate) noexcept;
    };

    struct Ramp
    {
        float current = 0.0f, step = 0.0f;
        void reset (float v) noexcept              { current = v; step = 0.0f; }
        void setTarget (float t, int n) noexcept   { step = (t - current) / (float) juce::jmax (1, n); }
        float next() noexcept                      { current += step; return current; }
    };

    void processChunk (float* left, float* right, int numOut, int n);
    rvb::Engine& engineFor (int voiceIndex);

    std::atomic<float>* voiceParam    = nullptr;
    std::atomic<float>* decayParam    = nullptr;
    std::atomic<float>* predelayParam = nullptr;
    std::atomic<float>* lowcutParam   = nullptr;
    std::atomic<float>* highcutParam  = nullptr;
    std::atomic<float>* widthParam    = nullptr;
    std::atomic<float>* duckParam     = nullptr;
    std::atomic<float>* mixParam      = nullptr;

    double sampleRate = 44100.0;

    rvb::FdnRoom tightRoom, studioRoom, chamber;
    rvb::Plate   plate;
    rvb::Spring  spring;

    int activeVoice = -1;
    rvb::Engine* active = nullptr;
    rvb::Engine* fading = nullptr;     // previous engine, fading out
    int fadeLen = 1, fadePos = 0;

    Svf hp1, hp2, lp;
    float smoothLow = 200.0f, smoothHigh = 5000.0f, freqSmooth = 0.0f;
    rvb::DelayLine preDelayLine;
    float preDelaySm = 0.0f, preDelayCoeff = 0.0f;

    float duckEnv = 0.0f, duckAttack = 0.0f, duckRelease = 0.0f;
    Ramp dryGain, wetGain, widthGain;

    static constexpr int chunk = 256;
    std::vector<float> monoIn, wetL, wetR, fadeL, fadeR;

    std::atomic<int> currentPreset { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AlbionGuitarReverbAudioProcessor)
};
