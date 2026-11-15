// Dev tool: renders the plugin editor to a PNG (no host, no window).
//   <Plugin>Snapshot <out.png>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <thread>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();
namespace juce::detail { bool dispatchNextMessageOnSystemQueue (bool returnIfNoPendingMessages); }

int main (int argc, char** argv)
{
    if (argc < 2) { std::puts ("usage: Snapshot <out.png> [paramId,paramId... to switch off]"); return 1; }
    juce::ScopedJuceInitialiser_GUI gui;
    std::unique_ptr<juce::AudioProcessor> proc (createPluginFilter());
    proc->prepareToPlay (48000.0, 256);

    // a loud "syllable" so the meters have something to show
    juce::AudioBuffer<float> buf (2, 256);
    juce::MidiBuffer midi;
    for (int b = 0; b < 200; ++b)
    {
        const auto level = b < 150 ? 0.05f : 0.5f;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 256; ++i)
            {
                const auto t = (float) (b * 256 + i) / 48000.0f;
                buf.setSample (ch, i, level * (std::sin (1382.3f * t) + 0.5f * std::sin (2764.6f * t) + 0.3f * std::sin (40000.0f * t)));
            }
        proc->processBlock (buf, midi);
    }

    // optional 2nd argument: parameter IDs to switch off, e.g. "comp1On,eqOn"
    if (argc > 2)
        for (auto& id : juce::StringArray::fromTokens (argv[2], ",", ""))
            for (auto* q : proc->getParameters())
                if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (q); r != nullptr && r->getParameterID() == id)
                    r->setValueNotifyingHost (0.0f);

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
