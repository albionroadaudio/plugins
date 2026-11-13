// Dev tool: checks the per-drum Bleed and Tune controls (DRUMS mixer) against a real kit.
//   AlbionDrumsEngineTest <drumkit.xml>
// Each case uses a fresh processor so sample selection is identical, plays one
// snare hit (note 38) and measures the stereo mix.
#include "../src/PluginProcessor.h"
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

int main (int argc, char** argv)
{
    if (argc < 2) { std::puts ("usage: AlbionDrumsEngineTest <drumkit.xml>"); return 1; }
    juce::ScopedJuceInitialiser_GUI gui;
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
    return failures == 0 ? 0 : 1;
}
