#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <set>
#include <utility>

namespace
{
    const juce::Identifier kitNode { "KIT" }, mixerNode { "MIXER" }, channelNode { "CH" },
                           drumsNode { "DRUMS" }, instrumentNode { "INSTR" };
    const juce::Identifier pathProp { "path" }, midimapProp { "midimap" };
    const juce::Identifier nameProp { "name" }, levelProp { "level" }, panProp { "pan" },
                           muteProp { "mute" }, soloProp { "solo" };

    constexpr float minDb = -60.0f;   // fader bottom = off

    // Default pans: channels that form a pair like "OHL"/"OHR", "Amb L"/"Amb R",
    // "Room_Left"/"Room_Right" are spread hard left/right; everything else centre.
    float defaultPan (const juce::String& name, const std::vector<juce::String>& all)
    {
        auto exists = [&all] (const juce::String& n) { return std::find (all.begin(), all.end(), n) != all.end(); };
        struct Suffix { const char* l; const char* r; };
        static constexpr Suffix suffixes[] = { { "Left", "Right" }, { "left", "right" }, { "LEFT", "RIGHT" },
                                               { "L", "R" }, { "l", "r" } };
        for (const auto& s : suffixes)
        {
            if (name.endsWith (s.l) && exists (name.dropLastCharacters ((int) strlen (s.l)) + s.r))
                return -1.0f;
            if (name.endsWith (s.r) && exists (name.dropLastCharacters ((int) strlen (s.r)) + s.l))
                return 1.0f;
        }
        return 0.0f;
    }
}

//==============================================================================
KitWorker::KitWorker (DrumEngine& e) : juce::Thread ("Albion Drums kit loader"), engine (e) {}

KitWorker::~KitWorker()
{
    stopThread (4000);
    for (auto& slot : retired)
        delete slot.exchange (nullptr);
}

void KitWorker::requestLoad (const juce::File& kitFile, const juce::File& midimap)
{
    {
        const juce::ScopedLock sl (lock);
        requestKit = kitFile;
        requestMidimap = midimap;
    }
    ++requestId;
    loading = true;
    notify();
}

juce::File KitWorker::getRequestedKit() const
{
    const juce::ScopedLock sl (lock);
    return requestKit;
}

std::unique_ptr<Kit> KitWorker::takeResult (juce::String& error, bool& hasResult)
{
    const juce::ScopedLock sl (lock);
    hasResult = resultReady;
    resultReady = false;
    error = resultError;
    return std::move (result);
}

bool KitWorker::retire (Kit* k) noexcept
{
    for (auto& slot : retired)
    {
        Kit* expected = nullptr;
        if (slot.compare_exchange_strong (expected, k))
            return true;
    }
    return false;
}

void KitWorker::run()
{
    while (! threadShouldExit())
    {
        wait (5);

        // Snapshot the retired kits *before* draining prefetch requests: anything
        // the audio thread queued for a kit was queued before it retired that kit,
        // so after the drain nothing can still point into the snapshot.
        std::array<Kit*, 16> toDelete {};
        for (size_t i = 0; i < retired.size(); ++i)
            toDelete[i] = retired[i].exchange (nullptr);

        engine.prefetchFifo.read (engine.prefetchFifo.getNumReady()).forEach ([this] (int index)
        {
            if (const auto* s = engine.prefetchQueue[(size_t) index])
                for (const auto& stream : s->streams)
                    stream.wav->adviseWillNeed();
        });

        for (auto* k : toDelete)
            delete k;

        // --- kit load request -------------------------------------------------------
        const auto id = requestId.load();
        if (id == handledId)
            continue;
        handledId = id;

        juce::File kitFile, midimap;
        {
            const juce::ScopedLock sl (lock);
            kitFile = requestKit;
            midimap = requestMidimap;
        }

        loading = true;
        juce::String error;
        KitLoader loader;
        auto kit = loader.load (kitFile, midimap, progress,
                                [this, id] { return ! threadShouldExit() && requestId.load() == id; }, error);

        if (requestId.load() == id)   // not superseded by a newer request
        {
            const juce::ScopedLock sl (lock);
            result = std::move (kit);
            resultError = result == nullptr ? (error.isEmpty() ? juce::String ("Kit loading was cancelled") : error) : juce::String();
            resultReady = true;
            loading = false;
        }
    }
}

