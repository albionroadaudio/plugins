// Dev tool: renders the editor to a PNG with a kit loaded (no host, no window).
//   AlbionDrumsSnapshot <drumkit.xml> <out.png> [width height [drums]]
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

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::puts ("usage: AlbionDrumsSnapshot <drumkit.xml> <out.png> [width height]");
        return 1;
    }
    juce::ScopedJuceInitialiser_GUI gui;
    std::unique_ptr<juce::AudioProcessor> proc (createPluginFilter());
    auto& drums = dynamic_cast<AlbionDrumsAudioProcessor&> (*proc);
    proc->prepareToPlay (48000.0, 256);
    drums.loadKit (juce::File (juce::String (argv[1])));

    for (int i = 0; i < 600 && drums.getPublishedKit() == nullptr && drums.getLastError().isEmpty(); ++i)
        pumpMessages (50);
    if (drums.getPublishedKit() == nullptr)
    {
        std::printf ("kit failed to load: %s\n", drums.getLastError().toRawUTF8());
        return 1;
    }

    if (argc >= 6 && juce::String (argv[5]) == "drums")
        drums.getUiState().setProperty ("drumsView", true, nullptr);

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc->createEditor());
    if (argc >= 5)
        editor->setSize (std::atoi (argv[3]), std::atoi (argv[4]));

    // light a few pads + meters so the snapshot shows hit states
    juce::AudioBuffer<float> buf (proc->getTotalNumOutputChannels(), 256);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (10, 36, (juce::uint8) 120), 0);
    midi.addEvent (juce::MidiMessage::noteOn (10, 38, (juce::uint8) 110), 0);
    proc->processBlock (buf, midi);
    for (int i = 0; i < 6; ++i) { juce::MidiBuffer none; proc->processBlock (buf, none); }
    pumpMessages (150);

    const auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
    const juce::File out { juce::String (argv[2]) };
    out.deleteFile();
    juce::FileOutputStream os (out);
    juce::PNGImageFormat().writeImageToStream (img, os);
    std::printf ("wrote %s (%dx%d)\n", out.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    editor.reset();
    proc->releaseResources();
    return 0;
}
