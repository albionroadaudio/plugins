// Dev tool: renders the Reference editor to a PNG with a learned mix and a reference.
//   AlbionReferenceSnapshot <out.png> [width height]
#include "Common.h"

int main (int argc, char** argv)
{
    if (argc < 2) { std::puts ("usage: AlbionReferenceSnapshot <out.png> [w h]"); return 1; }
    juce::ScopedJuceInitialiser_GUI gui;
    auto proc = std::make_unique<AlbionReferenceAudioProcessor>();
    proc->prepareToPlay (48000.0, 4096);

    Noise mix; mix.pinkish = true;
    proc->startLearn();
    feed (*proc, mix, 5.0, 0.25f);
    proc->stopLearn();
    proc->setReference ("Generic balanced (-4.5 dB/oct)", profiles::builtIns()[2].curve);
    feed (*proc, mix, 2.0, 0.25f);

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc->createEditor());
    if (argc >= 4) editor->setSize (std::atoi (argv[2]), std::atoi (argv[3]));
    feed (*proc, mix, 1.0, 0.25f);
    pumpMessages (300);

    // ALBION_SWITCH=1: click the theme toggle on the open editor (tests in-place switching)
    if (std::getenv ("ALBION_SWITCH") != nullptr)
        for (auto* c : editor->getChildren())
            if (auto* t = dynamic_cast<albion::ThemeToggle*> (c))
            {
                t->triggerClick();
                pumpMessages (300);
            }

    const auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
    const juce::File out { juce::String (argv[1]) };
    out.deleteFile();
    juce::FileOutputStream os (out);
    juce::PNGImageFormat().writeImageToStream (img, os);
    std::printf ("wrote %s (%dx%d)\n", out.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    editor.reset();
    proc->releaseResources();
    return 0;
}