//==============================================================================
AlbionDrumsAudioProcessor::AlbionDrumsAudioProcessor()
    : AudioProcessor ([]
      {
          BusesProperties buses;
          buses = buses.withOutput ("Main", juce::AudioChannelSet::stereo(), true);
          for (int i = 1; i <= numDirectOuts; ++i)
              buses = buses.withOutput ("Direct " + juce::String (i), juce::AudioChannelSet::mono(), false);
          return buses;
      }()),
      apvts (*this, nullptr, "AlbionDrumsState", createParameterLayout())
{
    masterParam   = apvts.getRawParameterValue (ids::master.getParamID());
    tuneParam     = apvts.getRawParameterValue (ids::tune.getParamID());
    bleedParam    = apvts.getRawParameterValue (ids::bleed.getParamID());
    humanizeParam = apvts.getRawParameterValue (ids::humanize.getParamID());
    dynamicsParam = apvts.getRawParameterValue (ids::dynamics.getParamID());

    apvts.state.getOrCreateChildWithName (kitNode, nullptr);
    apvts.state.getOrCreateChildWithName (mixerNode, nullptr);
    apvts.state.addListener (this);

    worker.startThread (juce::Thread::Priority::low);
    startTimerHz (20);
}

AlbionDrumsAudioProcessor::~AlbionDrumsAudioProcessor()
{
    stopTimer();
    apvts.state.removeListener (this);
    worker.stopThread (4000);

    std::set<Kit*> owned { pendingKit.exchange (nullptr), audioKit, deferredRetire, publishedKit };
    owned.erase (nullptr);
    for (auto* k : owned)
        delete k;
}

juce::AudioProcessorValueTreeState::ParameterLayout AlbionDrumsAudioProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " %"; }).withLabel ("%");

    NormalisableRange<float> masterRange (-60.0f, 12.0f, 0.1f);
    masterRange.setSkewForCentre (-12.0f);
    layout.add (std::make_unique<AudioParameterFloat> (ids::master, "Master", masterRange, -6.0f,   // headroom: kits sum many mics
        AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return v <= -59.9f ? String ("-inf dB") : (v > 0 ? "+" : "") + String (v, 1) + " dB"; })
            .withLabel ("dB")));

    layout.add (std::make_unique<AudioParameterFloat> (ids::tune, "Tune", NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return (v > 0 ? "+" : "") + String (v, 1) + " st"; })
            .withLabel ("st")));

    layout.add (std::make_unique<AudioParameterFloat> (ids::bleed,    "Bleed",    NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ids::humanize, "Humanize", NormalisableRange<float> (0.0f, 100.0f, 0.1f),   0.0f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ids::dynamics, "Dynamics", NormalisableRange<float> (0.0f, 100.0f, 0.1f),   0.0f, pct));

    return layout;
}

//==============================================================================
bool AlbionDrumsAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (! layouts.inputBuses.isEmpty() && ! layouts.getMainInputChannelSet().isDisabled())
        return false;
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    for (int i = 1; i < layouts.outputBuses.size(); ++i)
    {
        const auto& set = layouts.outputBuses.getReference (i);
        if (! set.isDisabled() && set != juce::AudioChannelSet::mono())
            return false;
    }
    return true;
}

void AlbionDrumsAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate);
    chunkSize = juce::jlimit (16, 128, samplesPerBlock);   // render in short chunks: the scratch buffer stays small and in cache
    scratch.setSize (Kit::maxChannels, chunkSize, false, true, false);
    for (int i = 0; i < Kit::maxChannels; ++i)
        scratchPtrs[(size_t) i] = scratch.getWritePointer (i);

    swapInPendingKit();
    engine.setKit (audioKit);
    masterCurrent = juce::Decibels::decibelsToGain (masterParam->load(), minDb);
    setLatencySamples (0);
}

