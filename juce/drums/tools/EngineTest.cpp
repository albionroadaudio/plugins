// Dev tool: checks the per-drum Bleed and Tune controls (DRUMS mixer), the Humanize
// control, per-kit mixer defaults and typed dB entry against a real kit.
//   AlbionDrumsEngineTest <drumkit.xml>
// The Bleed/Tune cases use a fresh processor so sample selection is identical,
// play one snare hit (note 38) and measure the stereo mix.
#include "../src/PluginProcessor.h"
#include "../src/KitViews.h"
#include <cstdlib>
#include <juce_gui_basics/juce_gui_basics.h>
#include <thread>

namespace juce::detail { bool dispatchNextMessageOnSystemQueue (bool returnIfNoPendingMessages); }

static void pumpMessages (int ms)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
    while (juce::Time::getMillisecondCounter() < end)
    {
        while (juce::detail::dispatchNextMessageOnSystemQueue (true)) {}
        std::this_thread::sleep_for (std::chrono::milliseconds (5));
    }
}

struct Result { double energy = 0; double decaySeconds = 0; };   // decay = last non-silent output sample

// settings: (note whose drum gets the property, property, value)
static Result run (const juce::File& kitFile, std::vector<std::tuple<int, juce::String, double>> settings)
{
    std::unique_ptr<juce::AudioProcessor> proc (createPluginFilter());
    auto& drums = dynamic_cast<AlbionDrumsAudioProcessor&> (*proc);
    constexpr double rate = 48000.0;
    constexpr int block = 256;
    proc->prepareToPlay (rate, block);
    drums.loadKit (kitFile);
    for (int i = 0; i < 600 && drums.getPublishedKit() == nullptr && drums.getLastError().isEmpty(); ++i)
        pumpMessages (50);
    const auto* kit = drums.getPublishedKit();
    if (kit == nullptr)
    {
        std::printf ("kit failed to load: %s\n", drums.getLastError().toRawUTF8());
        std::exit (2);
    }
    for (const auto& [note, prop, value] : settings)
        for (auto inst : kit->noteToInstruments[(size_t) note])
            drums.getInstrumentState (kit->instruments[(size_t) inst].name).setProperty (prop, value, nullptr);
    pumpMessages (20);

    juce::AudioBuffer<float> buf (proc->getTotalNumOutputChannels(), block);
    Result r;
    std::int64_t lastSound = 0, t = 0;
    for (int b = 0; b < (int) (8.0 * rate) / block; ++b)
    {
        buf.clear();
        juce::MidiBuffer midi;
        if (b == 0)
            midi.addEvent (juce::MidiMessage::noteOn (10, 38, (juce::uint8) 110), 0);
        proc->processBlock (buf, midi);
        double sum = 0;
        for (int ch = 0; ch < 2; ++ch)
            for (int k = 0; k < block; ++k)
                sum += (double) buf.getSample (ch, k) * buf.getSample (ch, k);
        r.energy += sum;
        for (int k = 0; k < block; ++k, ++t)
            if (std::abs (buf.getSample (0, k)) > 1.0e-6f || std::abs (buf.getSample (1, k)) > 1.0e-6f)
                lastSound = t;
    }
    // the sample's end lands at length / playback speed, so this scales exactly with Tune
    r.decaySeconds = (double) lastSound / rate;
    proc->releaseResources();
    return r;
}

static void setParam (juce::AudioProcessor& proc, const juce::String& id, float plainValue)
{
    for (auto* p : proc.getParameters())
        if (auto* withId = dynamic_cast<juce::RangedAudioParameter*> (p); withId != nullptr && withId->getParameterID() == id)
            withId->setValueNotifyingHost (withId->convertTo0to1 (plainValue));
}

