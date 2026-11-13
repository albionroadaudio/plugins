#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>

// Analog-style gain reduction meter. Like a VU movement used in GR mode,
// deflection is linear in gain: 0 dB GR rests at the right end, -6 dB sits
// at half scale, -20 dB at 10%.
class GrMeterComponent final : public juce::Component
{
public:
    GrMeterComponent();

    // Positive dB of gain reduction, already ballistically smoothed.
    void setGainReduction (float grDb);

    void paint (juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override { renderBackground(); repaint(); }   // theme switch

private:
    struct Geometry
    {
        juce::Rectangle<float> face;
        juce::Point<float> pivot;
        float radius = 0.0f;
    };

    static float grToAngle (float grDb) noexcept;
    Geometry geometry() const;
    void renderBackground();

    juce::Image background;
    float shownGr = 0.0f;
};