void AlbionDrumsAudioProcessor::swapInPendingKit() noexcept
{
    if (deferredRetire != nullptr && worker.retire (deferredRetire))
        deferredRetire = nullptr;

    if (auto* k = pendingKit.exchange (nullptr))
    {
        auto* old = audioKit;
        audioKit = k;
        engine.setKit (k);

        for (int ch = 0; ch < Kit::maxChannels; ++ch)
        {
            curL[(size_t) ch] = k->gainL[(size_t) ch].load (std::memory_order_relaxed);
            curR[(size_t) ch] = k->gainR[(size_t) ch].load (std::memory_order_relaxed);
            curD[(size_t) ch] = k->gainDirect[(size_t) ch].load (std::memory_order_relaxed);
        }

        if (old != nullptr && ! worker.retire (old))
        {
            if (deferredRetire == nullptr)
                deferredRetire = old;
            // else: both slots busy; extremely unlikely, leak rather than block
        }
    }
}

void AlbionDrumsAudioProcessor::audition (int note, float velocity01) noexcept
{
    const auto scope = auditionFifo.write (1);
    if (scope.blockSize1 > 0)
        auditionQueue[(size_t) scope.startIndex1] = { note, velocity01 };
}

void AlbionDrumsAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    swapInPendingKit();

    buffer.clear();
    const int numSamples = buffer.getNumSamples();
    if (numSamples == 0)
        return;

    engine.tuneSemitones = tuneParam->load();
    engine.bleed         = bleedParam->load() * 0.01f;
    engine.humanize      = humanizeParam->load() * 0.01f;
    engine.dynamics      = dynamicsParam->load() * 0.01f;

    // per-block gain ramps for the mixer
    if (audioKit != nullptr)
        for (int ch = 0; ch < audioKit->numChannels(); ++ch)
        {
            const auto inv = 1.0f / (float) numSamples;
            stepL[(size_t) ch] = (audioKit->gainL[(size_t) ch].load (std::memory_order_relaxed) - curL[(size_t) ch]) * inv;
            stepR[(size_t) ch] = (audioKit->gainR[(size_t) ch].load (std::memory_order_relaxed) - curR[(size_t) ch]) * inv;
            stepD[(size_t) ch] = (audioKit->gainDirect[(size_t) ch].load (std::memory_order_relaxed) - curD[(size_t) ch]) * inv;
        }

    auditionFifo.read (auditionFifo.getNumReady()).forEach ([this] (int i)
    {
        engine.noteOn (auditionQueue[(size_t) i].first, auditionQueue[(size_t) i].second);
    });

    int pos = 0;
    for (const auto meta : midi)
    {
        const auto when = juce::jlimit (pos, numSamples, meta.samplePosition);
        renderRange (buffer, pos, when, numSamples);
        pos = when;

        const auto msg = meta.getMessage();
        if (msg.isNoteOn())
            engine.noteOn (msg.getNoteNumber(), (msg.getVelocity() - 0.5f) / 127.0f);   // DrumGizmo's centring
        else if (msg.isAftertouch() && msg.getAfterTouchValue() > 0)
            engine.chokeNote (msg.getNoteNumber());
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            engine.allNotesOff();
    }
    renderRange (buffer, pos, numSamples, numSamples);

    // snap ramps to their targets to avoid drift
    if (audioKit != nullptr)
        for (int ch = 0; ch < audioKit->numChannels(); ++ch)
        {
            curL[(size_t) ch] = audioKit->gainL[(size_t) ch].load (std::memory_order_relaxed);
            curR[(size_t) ch] = audioKit->gainR[(size_t) ch].load (std::memory_order_relaxed);
            curD[(size_t) ch] = audioKit->gainDirect[(size_t) ch].load (std::memory_order_relaxed);
        }

    // master on the main stereo bus
    const auto masterTarget = juce::Decibels::decibelsToGain (masterParam->load(), minDb);
    auto mainBus = getBusBuffer (buffer, false, 0);
    for (int ch = 0; ch < mainBus.getNumChannels(); ++ch)
        mainBus.applyGainRamp (ch, 0, numSamples, masterCurrent, masterTarget);
    masterCurrent = masterTarget;
    activeVoices.store (engine.activeVoices(), std::memory_order_relaxed);
}