// Humanize regression: run for a few minutes (the plugin's clock keeps going even
// while the transport is stopped), then prepareToPlay again, as a host does before a
// bounce, and keep playing. Returns the mean per-hit energy of the first few seconds
// after the re-prepare divided by that of the last few seconds.
static double humanizeAfterReprepare (const juce::File& kitFile)
{
    std::unique_ptr<juce::AudioProcessor> proc (createPluginFilter());
    auto& drums = dynamic_cast<AlbionDrumsAudioProcessor&> (*proc);
    constexpr double rate = 48000.0;
    constexpr int block = 256;
    constexpr int hitEvery = (int) (0.5 * rate) / block;    // a snare hit every ~0.5 s
    proc->prepareToPlay (rate, block);
    drums.loadKit (kitFile);
    for (int i = 0; i < 600 && drums.getPublishedKit() == nullptr && drums.getLastError().isEmpty(); ++i)
        pumpMessages (50);
    if (drums.getPublishedKit() == nullptr)
        std::exit (2);
    setParam (*proc, "humanize", 50.0f);
    setParam (*proc, "dynamics", 50.0f);

    juce::AudioBuffer<float> buf (proc->getTotalNumOutputChannels(), block);
    std::vector<double> hits;
    auto play = [&] (double seconds, bool record)
    {
        for (int b = 0; b < (int) (seconds * rate) / block; ++b)
        {
            buf.clear();
            juce::MidiBuffer midi;
            if (b % hitEvery == 0)
            {
                midi.addEvent (juce::MidiMessage::noteOn (10, 38, (juce::uint8) 100), 0);
                if (record)
                    hits.push_back (0.0);
            }
            proc->processBlock (buf, midi);
            if (record && ! hits.empty())
                for (int ch = 0; ch < 2; ++ch)
                    for (int k = 0; k < block; ++k)
                        hits.back() += (double) buf.getSample (ch, k) * buf.getSample (ch, k);
        }
    };

    play (300.0, false);                // a session in progress
    proc->releaseResources();
    proc->prepareToPlay (rate, block);  // the host prepares again for the bounce
    play (40.0, true);
    proc->releaseResources();

    std::printf ("humanize per-hit energy after re-prepare:");
    for (size_t i = 0; i < juce::jmin<size_t> (24, hits.size()); ++i)
        std::printf (" %.2f", hits[i]);
    std::printf (" ... %.2f\n", hits.back());
    const auto n = juce::jmin<size_t> (16, hits.size() / 3);   // ~8 s at each end
    double first = 0, last = 0;
    for (size_t i = 0; i < n; ++i)
    {
        first += hits[i];
        last  += hits[hits.size() - 1 - i];
    }
    return first / juce::jmax (1.0e-12, last);
}

static std::unique_ptr<juce::AudioProcessor> loadedProcessor (const juce::File& kitFile)
{
    std::unique_ptr<juce::AudioProcessor> proc (createPluginFilter());
    auto& drums = dynamic_cast<AlbionDrumsAudioProcessor&> (*proc);
    proc->prepareToPlay (48000.0, 256);
    drums.loadKit (kitFile);
    for (int i = 0; i < 600 && drums.getPublishedKit() == nullptr && drums.getLastError().isEmpty(); ++i)
        pumpMessages (50);
    if (drums.getPublishedKit() == nullptr)
        std::exit (2);
    return proc;
}

static bool same (double a, double b) { return std::abs (a - b) < 1.0e-9; }

static double levelOf (juce::AudioProcessor& proc, const juce::String& instrument)
{
    return (double) dynamic_cast<AlbionDrumsAudioProcessor&> (proc).getInstrumentState (instrument).getProperty ("level", 0.0);
}

