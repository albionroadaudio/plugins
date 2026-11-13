#pragma once

#include "Analysis.h"
#include "MatchEq.h"
#include "Profiles.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace ids
{
    inline const juce::ParameterID amount    { "amount",    1 };
    inline const juce::ParameterID maxDb     { "maxdb",     1 };
    inline const juce::ParameterID smoothing { "smoothing", 1 };
    inline const juce::ParameterID eqOn      { "eqon",      1 };
    inline const juce::ParameterID output    { "output",    1 };
    inline const juce::ParameterID tilt      { "tilt",      1 };
}

class AlbionReferenceAudioProcessor final : public juce::AudioProcessor,
                                            public juce::ChangeBroadcaster,
                                            private juce::Timer
{
public:
    AlbionReferenceAudioProcessor();
    ~AlbionReferenceAudioProcessor() override;

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

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts;
    AnalysisEngine analysis;

    // --- message-thread API ------------------------------------------------------------
    // reference
    bool hasReference() const noexcept                  { return referenceValid; }
    const juce::String& getReferenceName() const        { return referenceName; }
    const spectrum::Curve& getReferenceCurve() const    { return referenceCurve; }
    void setReference (const juce::String& name, const spectrum::Curve& curve);
    void clearReference();

    void startCapture();
    bool stopCapture();                                  // true: reference set from capture
    void loadReferenceFile (const juce::File&);
    bool isAnalysingFile() const noexcept               { return fileJobRunning.load(); }
    float getFileProgress() const noexcept              { return fileProgress.load(); }

    // mix learning
    void startLearn();
    bool stopLearn();
    bool hasLearnedMix() const noexcept                 { return mixValid; }
    double getLearnedSeconds() const noexcept           { return mixSeconds; }
    const spectrum::Curve& getLearnedMix() const        { return mixCurve; }
    void clearLearnedMix();

    // match EQ (100 % amount) for display
    const spectrum::Curve& getEqResponse() const        { return eqResponse; }
    const spectrum::Curve& getEqTarget() const          { return eqTarget; }
    bool isMatchReady() const noexcept                  { return referenceValid && mixValid; }

    // profiles
    std::vector<profiles::Profile> getProfiles() const; // built-ins then user profiles
    juce::File saveReferenceAsProfile (const juce::String& name);

    juce::String getStatusMessage() const               { return statusMessage; }

private:
    void timerCallback() override;
    void recomputeEq();
    void setStatus (const juce::String&);

    // audio
    matcheq::Processor eq;
    std::array<std::atomic<float>, matcheq::numBands> eqGains {};   // dB at 100 %
    juce::AudioBuffer<float> preCopy;
    juce::SmoothedValue<float> outputGain;
    std::atomic<float>* amountParam = nullptr;
    std::atomic<float>* maxDbParam = nullptr;
    std::atomic<float>* smoothingParam = nullptr;
    std::atomic<float>* eqOnParam = nullptr;
    std::atomic<float>* outputParam = nullptr;

    // message-thread state
    bool referenceValid = false, mixValid = false;
    juce::String referenceName, statusMessage;
    spectrum::Curve referenceCurve {}, mixCurve {}, eqResponse {}, eqTarget {};
    double mixSeconds = 0.0;
    float lastMax = -1.0f, lastSmoothing = -1.0f;

    // background file analysis
    juce::ThreadPool pool { 1 };
    std::atomic<bool> fileJobRunning { false }, cancelFileJob { false };
    std::atomic<float> fileProgress { 0.0f };
    juce::CriticalSection fileLock;
    bool fileResultReady = false, fileResultOk = false;
    spectrum::Curve fileResult {};
    juce::String fileResultName, fileResultError;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AlbionReferenceAudioProcessor)
};