void AlbionDrumsAudioProcessor::renderRange (juce::AudioBuffer<float>& buffer, int start, int end, int)
{
    while (start < end)
    {
        const int n = juce::jmin (chunkSize, end - start);

        if (audioKit == nullptr)
        {
            engine.render (scratchPtrs.data(), n);
            start += n;
            continue;
        }

        const auto numCh = audioKit->numChannels();
        for (int ch = 0; ch < numCh; ++ch)
            juce::FloatVectorOperations::clear (scratchPtrs[(size_t) ch], n);

        engine.render (scratchPtrs.data(), n);

        auto mainBus = getBusBuffer (buffer, false, 0);
        float* outL = mainBus.getWritePointer (0, start);
        float* outR = mainBus.getWritePointer (1, start);

        for (int ch = 0; ch < numCh; ++ch)
        {
            const float* src = scratchPtrs[(size_t) ch];
            auto gl = curL[(size_t) ch], gr = curR[(size_t) ch];
            const auto sl = stepL[(size_t) ch], sr = stepR[(size_t) ch];
            float peak = 0.0f;

            for (int k = 0; k < n; ++k)
            {
                const auto x = src[k];
                outL[k] += x * gl;
                outR[k] += x * gr;
                peak = juce::jmax (peak, std::abs (x) * juce::jmax (gl, gr));
                gl += sl;
                gr += sr;
            }
            curL[(size_t) ch] = gl;
            curR[(size_t) ch] = gr;

            // direct outs: kit channel i -> "Direct i+1", post fader, pre pan
            const int bus = ch + 1;
            if (bus <= numDirectOuts && bus < getBusCount (false) && getBus (false, bus)->isEnabled())
            {
                auto direct = getBusBuffer (buffer, false, bus);
                float* d = direct.getWritePointer (0, start);
                auto gd = curD[(size_t) ch];
                const auto sd = stepD[(size_t) ch];
                for (int k = 0; k < n; ++k)
                {
                    d[k] += src[k] * gd;
                    gd += sd;
                }
                curD[(size_t) ch] = gd;
            }
            else
                curD[(size_t) ch] += stepD[(size_t) ch] * (float) n;

            auto& m = audioKit->meter[(size_t) ch];
            if (peak > m.load (std::memory_order_relaxed))
                m.store (peak, std::memory_order_relaxed);
        }

        start += n;
    }
}

//==============================================================================
void AlbionDrumsAudioProcessor::loadKit (const juce::File& kitFile, const juce::File& midimap)
{
    // a different kit gets its saved defaults; a new MIDI map for the same kit keeps the mix
    applyDefaultsOnLoad = publishedKit == nullptr || publishedKit->kitFile != kitFile;
    auto kitState = apvts.state.getOrCreateChildWithName (kitNode, nullptr);
    kitState.setProperty (pathProp, kitFile.getFullPathName(), nullptr);
    kitState.setProperty (midimapProp, midimap.getFullPathName(), nullptr);
    lastError.clear();
    worker.requestLoad (kitFile, midimap);
    sendChangeMessage();
}

