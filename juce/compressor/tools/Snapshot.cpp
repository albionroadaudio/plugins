// Dev tool: renders the plugin editor to a PNG (no host, no window).
//   <Plugin>Snapshot <out.png>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <thread>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();
namespace juce::detail { bool dispatchNextMessageOnSystemQueue (bool returnIfNoPendingMessages); }

int main (int argc, char** argv)
{
    if (argc < 2) { std::puts ("usage: Snapshot <out.png>"); return 1; }
    juce::ScopedJuceInitialiser_GUI gui;
    std::unique_ptr<juce::AudioProcessor> proc (createPluginFilter());
    proc->prepareToPlay (48000.0, 256);

    // a few blocks of a loud tone so meters have something to show
    juce::AudioBuffer<float> buf (2, 256);
    juce::MidiBuffer midi;
    for (int b = 0; b < 200; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 256; ++i)
                buf.setSample (ch, i, 0.7f * std::sin (0.13f * (float) (b * 256 + i)));
        proc->processBlock (buf, midi);
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc->createEditor());
    const auto end = juce::Time::getMillisecondCounter() + 250;
    while (juce::Time::getMillisecondCounter() < end)
    {
        while (juce::detail::dispatchNextMessageOnSystemQueue (true)) {}
        std::this_thread::sleep_for (std::chrono::milliseconds (5));
    }

    const auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
    const juce::File out { juce::String (argv[1]) };
    out.deleteFile();
    juce::FileOutputStream os (out);
    juce::PNGImageFormat().writeImageToStream (img, os);
    std::printf ("wrote %s (%dx%d)\n", out.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    editor.reset();
    return 0;
}
