#pragma once

#include "DrumEngine.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

namespace ids
{
    inline const juce::ParameterID master   { "master",   1 };
    inline const juce::ParameterID tune     { "tune",     1 };
    inline const juce::ParameterID bleed    { "bleed",    1 };
    inline const juce::ParameterID humanize { "humanize", 1 };
    inline const juce::ParameterID dynamics { "dynamics", 1 };
}

//==============================================================================
// Background thread: loads kits, frees retired kits, and madvise()s samples
// the audio thread just triggered. Never touched by the audio thread except
// through lock-free slots.
class KitWorker final : public juce::Thread
{
public:
    explicit KitWorker (DrumEngine& e);
    ~KitWorker() override;

    void requestLoad (const juce::File& kitFile, const juce::File& midimap);
    bool isLoading() const noexcept           { return loading.load(); }
    float getProgress() const noexcept        { return progress.load(); }
    juce::File getRequestedKit() const;

    // Message thread: returns a finished kit (or nullptr) and any error text.
    std::unique_ptr<Kit> takeResult (juce::String& error, bool& hasResult);

    // Any thread, lock-free: hand a kit over to be deleted. False if the
    // retire slots are momentarily full (try again later).
    bool retire (Kit*) noexcept;

    void run() override;

private:
    DrumEngine& engine;
    std::array<std::atomic<Kit*>, 16> retired {};

    juce::CriticalSection lock;
    juce::File requestKit, requestMidimap;
    std::atomic<int> requestId { 0 };
    int handledId = 0;
    std::unique_ptr<Kit> result;
    juce::String resultError;
    bool resultReady = false;

    std::atomic<bool> loading { false };
    std::atomic<float> progress { 0.0f };
};

//==============================================================================
class AlbionDrumsAudioProcessor final : public juce::AudioProcessor,
                                        public juce::ChangeBroadcaster,
                                        private juce::Timer,
                                        private juce::ValueTree::Listener
{
public:
    static constexpr int numDirectOuts = 16;

    AlbionDrumsAudioProcessor();
    ~AlbionDrumsAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts;

    // --- message-thread API for the editor ----------------------------------------
    void loadKit (const juce::File& kitFile, const juce::File& midimap = {});
    const Kit* getPublishedKit() const noexcept    { return publishedKit; }
    bool  isLoading() const noexcept               { return worker.isLoading(); }
    float getLoadProgress() const noexcept         { return worker.getProgress(); }
    juce::String getLastError() const              { return lastError; }
    juce::File getRequestedKit() const             { return worker.getRequestedKit(); }
    juce::ValueTree getChannelState (const juce::String& channelName);
    juce::ValueTree getInstrumentState (const juce::String& instrumentName);
    juce::ValueTree getUiState()                   { return apvts.state.getOrCreateChildWithName ("UI", nullptr); }

    int getActiveVoices() const noexcept           { return activeVoices.load (std::memory_order_relaxed); }

    // Per-kit mixer defaults (drum level/bleed/tune, mic level/pan), keyed by the kit's
    // file name and stored in ~/.config/Albion Road Audio/Drums kit defaults.xml.
    // They're applied automatically when a different kit file is loaded, never when a
    // project restores its own state.
    bool hasKitDefaults() const;
    bool saveKitDefaults();
    bool recallKitDefaults();

    // Any thread: play a note as if from MIDI (pad audition).
    void audition (int note, float velocity01) noexcept;

private:
    void timerCallback() override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void updateMixerGains();
    void updateInstrumentGains();
    void ensureMixerDefaults (const Kit&);
    bool applyKitDefaults (const Kit&);
    static juce::File kitDefaultsFile();
    static juce::ValueTree readKitDefaults();
    void requestLoadFromState();
    void swapInPendingKit() noexcept;
    void renderRange (juce::AudioBuffer<float>& buffer, int start, int end, int blockLength);

    DrumEngine engine;
    KitWorker worker { engine };

    std::atomic<Kit*> pendingKit { nullptr };   // message -> audio
    Kit* audioKit = nullptr;                    // audio thread
    Kit* deferredRetire = nullptr;              // audio thread
    Kit* publishedKit = nullptr;                // message thread
    juce::String lastError;
    bool applyDefaultsOnLoad = false;           // message thread: set by loadKit() for a new kit file

    std::atomic<float>* masterParam   = nullptr;
    std::atomic<float>* tuneParam     = nullptr;
    std::atomic<float>* bleedParam    = nullptr;
    std::atomic<float>* humanizeParam = nullptr;
    std::atomic<float>* dynamicsParam = nullptr;

    // render scratch + smoothed gains
    juce::AudioBuffer<float> scratch;
    std::array<float*, Kit::maxChannels> scratchPtrs {};
    std::array<float, Kit::maxChannels> curL {}, curR {}, curD {}, stepL {}, stepR {}, stepD {};
    float masterCurrent = 1.0f;
    std::atomic<int> activeVoices { 0 };
    int chunkSize = 512;

    // pad audition: lock-free single consumer
    juce::AbstractFifo auditionFifo { 64 };
    std::array<std::pair<int, float>, 64> auditionQueue {};

    JUCE_DECLARE_WEAK_REFERENCEABLE (AlbionDrumsAudioProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AlbionDrumsAudioProcessor)
};