void AlbionDrumsAudioProcessor::requestLoadFromState()
{
    const auto kitState = apvts.state.getChildWithName (kitNode);
    const juce::File kitFile (kitState.getProperty (pathProp).toString());
    const auto midimapPath = kitState.getProperty (midimapProp).toString();
    const juce::File midimap = midimapPath.isNotEmpty() ? juce::File (midimapPath) : juce::File();

    if (kitState.getProperty (pathProp).toString().isEmpty())
        return;

    const bool sameAsLoaded = publishedKit != nullptr && publishedKit->kitFile == kitFile
                              && (midimap == juce::File() || publishedKit->midimapFile == midimap);
    if (sameAsLoaded || (worker.isLoading() && worker.getRequestedKit() == kitFile))
        return;

    applyDefaultsOnLoad = false;   // restoring a project: its own mixer state wins

    if (! kitFile.existsAsFile())
    {
        lastError = "Kit not found: " + kitFile.getFullPathName();
        sendChangeMessage();
        return;
    }
    worker.requestLoad (kitFile, midimap);
    sendChangeMessage();
}

void AlbionDrumsAudioProcessor::timerCallback()
{
    juce::String error;
    bool hasResult = false;
    auto kit = worker.takeResult (error, hasResult);
    if (! hasResult)
        return;

    if (kit == nullptr)
    {
        lastError = error;
        applyDefaultsOnLoad = false;
        sendChangeMessage();
        return;
    }

    lastError.clear();
    ensureMixerDefaults (*kit);
    if (std::exchange (applyDefaultsOnLoad, false))
        applyKitDefaults (*kit);

    auto kitState = apvts.state.getOrCreateChildWithName (kitNode, nullptr);
    kitState.setProperty (pathProp, kit->kitFile.getFullPathName(), nullptr);
    kitState.setProperty (midimapProp, kit->midimapFile.getFullPathName(), nullptr);

    publishedKit = kit.release();
    updateMixerGains();
    updateInstrumentGains();

    if (auto* stale = pendingKit.exchange (publishedKit))   // never reached the audio thread
        if (! worker.retire (stale))
            delete stale;   // worker slots full: safe here, the audio thread never saw it

    sendChangeMessage();
}

//==============================================================================
juce::ValueTree AlbionDrumsAudioProcessor::getChannelState (const juce::String& channelName)
{
    auto mixer = apvts.state.getOrCreateChildWithName (mixerNode, nullptr);
    auto ch = mixer.getChildWithProperty (nameProp, channelName);
    if (! ch.isValid())
    {
        ch = juce::ValueTree (channelNode);
        ch.setProperty (nameProp, channelName, nullptr);
        ch.setProperty (levelProp, 0.0, nullptr);
        ch.setProperty (panProp, 0.0, nullptr);
        ch.setProperty (muteProp, false, nullptr);
        ch.setProperty (soloProp, false, nullptr);
        mixer.appendChild (ch, nullptr);
    }
    return ch;
}

juce::ValueTree AlbionDrumsAudioProcessor::getInstrumentState (const juce::String& instrumentName)
{
    auto drumsTree = apvts.state.getOrCreateChildWithName (drumsNode, nullptr);
    auto in = drumsTree.getChildWithProperty (nameProp, instrumentName);
    if (! in.isValid())
    {
        in = juce::ValueTree (instrumentNode);
        in.setProperty (nameProp, instrumentName, nullptr);
        in.setProperty (levelProp, 0.0, nullptr);
        in.setProperty (muteProp, false, nullptr);
        in.setProperty (soloProp, false, nullptr);
        in.setProperty ("bleed", 100.0, nullptr);  // % of the kit Bleed
        in.setProperty ("tune", 0.0, nullptr);     // semitones on top of the kit Tune
        drumsTree.appendChild (in, nullptr);
    }
    return in;
}

