#include "AlbionLookAndFeel.h"
#include <array>
#include <cstdlib>

using namespace albion;

//==============================================================================
juce::Font albion::sans (float height, bool bold)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

juce::Font albion::mono (float height)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), height, juce::Font::plain));
}

void albion::drawLogo (juce::Graphics& g, juce::Rectangle<float> area)
{
    // Geometry from the site's svg/logo.svg (48 x 48 viewBox).
    const auto size = juce::jmin (area.getWidth(), area.getHeight());
    const auto k = size / 48.0f;
    const auto o = area.withSizeKeepingCentre (size, size).getTopLeft();
    auto pt = [&] (float x, float y) { return juce::Point<float> (o.x + x * k, o.y + y * k); };

    g.setColour (colours::logo);
    g.drawEllipse (juce::Rectangle<float> (42.0f * k, 42.0f * k).withCentre (pt (24, 24)), 1.5f * k);
    g.setColour (colours::accent.withAlpha (0.55f));
    g.drawEllipse (juce::Rectangle<float> (33.0f * k, 33.0f * k).withCentre (pt (24, 24)), 1.0f * k);

    struct Bar { float x, y, h; };
    static constexpr Bar bars[] = { { 11, 21, 6 }, { 15, 17, 14 }, { 19, 13, 22 }, { 27, 13, 22 }, { 31, 17, 14 }, { 35, 21, 6 } };
    g.setColour (colours::logo);
    for (const auto& b : bars)
        g.fillRoundedRectangle (juce::Rectangle<float> (pt (b.x, b.y), pt (b.x + 2, b.y + b.h)), 1.0f * k);
    g.setColour (colours::accent);
    g.fillRoundedRectangle (juce::Rectangle<float> (pt (23, 16), pt (25, 32)), 1.0f * k);
}

void albion::drawHeader (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& product, float s)
{
    g.setColour (colours::paper);
    g.fillRect (area);
    g.setColour (colours::line);
    g.fillRect (area.removeFromBottom (juce::jmax (1.0f, s)));

    auto content = area.reduced (18.0f * s, 0.0f);
    drawLogo (g, content.removeFromLeft (38.0f * s).withSizeKeepingCentre (38.0f * s, 38.0f * s));
    content.removeFromLeft (12.0f * s);

    auto block = content.withSizeKeepingCentre (content.getWidth(), 34.0f * s);
    g.setColour (colours::ink);
    g.setFont (sans (19.0f * s, true).withExtraKerningFactor (0.075f));
    g.drawText ("ALBION ROAD AUDIO", block.removeFromTop (19.0f * s), juce::Justification::centredLeft, false);
    block.removeFromTop (5.0f * s);
    g.setColour (colours::muted);
    g.setFont (mono (10.0f * s).withExtraKerningFactor (0.16f));
    g.drawText (product.toUpperCase(), block, juce::Justification::centredLeft, false);
}

//==============================================================================
namespace
{
    struct Palette
    {
        juce::Colour paper, paperLight, paperDeep, line, lineDark, muted, ink, dark,
                     accent, accentDark, peach, olive, knob, pointer, logo;

        std::array<juce::Colour*, 15> fields() noexcept
        {
            return { &paper, &paperLight, &paperDeep, &line, &lineDark, &muted, &ink, &dark,
                     &accent, &accentDark, &peach, &olive, &knob, &pointer, &logo };
        }
    };

    Palette lightPalette()
    {
        // albionroadaudio.co.uk :root
        return { juce::Colour (0xfff3f0e7), juce::Colour (0xfff8f6f0), juce::Colour (0xffe8e2d5), juce::Colour (0xffd8d1c3),
                 juce::Colour (0xffcec7b9), juce::Colour (0xff6b695f), juce::Colour (0xff24241f), juce::Colour (0xff252721),
                 juce::Colour (0xffb85f3e), juce::Colour (0xff91452f), juce::Colour (0xffe3a080), juce::Colour (0xff73765d),
                 juce::Colour (0xff24241f), juce::Colour (0xfff3f0e7), juce::Colour (0xff24241f) };
    }

    Palette darkPalette()
    {
        // the site's dark sections: --dark background, light warm text, peach accents
        return { juce::Colour (0xff252721), juce::Colour (0xff2d2f29), juce::Colour (0xff1c1e19), juce::Colour (0xff3a3c35),
                 juce::Colour (0xff4b4d45), juce::Colour (0xffa3a196), juce::Colour (0xffece8dc), juce::Colour (0xff141511),
                 juce::Colour (0xffd98a67), juce::Colour (0xffe3a080), juce::Colour (0xffe3a080), juce::Colour (0xffa3a687),
                 juce::Colour (0xff161713), juce::Colour (0xffece8dc), juce::Colour (0xffe3a080) };
    }

