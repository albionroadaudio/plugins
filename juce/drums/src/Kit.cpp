#include "Kit.h"
#include <map>
#include <unordered_map>

namespace
{
    constexpr size_t headBytes = 256 * 1024;   // pre-faulted attack per file

    juce::String rootTagOf (const juce::File& f)
    {
        juce::FileInputStream in (f);
        if (! in.openedOk())
            return {};
        const auto head = in.readString().substring (0, 2048);
        // skip the xml declaration / comments, find the first real element
        for (int i = head.indexOfChar ('<'); i >= 0; i = head.indexOfChar (i + 1, '<'))
        {
            const auto c = head[i + 1];
            if (c == '?' || c == '!')
                continue;
            return head.substring (i + 1).upToFirstOccurrenceOf (" ", false, false)
                                         .upToFirstOccurrenceOf (">", false, false)
                                         .upToFirstOccurrenceOf ("\n", false, false).trim();
        }
        return {};
    }

    juce::Array<juce::File> xmlFilesWithRoot (const juce::File& folder, const juce::String& root)
    {
        juce::Array<juce::File> result;
        for (const auto& entry : juce::RangedDirectoryIterator (folder, false, "*.xml", juce::File::findFiles))
            if (rootTagOf (entry.getFile()) == root)
                result.add (entry.getFile());
        result.sort();
        return result;
    }

    enum class MainState { unset, isMain, notMain };
}

juce::Array<juce::File> kitfiles::findKitVariants (const juce::File& folder) { return xmlFilesWithRoot (folder, "drumkit"); }
juce::Array<juce::File> kitfiles::findMidimaps (const juce::File& folder)    { return xmlFilesWithRoot (folder, "midimap"); }