void AlbionDrumsAudioProcessor::updateInstrumentGains()
{
    if (publishedKit == nullptr)
        return;

    const auto drumsTree = apvts.state.getChildWithName (drumsNode);
    bool anySolo = false;
    for (const auto& inst : publishedKit->instruments)
        anySolo |= (bool) drumsTree.getChildWithProperty (nameProp, inst.name).getProperty (soloProp, false);

    for (size_t i = 0; i < publishedKit->instruments.size(); ++i)
    {
        const auto in    = drumsTree.getChildWithProperty (nameProp, publishedKit->instruments[i].name);
        const auto level = (float) in.getProperty (levelProp, 0.0);
        const bool mute  = in.getProperty (muteProp, false);
        const bool solo  = in.getProperty (soloProp, false);
        const auto g = (mute || (anySolo && ! solo) || level <= minDb + 0.05f) ? 0.0f : juce::Decibels::decibelsToGain (level);
        publishedKit->instrumentGain[i]->store (g, std::memory_order_relaxed);

        publishedKit->instrumentBleed[i]->store (juce::jlimit (0.0f, 1.0f, (float) in.getProperty ("bleed", 100.0) * 0.01f),
                                                 std::memory_order_relaxed);
        publishedKit->instrumentTune[i]->store (juce::jlimit (-12.0f, 12.0f, (float) in.getProperty ("tune", 0.0)),
                                                std::memory_order_relaxed);
    }
}

void AlbionDrumsAudioProcessor::ensureMixerDefaults (const Kit& kit)
{
    auto mixer = apvts.state.getOrCreateChildWithName (mixerNode, nullptr);
    for (const auto& name : kit.channelNames)
        if (! mixer.getChildWithProperty (nameProp, name).isValid())
            getChannelState (name).setProperty (panProp, defaultPan (name, kit.channelNames), nullptr);
}

juce::File AlbionDrumsAudioProcessor::kitDefaultsFile()
{
    // ALBION_DRUMS_KIT_DEFAULTS overrides the location (used by the test tool)
    const auto overridePath = juce::SystemStats::getEnvironmentVariable ("ALBION_DRUMS_KIT_DEFAULTS", {});
    if (juce::File::isAbsolutePath (overridePath))
        return juce::File (overridePath);
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Albion Road Audio").getChildFile ("Drums kit defaults.xml");
}

juce::ValueTree AlbionDrumsAudioProcessor::readKitDefaults()
{
    if (auto xml = juce::XmlDocument::parse (kitDefaultsFile()))
        if (auto tree = juce::ValueTree::fromXml (*xml); tree.hasType ("KITDEFAULTS"))
            return tree;
    return juce::ValueTree ("KITDEFAULTS");
}

bool AlbionDrumsAudioProcessor::hasKitDefaults() const
{
    return publishedKit != nullptr
        && readKitDefaults().getChildWithProperty ("file", publishedKit->kitFile.getFileName()).isValid();
}

bool AlbionDrumsAudioProcessor::saveKitDefaults()
{
    if (publishedKit == nullptr)
        return false;

    auto all = readKitDefaults();
    const auto key = publishedKit->kitFile.getFileName();
    if (auto old = all.getChildWithProperty ("file", key); old.isValid())
        all.removeChild (old, nullptr);

    juce::ValueTree entry ("KIT");
    entry.setProperty ("file", key, nullptr);
    for (const auto& inst : publishedKit->instruments)
    {
        const auto in = getInstrumentState (inst.name);
        juce::ValueTree d (instrumentNode);
        d.setProperty (nameProp, inst.name, nullptr);
        for (const auto& prop : { levelProp, juce::Identifier ("bleed"), juce::Identifier ("tune") })
            d.setProperty (prop, in.getProperty (prop), nullptr);
        entry.appendChild (d, nullptr);
    }
    for (const auto& name : publishedKit->channelNames)
    {
        const auto ch = getChannelState (name);
        juce::ValueTree d (channelNode);
        d.setProperty (nameProp, name, nullptr);
        d.setProperty (levelProp, ch.getProperty (levelProp), nullptr);
        d.setProperty (panProp, ch.getProperty (panProp), nullptr);
        entry.appendChild (d, nullptr);
    }
    all.appendChild (entry, nullptr);

    const auto file = kitDefaultsFile();
    auto xml = all.createXml();
    return file.getParentDirectory().createDirectory() && xml != nullptr && xml->writeTo (file);
}