    Palette current()
    {
        using namespace colours;
        return { paper, paperLight, paperDeep, line, lineDark, muted, ink, dark, accent, accentDark, peach, olive, knob, pointer, logo };
    }

    void install (const Palette& p)
    {
        using namespace colours;
        paper = p.paper; paperLight = p.paperLight; paperDeep = p.paperDeep; line = p.line; lineDark = p.lineDark;
        muted = p.muted; ink = p.ink; dark = p.dark; accent = p.accent; accentDark = p.accentDark;
        peach = p.peach; olive = p.olive; knob = p.knob; pointer = p.pointer; logo = p.logo;
        panel = paper; panelLight = paperLight; panelDark = paperDeep; brass = accent;
        brassDark = lineDark; cream = ink; red = accent;
    }

    bool themeLoaded = false, themeDark = true;

    juce::File settingsFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Albion Road Audio").getChildFile ("settings.xml");
    }

    void ensureThemeLoaded()
    {
        if (themeLoaded)
            return;
        themeLoaded = true;
        bool dark = true;   // default
        if (auto xml = juce::XmlDocument::parse (settingsFile()); xml != nullptr)
            dark = xml->getStringAttribute ("theme", "dark") != "light";
        if (const auto* env = std::getenv ("ALBION_THEME"))   // dev override for screenshots
            dark = juce::String (env) != "light";
        themeDark = dark;
        install (dark ? darkPalette() : lightPalette());
    }

    // Swap explicitly-set colours on a component tree from one palette to the
    // other (matched by RGB, alpha preserved), so editors that called setColour()
    // with palette colours follow the theme without being rebuilt.
    juce::Colour remap (juce::Colour c, Palette& from, Palette& to)
    {
        const auto rgb = c.withAlpha (1.0f);
        auto f = from.fields();
        auto t = to.fields();
        for (size_t i = 0; i < f.size(); ++i)
            if (f[i]->withAlpha (1.0f) == rgb)
                return t[i]->withAlpha (c.getFloatAlpha());
        return c;
    }

    void remapTree (juce::Component& comp, Palette& from, Palette& to)
    {
        static const int ids[] = {
            juce::Label::textColourId, juce::Label::backgroundColourId, juce::Label::outlineColourId,
            juce::Label::textWhenEditingColourId, juce::Label::backgroundWhenEditingColourId, juce::Label::outlineWhenEditingColourId,
            juce::Slider::textBoxTextColourId, juce::Slider::textBoxBackgroundColourId, juce::Slider::textBoxOutlineColourId,
            juce::Slider::textBoxHighlightColourId, juce::Slider::trackColourId, juce::Slider::backgroundColourId,
            juce::Slider::thumbColourId, juce::ComboBox::backgroundColourId, juce::ComboBox::textColourId,
            juce::ComboBox::outlineColourId, juce::ComboBox::arrowColourId, juce::TextButton::buttonColourId,
            juce::TextButton::textColourOffId, juce::TextButton::textColourOnId };
        for (auto id : ids)
            if (comp.isColourSpecified (id))
                comp.setColour (id, remap (comp.findColour (id), from, to));

        auto& props = comp.getProperties();
        if (props.contains ("onColour"))
        {
            const auto c = remap (juce::Colour ((juce::uint32) (juce::int64) props["onColour"]), from, to);
            props.set ("onColour", (juce::int64) c.getARGB());
        }

        for (auto* child : comp.getChildren())
            remapTree (*child, from, to);
    }
}

bool albion::isDarkTheme()
{
    ensureThemeLoaded();
    return themeDark;
}

void albion::setDarkTheme (bool dark, bool savePreference)
{
    ensureThemeLoaded();
    themeDark = dark;
    install (dark ? darkPalette() : lightPalette());

    if (savePreference)
    {
        const auto file = settingsFile();
        file.getParentDirectory().createDirectory();
        std::unique_ptr<juce::XmlElement> xml (juce::XmlDocument::parse (file));
        if (xml == nullptr || ! xml->hasTagName ("AlbionRoadAudioSettings"))
            xml = std::make_unique<juce::XmlElement> ("AlbionRoadAudioSettings");
        xml->setAttribute ("theme", dark ? "dark" : "light");
        xml->writeTo (file);
    }
}

void albion::switchTheme (juce::Component& editor, AlbionLookAndFeel& lnf)
{
    auto from = current();
    setDarkTheme (! isDarkTheme(), true);
    auto to = current();
    lnf.applyTheme();
    remapTree (editor, from, to);
    editor.sendLookAndFeelChange();   // recreates slider text boxes, re-renders cached meter faces
    editor.repaint();
}

