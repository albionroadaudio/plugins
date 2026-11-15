#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include "Dsp.h"

namespace ids
{
    inline const juce::ParameterID denoise { "denoise", 1 };
    inline const juce::ParameterID hpf     { "hpf",     1 };
    inline const juce::ParameterID comp1   { "comp1",   1 };
    inline const juce::ParameterID deess   { "deess",   1 };
    inline const juce::ParameterID mud     { "mud",     1 };
    inline const juce::ParameterID nasal   { "nasal",   1 };
    inline const juce::ParameterID air     { "air",     1 };
    inline const juce::ParameterID comp2   { "comp2",   1 };
    inline const juce::ParameterID output  { "output",  1 };

    // section on/off switches (the high-pass uses `hpf` itself)
    inline const juce::ParameterID denoiseOn { "denoiseOn", 1 };
    inline const juce::ParameterID comp1On   { "comp1On",   1 };
    inline const juce::ParameterID deessOn   { "deessOn",   1 };
    inline const juce::ParameterID eqOn      { "eqOn",      1 };
    inline const juce::ParameterID comp2On   { "comp2On",   1 };
}

// The fixed settings of each stage (the user's vocal chain).
namespace chain
{
    constexpr double hpfHz       = 120.0;
    constexpr double mudHz       = 200.0,   mudQ   = 0.8;    // wide cut
    constexpr double nasalHz     = 2600.0,  nasalQ = 4.0;    // narrow cut
    constexpr double airHz       = 11000.0, airQ   = 0.707;  // gentle high shelf

    constexpr double clampAttackMs = 1.0,  clampReleaseMs = 80.0, clampRatio = 4.0, clampTargetDb = 10.0;
    constexpr double glueAttackMs  = 30.0, glueReleaseMs  = 30.0, glueThresholdDb = -60.0, glueTargetDb = 5.0;
}

// ARA Vocal Multitool: denoise -> high-pass -> clamp compressor -> de-esser -> EQ -> glue compressor.
class VocalMultitoolProcessor final : public juce::AudioProcessor
{
public:
    VocalMultitoolProcessor();
    ~VocalMultitoolProcessor() override = default;

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
    enum Meter { mDenoise, mClamp, mDeEss, mGlue, numMeters };
    float takeMeterDb (Meter m) noexcept { return meters[(size_t) m].exchange (0.0f, std::memory_order_relaxed); }

private:
    static void raise (std::atomic<float>& a, float v) noexcept
    {
        auto cur = a.load (std::memory_order_relaxed);
        while (v > cur && ! a.compare_exchange_weak (cur, v, std::memory_order_relaxed)) {}
    }
    void updateEq (bool force) noexcept;
    static double on (const std::atomic<float>* p) noexcept { return p->load() > 0.5f ? 1.0 : 0.0; }

    std::atomic<float>* denoiseParam = nullptr;
    std::atomic<float>* hpfParam     = nullptr;
    std::atomic<float>* comp1Param   = nullptr;
    std::atomic<float>* deessParam   = nullptr;
    std::atomic<float>* mudParam     = nullptr;
    std::atomic<float>* nasalParam   = nullptr;
    std::atomic<float>* airParam     = nullptr;
    std::atomic<float>* comp2Param   = nullptr;
    std::atomic<float>* outputParam  = nullptr;
    std::atomic<float>* denoiseOnParam = nullptr;
    std::atomic<float>* comp1OnParam   = nullptr;
    std::atomic<float>* deessOnParam   = nullptr;
    std::atomic<float>* eqOnParam      = nullptr;
    std::atomic<float>* comp2OnParam   = nullptr;

    double sampleRate = 48000.0;
    vmt::Denoiser denoiser;
    vmt::Biquad hp1, hp2, mudEq, nasalEq, airEq;
    vmt::AutoComp clamp { { vmt::AutoComp::Style::Clamp, chain::clampAttackMs, chain::clampReleaseMs, false,
                            chain::clampRatio, 0.0, chain::clampTargetDb, 6.0, -45.0 } };
    vmt::AutoComp glue  { { vmt::AutoComp::Style::Glue, chain::glueAttackMs, chain::glueReleaseMs, true,
                            1.0, chain::glueThresholdDb, chain::glueTargetDb, 6.0, -45.0 } };
    vmt::DeEsser deEsser;

    // smoothed controls
    double hpfMix = 1.0, hpfCoeff = 0.0, ctlCoeff = 0.0;
    double clampAmt = 0.0, deessAmt = 0.0, glueAmt = 0.0, outGain = 1.0;
    double mudDb = 0.0, nasalDb = 0.0, airDb = 0.0;
    double eqCoeffChunk = 0.0;

    std::array<std::atomic<float>, numMeters> meters {};
    std::atomic<int> currentPreset { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalMultitoolProcessor)
};