bool AlbionDrumsAudioProcessor::recallKitDefaults()
{
    return publishedKit != nullptr && applyKitDefaults (*publishedKit);
}

bool AlbionDrumsAudioProcessor::applyKitDefaults (const Kit& kit)
{
    const auto entry = readKitDefaults().getChildWithProperty ("file", kit.kitFile.getFileName());
    if (! entry.isValid())
        return false;

    for (const auto& d : entry)
    {
        const auto name = d.getProperty (nameProp).toString();
        if (d.hasType (instrumentNode))
        {
            if (std::none_of (kit.instruments.begin(), kit.instruments.end(), [&] (const auto& i) { return i.name == name; }))
                continue;
            auto in = getInstrumentState (name);
            for (const auto& prop : { levelProp, juce::Identifier ("bleed"), juce::Identifier ("tune") })
                if (d.hasProperty (prop))
                    in.setProperty (prop, d.getProperty (prop), nullptr);
        }
        else if (d.hasType (channelNode))
        {
            if (std::find (kit.channelNames.begin(), kit.channelNames.end(), name) == kit.channelNames.end())
                continue;
            auto ch = getChannelState (name);
            for (const auto& prop : { levelProp, panProp })
                if (d.hasProperty (prop))
                    ch.setProperty (prop, d.getProperty (prop), nullptr);
        }
    }
    return true;
}

void AlbionDrumsAudioProcessor::updateMixerGains()
{
    if (publishedKit == nullptr)
        return;

    const auto mixer = apvts.state.getChildWithName (mixerNode);
    bool anySolo = false;
    for (const auto& name : publishedKit->channelNames)
        anySolo |= (bool) mixer.getChildWithProperty (nameProp, name).getProperty (soloProp, false);

    for (int i = 0; i < publishedKit->numChannels(); ++i)
    {
        const auto ch    = mixer.getChildWithProperty (nameProp, publishedKit->channelNames[(size_t) i]);
        const auto level = (float) ch.getProperty (levelProp, 0.0);
        const auto pan   = juce::jlimit (-1.0f, 1.0f, (float) ch.getProperty (panProp, 0.0));
        const bool mute  = ch.getProperty (muteProp, false);
        const bool solo  = ch.getProperty (soloProp, false);

        const auto g = (mute || (anySolo && ! solo) || level <= minDb + 0.05f) ? 0.0f : juce::Decibels::decibelsToGain (level);
        publishedKit->gainL[(size_t) i]      = g * juce::jmin (1.0f, 1.0f - pan);   // balance law: centre = unity
        publishedKit->gainR[(size_t) i]      = g * juce::jmin (1.0f, 1.0f + pan);
        publishedKit->gainDirect[(size_t) i] = g;
    }
}

void AlbionDrumsAudioProcessor::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier&)
{
    if (tree.hasType (channelNode))
        updateMixerGains();
    else if (tree.hasType (instrumentNode))
        updateInstrumentGains();
}

//==============================================================================
juce::AudioProcessorEditor* AlbionDrumsAudioProcessor::createEditor()
{
    return new AlbionDrumsAudioProcessorEditor (*this);
}

void AlbionDrumsAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void AlbionDrumsAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            apvts.state.getOrCreateChildWithName (kitNode, nullptr);
            apvts.state.getOrCreateChildWithName (mixerNode, nullptr);

            auto apply = [this] { requestLoadFromState(); updateMixerGains(); updateInstrumentGains(); };
            if (juce::MessageManager::getInstance()->isThisTheMessageThread())
                apply();
            else
                juce::MessageManager::callAsync ([safe = juce::WeakReference<AlbionDrumsAudioProcessor> (this), apply]
                                                 { if (safe != nullptr) apply(); });
        }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AlbionDrumsAudioProcessor();
}