//==============================================================================
std::unique_ptr<Kit> KitLoader::load (const juce::File& kitFile, juce::File midimapFile,
                                      std::atomic<float>& progress,
                                      const std::function<bool()>& shouldContinue,
                                      juce::String& error)
{
    progress = 0.0f;
    auto kit = std::make_unique<Kit>();
    kit->kitFile = kitFile;
    const auto kitDir = kitFile.getParentDirectory();

    const auto kitXml = juce::XmlDocument::parse (kitFile);
    if (kitXml == nullptr || ! kitXml->hasTagName ("drumkit"))
    {
        error = "Not a DrumGizmo drumkit file: " + kitFile.getFileName();
        return nullptr;
    }

    // --- drumkit metadata ---------------------------------------------------------
    const bool kitDeclaresRate = kitXml->hasAttribute ("samplerate");
    kit->sampleRate  = kitXml->getDoubleAttribute ("samplerate", 44100.0);
    kit->name        = kitXml->getStringAttribute ("name");
    kit->description = kitXml->getStringAttribute ("description");
    juce::String defaultMidimap;

    if (auto* meta = kitXml->getChildByName ("metadata"))
    {
        if (auto* t = meta->getChildByName ("title"))        kit->name = t->getAllSubText().trim();
        if (auto* d = meta->getChildByName ("description"))  kit->description = d->getAllSubText().trim();
        if (auto* img = meta->getChildByName ("image"))      kit->imageFile = kitDir.getChildFile (img->getStringAttribute ("src"));
        if (auto* dm = meta->getChildByName ("defaultmidimap")) defaultMidimap = dm->getStringAttribute ("src");
    }
    if (kit->name.isEmpty())
        kit->name = kitFile.getFileNameWithoutExtension();

    std::unordered_map<juce::String, int> channelIndex;
    if (auto* channels = kitXml->getChildByName ("channels"))
        for (auto* ch : channels->getChildWithTagNameIterator ("channel"))
        {
            if (kit->numChannels() >= Kit::maxChannels)
                break;
            channelIndex[ch->getStringAttribute ("name")] = kit->numChannels();
            kit->channelNames.push_back (ch->getStringAttribute ("name"));
        }

    // --- collect instrument references first so progress is meaningful --------
    struct ChannelMap { juce::String out; MainState main; };
    struct InstrumentRef
    {
        juce::String name, file, group;
        std::map<juce::String, ChannelMap> channelMap;
        std::vector<std::pair<juce::String, float>> chokes;
    };
    std::vector<InstrumentRef> refs;

    if (auto* instruments = kitXml->getChildByName ("instruments"))
        for (auto* in : instruments->getChildWithTagNameIterator ("instrument"))
        {
            InstrumentRef r;
            r.name  = in->getStringAttribute ("name");
            r.file  = in->getStringAttribute ("file");
            r.group = in->getStringAttribute ("group");
            for (auto* cm : in->getChildWithTagNameIterator ("channelmap"))
            {
                const auto mainAttr = cm->getStringAttribute ("main");
                r.channelMap[cm->getStringAttribute ("in")] = { cm->getStringAttribute ("out"),
                    mainAttr == "true" ? MainState::isMain : (mainAttr == "false" ? MainState::notMain : MainState::unset) };
            }
            if (auto* chokes = in->getChildByName ("chokes"))
                for (auto* c : chokes->getChildWithTagNameIterator ("choke"))
                    r.chokes.emplace_back (c->getStringAttribute ("instrument"), (float) c->getDoubleAttribute ("choketime", 68.0));
            refs.push_back (std::move (r));
        }

    if (refs.empty())
    {
        error = "The kit has no instruments";
        return nullptr;
    }

    std::unordered_map<juce::String, MappedWav*> wavCache;
    std::unordered_map<juce::String, int> groupIds;

    // --- instruments ------------------------------------------------------------------
    for (size_t ri = 0; ri < refs.size(); ++ri)
    {
        if (! shouldContinue())
            return nullptr;

        const auto& ref = refs[ri];
        const auto instFile = kitDir.getChildFile (ref.file);
        const auto instXml  = juce::XmlDocument::parse (instFile);
        if (instXml == nullptr || ! instXml->hasTagName ("instrument"))
        {
            error = "Can't read instrument file " + ref.file;
            return nullptr;
        }

        Kit::Instrument inst;
        inst.name = ref.name;
        const auto version = instXml->getStringAttribute ("version", "1.0");
        inst.powerBased = ! (version == "1" || version == "1.0" || version == "1.0.0");

        if (ref.group.isNotEmpty())
        {
            auto it = groupIds.find (ref.group);
            if (it == groupIds.end())
            {
                it = groupIds.emplace (ref.group, (int) kit->groupNames.size()).first;
                kit->groupNames.push_back (ref.group);
            }
            inst.group = it->second;
        }

        // DrumGizmo rule: if any channelmap entry sets "main", unset ones mean "not main"
        bool anyMainSet = false;
        for (const auto& [in, cm] : ref.channelMap)
            anyMainSet |= cm.main != MainState::unset;

        const auto instDir = instFile.getParentDirectory();

        if (auto* samples = instXml->getChildByName ("samples"))
            for (auto* s : samples->getChildWithTagNameIterator ("sample"))
            {
                Kit::Sample sample;
                sample.name       = s->getStringAttribute ("name");
                sample.power      = inst.powerBased ? (float) s->getDoubleAttribute ("power", 0.0) : 0.0f;
                sample.normalized = s->getStringAttribute ("normalized") == "true";

                for (auto* af : s->getChildWithTagNameIterator ("audiofile"))
                {
                    const auto instChannel = af->getStringAttribute ("channel");
                    const auto path        = instDir.getChildFile (af->getStringAttribute ("file"));
                    const auto fileChannel = af->getIntAttribute ("filechannel", 1) - 1;   // xml is 1-based

                    MappedWav* wav = nullptr;
                    const auto key = path.getFullPathName();
                    if (auto it = wavCache.find (key); it != wavCache.end())
                        wav = it->second;
                    else
                    {
                        auto w = std::make_unique<MappedWav>();
                        if (! w->open (path, error))
                            return nullptr;
                        wav = w.get();
                        wavCache[key] = wav;
                        kit->totalBytes += w->mappedBytes();
                        kit->files.push_back (std::move (w));
                    }

                    if (fileChannel < 0 || fileChannel >= wav->numChannels)
                        continue;

                    Kit::Stream stream;
                    stream.wav = wav;
                    stream.fileChannel = fileChannel;

                    auto outName = instChannel;
                    auto main = MainState::unset;
                    if (auto it = ref.channelMap.find (instChannel); it != ref.channelMap.end())
                    {
                        outName = it->second.out;
                        main = it->second.main;
                    }
                    if (main == MainState::unset && anyMainSet)
                        main = MainState::notMain;

                    stream.main = main != MainState::notMain;
                    kit->hasBleedControl |= main == MainState::isMain;

                    if (auto it = channelIndex.find (outName); it != channelIndex.end())
                        stream.kitChannel = it->second;

                    sample.length = juce::jmax (sample.length, wav->numFrames);
                    sample.streams.push_back (stream);
                }

                inst.samples.push_back (std::move (sample));
            }

        if (inst.powerBased)
        {
            std::stable_sort (inst.samples.begin(), inst.samples.end(),
                              [] (const auto& a, const auto& b) { return a.power < b.power; });
            if (! inst.samples.empty())
            {
                inst.minPower = inst.samples.front().power;
                inst.maxPower = inst.samples.back().power;
            }
        }
        else if (auto* velocities = instXml->getChildByName ("velocities"))
        {
            for (auto* v : velocities->getChildWithTagNameIterator ("velocity"))
                for (auto* sr : v->getChildWithTagNameIterator ("sampleref"))
                    for (int i = 0; i < (int) inst.samples.size(); ++i)
                        if (inst.samples[(size_t) i].name == sr->getStringAttribute ("name"))
                        {
                            inst.ranges.push_back ({ (float) v->getDoubleAttribute ("lower"),
                                                     (float) v->getDoubleAttribute ("upper"), i });
                            break;
                        }
        }

        inst.lastPlayed.assign (inst.samples.size(), 0);
        kit->instruments.push_back (std::move (inst));
        progress = 0.4f * (float) (ri + 1) / (float) refs.size();
    }

    // directed chokes, now that every instrument has an index
    for (size_t ri = 0; ri < refs.size(); ++ri)
        for (const auto& [target, ms] : refs[ri].chokes)
            for (int i = 0; i < (int) kit->instruments.size(); ++i)
                if (kit->instruments[(size_t) i].name == target)
                    kit->instruments[ri].chokes.push_back ({ i, ms });

    // --- midimap -----------------------------------------------------------------------
    if (! midimapFile.existsAsFile())
    {
        midimapFile = juce::File();
        if (defaultMidimap.isNotEmpty() && kitDir.getChildFile (defaultMidimap).existsAsFile())
            midimapFile = kitDir.getChildFile (defaultMidimap);
        else
        {
            auto maps = kitfiles::findMidimaps (kitDir);
            // Kits ship variants like DRSKit_full.xml + Midimap_full.xml: match the suffix.
            const auto variant = kitFile.getFileNameWithoutExtension().fromLastOccurrenceOf ("_", false, false);
            if (variant.isNotEmpty() && variant != kitFile.getFileNameWithoutExtension())
                for (const auto& m : maps)
                    if (m.getFileNameWithoutExtension().endsWithIgnoreCase ("_" + variant))
                    {
                        midimapFile = m;
                        break;
                    }
            if (midimapFile == juce::File())
                for (const auto& m : maps)
                    if (m.getFileName().containsIgnoreCase ("midimap"))
                    {
                        midimapFile = m;
                        break;
                    }
            if (midimapFile == juce::File() && ! maps.isEmpty())
                midimapFile = maps.getFirst();
        }
    }

    kit->midimapFile = midimapFile;
    if (midimapFile.existsAsFile())
        if (auto mm = juce::XmlDocument::parse (midimapFile); mm != nullptr && mm->hasTagName ("midimap"))
            for (auto* m : mm->getChildWithTagNameIterator ("map"))
            {
                const auto note = m->getIntAttribute ("note", -1);
                const auto instrName = m->getStringAttribute ("instr");
                if (! juce::isPositiveAndBelow (note, 128))
                    continue;
                for (int i = 0; i < (int) kit->instruments.size(); ++i)
                    if (kit->instruments[(size_t) i].name == instrName)
                    {
                        kit->noteToInstruments[(size_t) note].push_back (i);
                        kit->midimap.push_back ({ note, i });
                    }
            }

    // Many kits omit "samplerate"; trust the audio files over the 44.1k default.
    if (! kitDeclaresRate && ! kit->files.empty())
        kit->sampleRate = kit->files.front()->sampleRate;

    // --- warm the page cache: whole files async, attacks synchronously ----------
    for (size_t i = 0; i < kit->files.size(); ++i)
    {
        if (! shouldContinue())
            return nullptr;
        kit->files[i]->adviseWillNeed();
        kit->files[i]->touchHead (headBytes);
        progress = 0.4f + 0.6f * (float) (i + 1) / (float) kit->files.size();
    }

    for (int i = 0; i < Kit::maxChannels; ++i)
    {
        kit->gainL[(size_t) i] = 1.0f;
        kit->gainR[(size_t) i] = 1.0f;
        kit->gainDirect[(size_t) i] = 1.0f;
        kit->meter[(size_t) i] = 0.0f;
    }
    for (size_t i = 0; i < kit->instruments.size(); ++i)
    {
        kit->hit.push_back (std::make_unique<std::atomic<float>> (0.0f));
        kit->instrumentGain.push_back (std::make_unique<std::atomic<float>> (1.0f));
        kit->instrumentBleed.push_back (std::make_unique<std::atomic<float>> (1.0f));
        kit->instrumentTune.push_back (std::make_unique<std::atomic<float>> (0.0f));
        kit->instrumentMeter.push_back (std::make_unique<std::atomic<float>> (0.0f));
    }

    progress = 1.0f;
    return kit;
}