albion::ThemeToggle::ThemeToggle() : juce::Button ("Theme")
{
    setTooltip ("Light / dark mode (shared by all Albion Road Audio plugins)");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void albion::ThemeToggle::paintButton (juce::Graphics& g, bool over, bool down)
{
    // half-filled circle: the filled half shows the *other* mode
    const auto size = (float) juce::jmin (getWidth(), getHeight());
    auto r = getLocalBounds().toFloat().withSizeKeepingCentre (size, size).reduced (size * 0.18f);
    const auto c = over || down ? colours::accent : colours::muted;
    g.setColour (c);
    g.drawEllipse (r, juce::jmax (1.2f, size * 0.06f));
    juce::Path half;
    half.addPieSegment (r.reduced (size * 0.09f), 0.0f, juce::MathConstants<float>::pi, 0.0f);
    g.fillPath (half);
}

//==============================================================================
AlbionLookAndFeel::AlbionLookAndFeel()
{
    ensureThemeLoaded();
    applyTheme();
}

void AlbionLookAndFeel::applyTheme()
{
    setColour (juce::ResizableWindow::backgroundColourId, colours::paper);

    setColour (juce::Slider::textBoxTextColourId,       colours::ink);
    setColour (juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId,  colours::accent.withAlpha (0.35f));
    setColour (juce::Label::textColourId,               colours::ink);
    setColour (juce::Label::textWhenEditingColourId,    colours::ink);
    setColour (juce::TextEditor::backgroundColourId,    colours::paperLight);
    setColour (juce::TextEditor::textColourId,          colours::ink);
    setColour (juce::TextEditor::highlightColourId,     colours::accent.withAlpha (0.35f));
    setColour (juce::TextEditor::outlineColourId,       colours::line);
    setColour (juce::TextEditor::focusedOutlineColourId, colours::accent);
    setColour (juce::CaretComponent::caretColourId,     colours::accent);

    setColour (juce::ComboBox::backgroundColourId, colours::paperLight);
    setColour (juce::ComboBox::textColourId,       colours::ink);
    setColour (juce::ComboBox::outlineColourId,    colours::lineDark);
    setColour (juce::ComboBox::arrowColourId,      colours::accent);

    setColour (juce::ScrollBar::thumbColourId,                 colours::lineDark);
    setColour (juce::ScrollBar::trackColourId,                 colours::paperDeep);
    setColour (juce::PopupMenu::backgroundColourId,            colours::paperLight);
    setColour (juce::PopupMenu::textColourId,                  colours::ink);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::accent);
    setColour (juce::PopupMenu::highlightedTextColourId,       colours::paper);
    setColour (juce::PopupMenu::headerTextColourId,            colours::accentDark);

    setColour (juce::BubbleComponent::backgroundColourId, colours::ink);
    setColour (juce::BubbleComponent::outlineColourId,    colours::ink);
    setColour (juce::TooltipWindow::backgroundColourId,   colours::ink);
    setColour (juce::TooltipWindow::textColourId,         colours::paper);
    setColour (juce::TooltipWindow::outlineColourId,      colours::ink);
}

void AlbionLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                          float pos, float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    const auto size   = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto centre = bounds.getCentre();
    const auto radius = size * 0.5f;
    const auto angle  = startAngle + pos * (endAngle - startAngle);

    // scale dots (a knob can opt out and draw its own labelled scale)
    g.setColour (colours::muted.withAlpha (0.6f));
    const bool customScale = slider.getProperties().getWithDefault ("customScale", false);
    for (int i = 0; i <= 10 && ! customScale; ++i)
    {
        const auto a = startAngle + (float) i / 10.0f * (endAngle - startAngle);
        const auto d = radius * (i % 5 == 0 ? 0.075f : 0.045f);
        const auto p = centre.getPointOnCircumference (radius * 0.94f, a);
        g.fillEllipse (juce::Rectangle<float> (d, d).withCentre (p));
    }

    // value arc: hairline track, burnt-orange value
    const auto arcR = radius * 0.80f;
    juce::Path track, value;
    track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, angle, true);
    const auto arcW = juce::jmax (1.5f, radius * 0.06f);
    g.setColour (colours::line);
    g.strokePath (track, juce::PathStrokeType (arcW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (colours::accent);
    g.strokePath (value, juce::PathStrokeType (arcW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // knob body: light skirt + ink cap, soft shadow
    const auto skirt = juce::Rectangle<float> (radius * 1.36f, radius * 1.36f).withCentre (centre);
    g.setColour (colours::ink.withAlpha (0.12f));
    g.fillEllipse (skirt.translated (0.0f, radius * 0.06f));
    g.setGradientFill (juce::ColourGradient (colours::paperLight.brighter (0.12f), skirt.getX(), skirt.getY(),
                                             colours::paperDeep, skirt.getRight(), skirt.getBottom(), false));
    g.fillEllipse (skirt);
    g.setColour (colours::lineDark);
    g.drawEllipse (skirt, juce::jmax (1.0f, radius * 0.02f));

    const auto cap = skirt.reduced (radius * 0.12f);
    g.setGradientFill (juce::ColourGradient (colours::knob.brighter (0.35f), cap.getX(), cap.getY(),
                                             colours::knob, cap.getRight(), cap.getBottom(), false));
    g.fillEllipse (cap);

    // pointer
    const auto p1 = centre.getPointOnCircumference (radius * 0.12f, angle);
    const auto p2 = centre.getPointOnCircumference (radius * 0.56f, angle);
    g.setColour (colours::pointer);
    g.drawLine (juce::Line<float> (p1, p2), juce::jmax (1.5f, radius * 0.07f));
}

void AlbionLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPos, float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle style,
                                          juce::Slider& slider)
{
    if (style != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }

    // fader: hairline slot, orange fill from the bottom, ink cap
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    const auto slotW  = juce::jmax (3.0f, bounds.getWidth() * 0.10f);
    const auto slot   = juce::Rectangle<float> (slotW, bounds.getHeight()).withCentre (bounds.getCentre());
    g.setColour (colours::line);
    g.fillRoundedRectangle (slot, slotW * 0.5f);

    g.setColour (colours::accent.withAlpha (0.55f));
    g.fillRoundedRectangle (slot.withTop (sliderPos), slotW * 0.5f);

    // 0 dB mark
    const auto zeroY = (float) y + (float) height * (1.0f - (float) slider.valueToProportionOfLength (0.0));
    g.setColour (colours::muted.withAlpha (0.5f));
    g.drawHorizontalLine (juce::roundToInt (zeroY), bounds.getX() + 2.0f, bounds.getRight() - 2.0f);

    const auto capH = juce::jmax (10.0f, bounds.getWidth() * 0.32f);
    const auto cap  = juce::Rectangle<float> (bounds.getWidth() * 0.78f, capH).withCentre ({ bounds.getCentreX(), sliderPos });
    g.setColour (colours::ink.withAlpha (0.18f));
    g.fillRoundedRectangle (cap.translated (0.0f, 2.0f), 3.0f);
    g.setColour (colours::knob);
    g.fillRoundedRectangle (cap, 3.0f);
    g.setColour (colours::pointer.withAlpha (0.85f));
    g.drawHorizontalLine (juce::roundToInt (cap.getCentreY()), cap.getX() + 3.0f, cap.getRight() - 3.0f);
}

void AlbionLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                              bool over, bool down)
{
    // like the site's buttons: flat, square-ish; selected = ink ("button-dark")
    const auto r  = b.getLocalBounds().toFloat().reduced (1.0f);
    const auto on = b.getToggleState();
    const auto corner = juce::jmin (4.0f, r.getHeight() * 0.18f);
    const auto onColour = b.getProperties().contains ("onColour")
                            ? juce::Colour ((juce::uint32) (juce::int64) b.getProperties()["onColour"])
                            : colours::ink;

    g.setColour (on ? onColour : colours::paperLight);
    g.fillRoundedRectangle (r, corner);
    if (over || down)
    {
        g.setColour ((on ? juce::Colours::white : colours::accent).withAlpha (down ? 0.22f : 0.12f));
        g.fillRoundedRectangle (r, corner);
    }
    g.setColour (on ? onColour : (over ? colours::accent : colours::lineDark));
    g.drawRoundedRectangle (r, corner, juce::jmax (1.0f, uiScale));
}

void AlbionLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setColour (b.getToggleState() ? colours::paper : colours::ink);
    g.setFont (sans (juce::jmin (12.5f * uiScale, (float) b.getHeight() * 0.58f), true).withExtraKerningFactor (0.06f));
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, false);
}

juce::Font AlbionLookAndFeel::getLabelFont (juce::Label& l)
{
    return l.getFont();
}

juce::Font AlbionLookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return sans (juce::jmin (15.0f * uiScale, (float) box.getHeight() * 0.6f));
}

juce::Font AlbionLookAndFeel::getPopupMenuFont()
{
    return sans (15.0f * juce::jmax (1.0f, uiScale));
}
