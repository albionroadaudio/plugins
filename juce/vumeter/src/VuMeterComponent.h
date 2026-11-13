#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>

// One analog-style VU meter face. The scale is linear in voltage like a real
// moving-coil meter: 0 VU sits at 100%, +3 VU (141%) at the right end.
class VuMeterComponent final : public juce::Component
{
public:
    explicit VuMeterComponent (juce::String channelLabel);

    // ratio: linear level relative to the 0 VU reference (1.0 == 0 VU).
    // Returns true if the component needed a repaint.
    bool setState (float ratio, bool peakLit);
    void setReferenceText (juce::String text);

    // Highest needle reading since reset, in VU dB; shown top-right of the face.
    void setMaxReading (bool valid, float vu);

    void paint (juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override { renderBackground(); repaint(); }   // theme switch

    std::function<void()> onClick;
    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }

private:
    struct Geometry
    {
        juce::Rectangle<float> face;
        juce::Point<float> pivot;
        float radius = 0.0f;
    };

    static float ratioToFraction (float ratio) noexcept;   // 0..1 across the scale
    static float fractionToAngle (float fraction) noexcept; // radians, 0 = straight up
    Geometry geometry() const;
    void renderBackground();

    juce::String label;
    juce::String referenceText;
    juce::Image background;   // static face, re-rendered on resize / text change

    float needleFraction = 0.0f;
    bool  ledLit = false;
    bool  maxValid = false;
    float maxVu = 0.0f;
    juce::Rectangle<float> maxBox() const;
};
