#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Albion Road Audio house style, matched to albionroadaudio.co.uk, in a light
// ("paper") and a dark ("dark sections") theme. Dark is the default.
//
// The colours below are *mutable*: setDarkTheme() swaps the whole palette at
// runtime, and every paint() reads them fresh.
namespace albion::colours
{
    // --- palette -------------------------------------------------------------------------
    inline juce::Colour paper      { 0xfff3f0e7 };   // window background (--paper)
    inline juce::Colour paperLight { 0xfff8f6f0 };   // cards, top of gradients
    inline juce::Colour paperDeep  { 0xffe8e2d5 };   // insets, slots (--paper-deep)
    inline juce::Colour line       { 0xffd8d1c3 };   // hairlines (--line)
    inline juce::Colour lineDark   { 0xffcec7b9 };   // outlines
    inline juce::Colour muted      { 0xff6b695f };   // secondary text (--muted)
    inline juce::Colour ink        { 0xff24241f };   // text, active buttons (--ink)
    inline juce::Colour dark       { 0xff252721 };   // meter bezels (--dark)
    inline juce::Colour accent     { 0xffb85f3e };   // highlights (--accent)
    inline juce::Colour accentDark { 0xff91452f };   // captions (--accent-dark)
    inline juce::Colour peach      { 0xffe3a080 };   // light orange on dark
    inline juce::Colour olive      { 0xff73765d };   // --olive
    inline juce::Colour knob       { 0xff24241f };   // knob / fader caps
    inline juce::Colour pointer    { 0xfff3f0e7 };   // knob pointer, fader cap line
    inline juce::Colour logo       { 0xff24241f };   // logo ring and bars

    // --- role names used throughout the editors ------------------------------------
    inline juce::Colour panel      = paper;        // window background
    inline juce::Colour panelLight = paperLight;   // top of the background gradient
    inline juce::Colour panelDark  = paperDeep;    // insets, slots, unselected buttons
    inline juce::Colour brass      = accent;       // captions, value arcs, highlights
    inline juce::Colour brassDark  = lineDark;     // outlines
    inline juce::Colour cream      = ink;          // main text / active controls
    inline juce::Colour red        = accent;       // meter "hot" zones
}

class AlbionLookAndFeel;

namespace albion
{
    juce::Font sans (float height, bool bold = false);
    juce::Font mono (float height);

    // The Albion Road Audio mark (ring + seven bars), drawn into `area`.
    void drawLogo (juce::Graphics&, juce::Rectangle<float> area);

    // Shared plugin header: logo, "ALBION ROAD AUDIO" wordmark and the product
    // name in small spaced mono underneath, with a hairline below.
    void drawHeader (juce::Graphics&, juce::Rectangle<float> area, const juce::String& product, float scale);

    // --- light / dark theme -------------------------------------------------------------
    // The choice is shared by every Albion Road Audio plugin through
    // ~/.config/Albion Road Audio/settings.xml (default: dark).
    bool isDarkTheme();
    void setDarkTheme (bool dark, bool savePreference);

    // Flip the theme and re-colour an open editor in place.
    void switchTheme (juce::Component& editor, AlbionLookAndFeel& lnf);

    // Small half-moon toggle for the header. Set onClick to call switchTheme().
    class ThemeToggle final : public juce::Button
    {
    public:
        ThemeToggle();
        void paintButton (juce::Graphics&, bool isMouseOver, bool isButtonDown) override;
    };
}

class AlbionLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    AlbionLookAndFeel();

    // Re-applies the current palette to the look-and-feel colour IDs.
    void applyTheme();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool isMouseOverButton, bool isButtonDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool isMouseOverButton, bool isButtonDown) override;

    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;

    float uiScale = 1.0f;   // set by the editor on resize
};
