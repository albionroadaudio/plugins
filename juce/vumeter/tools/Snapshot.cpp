// Dev tool: renders the VU Meter editor to a PNG after feeding it test tones.
//   VuMeterSnapshot <out.png> [levelL_dBFS levelR_dBFS]
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
    if (argc < 2) { std::puts ("usage: VuMeterSnapshot <out.png> [levelL levelR]"); return 1; }
    const float lvl[2] = { argc > 3 ? (float) std::atof (argv[2]) : -21.0f, argc > 3 ? (float) std::atof (argv[3]) : -18.0f };

    juce::ScopedJuceInitialiser_GUI gui;
    std::unique_ptr<juce::AudioProcessor> proc (createPluginFilter());
    const double sr = 48000.0;
    proc->prepareToPlay (sr, 256);

    // 1 kHz sines with RMS at the requested dBFS (peak = RMS * sqrt 2), 1.5 s
    juce::AudioBuffer<float> buf (2, 256);
    juce::MidiBuffer midi;
    std::int64_t t = 0;
    for (int b = 0; b < (int) (1.5 * sr / 256); ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto a = juce::Decibels::decibelsToGain (lvl[ch]) * juce::MathConstants<float>::sqrt2;
            for (int i = 0; i < 256; ++i)
                buf.setSample (ch, i, a * std::sin (juce::MathConstants<float>::twoPi * 1000.0f * (float) (t + i) / (float) sr));
        }
        t += 256;
        proc->processBlock (buf, midi);
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc->createEditor());
    pumpMessages (200);

    const auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
    const juce::File out { juce::String (argv[1]) };
    out.deleteFile();
    juce::FileOutputStream os (out);
    juce::PNGImageFormat().writeImageToStream (img, os);
    std::printf ("wrote %s (%dx%d)\n", out.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    editor.reset();
    return 0;
}