int main (int argc, char** argv)
{
    if (argc < 2) { std::puts ("usage: AlbionDrumsEngineTest <drumkit.xml>"); return 1; }
    juce::ScopedJuceInitialiser_GUI gui;
    // keep the user's real kit defaults out of (and safe from) the tests
    const auto defaultsFile = juce::File::createTempFile ("albion-drums-kit-defaults.xml");
    setenv ("ALBION_DRUMS_KIT_DEFAULTS", defaultsFile.getFullPathName().toRawUTF8(), 1);
    const juce::File kit { juce::String (argv[1]) };
    int failures = 0;
    auto check = [&] (bool ok, const char* what) { std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what); failures += ok ? 0 : 1; };
    auto db = [] (double ratio) { return 10.0 * std::log10 (juce::jmax (1.0e-12, ratio)); };

    const auto base      = run (kit, {});
    const auto noBleed   = run (kit, { { 38, "bleed", 0.0 } });
    const auto halfBleed = run (kit, { { 38, "bleed", 50.0 } });
    const auto kickBleed = run (kit, { { 36, "bleed", 0.0 } });
    const auto up12      = run (kit, { { 38, "tune", 12.0 } });
    const auto down5     = run (kit, { { 38, "tune", -5.0 } });
    const auto kickTune  = run (kit, { { 36, "tune", 12.0 } });

    std::printf ("snare energy: default %.3f, bleed 0%% %+.1f dB, bleed 50%% %+.1f dB, kick bleed 0%% %+.2f dB\n",
                 base.energy, db (noBleed.energy / base.energy), db (halfBleed.energy / base.energy),
                 db (kickBleed.energy / base.energy));
    std::printf ("snare decay: default %.2f s, tune +12 %.2f s, tune -5 %.2f s, kick tune +12 %.2f s\n",
                 base.decaySeconds, up12.decaySeconds, down5.decaySeconds, kickTune.decaySeconds);

    check (base.energy > 0.0, "snare hit produces sound");
    check (noBleed.energy < base.energy * 0.9 && noBleed.energy > 0.0, "snare Bleed 0% removes its bleed mics but keeps the close mic");
    check (halfBleed.energy < base.energy && halfBleed.energy > noBleed.energy, "snare Bleed 50% sits between 0% and 100%");
    check (std::abs (kickBleed.energy - base.energy) < base.energy * 1.0e-6, "kick Bleed doesn't affect the snare");
    check (std::abs (up12.decaySeconds / base.decaySeconds - 0.5) < 0.02, "snare Tune +12 st plays twice as fast");
    check (std::abs (down5.decaySeconds / base.decaySeconds - std::exp2 (5.0 / 12.0)) < 0.02, "snare Tune -5 st plays 1.33x slower");
    check (std::abs (kickTune.energy - base.energy) < base.energy * 1.0e-6, "kick Tune doesn't affect the snare");
    const auto humanizeRatio = humanizeAfterReprepare (kit);
    std::printf ("humanize 50%%, after re-prepare: first 8 s vs last 8 s %+.1f dB\n", db (humanizeRatio));
    check (std::abs (db (humanizeRatio)) < 3.0, "Humanize doesn't make the start of a bounce quiet");

    // --- per-kit mixer defaults ------------------------------------------------------
    {
        auto a = loadedProcessor (kit);
        auto& drumsA = dynamic_cast<AlbionDrumsAudioProcessor&> (*a);
        check (! drumsA.hasKitDefaults(), "no kit defaults before saving");
        drumsA.getInstrumentState ("Hihat_semi_open").setProperty ("level", -3.0, nullptr);
        drumsA.getInstrumentState ("Crash_left_tip").setProperty ("level", 6.0, nullptr);
        drumsA.getInstrumentState ("Snare").setProperty ("tune", 2.0, nullptr);
        check (drumsA.saveKitDefaults() && drumsA.hasKitDefaults(), "kit defaults save");

        auto b = loadedProcessor (kit);   // a new instance loading the same kit
        auto& drumsB = dynamic_cast<AlbionDrumsAudioProcessor&> (*b);
        check (same (levelOf (*b, "Hihat_semi_open"), -3.0) && same (levelOf (*b, "Crash_left_tip"), 6.0)
                   && same ((double) drumsB.getInstrumentState ("Snare").getProperty ("tune"), 2.0)
                   && same (levelOf (*b, "Ride_shank_bell"), 0.0),
               "a new instance loading the kit gets its saved defaults");

        // a saved project keeps its own mix, even though the kit has defaults
        drumsA.getInstrumentState ("Hihat_semi_open").setProperty ("level", -10.0, nullptr);
        juce::MemoryBlock project;
        a->getStateInformation (project);
        std::unique_ptr<juce::AudioProcessor> c (createPluginFilter());
        auto& drumsC = dynamic_cast<AlbionDrumsAudioProcessor&> (*c);
        c->prepareToPlay (48000.0, 256);
        c->setStateInformation (project.getData(), (int) project.getSize());
        for (int i = 0; i < 600 && drumsC.getPublishedKit() == nullptr && drumsC.getLastError().isEmpty(); ++i)
            pumpMessages (50);
        check (drumsC.getPublishedKit() != nullptr && same (levelOf (*c, "Hihat_semi_open"), -10.0),
               "reopening a project keeps the project's mix, not the kit defaults");

        drumsC.getInstrumentState ("Crash_left_tip").setProperty ("level", 0.0, nullptr);
        check (drumsC.recallKitDefaults() && same (levelOf (*c, "Crash_left_tip"), 6.0) && same (levelOf (*c, "Hihat_semi_open"), -3.0),
               "Recall puts the kit defaults back");
    }
    defaultsFile.deleteFile();

    // --- typed dB values in the fader readout -----------------------------------------
    auto parsesTo = [] (const char* text, std::optional<double> want)
    {
        const auto got = MixerStrip::parseDb (text);
        return got.has_value() == want.has_value() && (! got || same (*got, *want));
    };
    check (parsesTo ("-3", -3.0) && parsesTo ("+6", 6.0) && parsesTo (" -2.5 dB", -2.5) && parsesTo ("4db", 4.0)
               && parsesTo ("-inf", -60.0) && parsesTo ("0", 0.0),
           "fader readout accepts -3, +6, -2.5 dB, 4db, -inf, 0");
    check (parsesTo ("abc", std::nullopt) && parsesTo ("", std::nullopt) && parsesTo ("-", std::nullopt),
           "fader readout ignores text that isn't a number");

    return failures == 0 ? 0 : 1;
}
