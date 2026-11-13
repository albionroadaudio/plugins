#include "VuMeterComponent.h"
#include "AlbionLookAndFeel.h"

namespace
{
    constexpr float maxRatio   = 1.41254f;                                     // +3 VU
    constexpr float halfSweep  = juce::degreesToRadians (48.0f);
    constexpr float pinLow     = -0.03f;                                        // needle rest pins
    constexpr float pinHigh    = 1.04f;

    const juce::Colour faceTop    { 0xfffbf6ea };
    const juce::Colour faceBottom { 0xffeee2c8 };
    const juce::Colour ink        { 0xff24241f };   // face print: always dark on the cream face
    const juce::Colour redInk     { 0xffb85f3e };
    juce::Colour bezel()          { return albion::colours::dark; }   // follows the theme

    juce::Font font (float height, bool bold = false)
    {
        return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
    }
}

VuMeterComponent::VuMeterComponent (juce::String channelLabel)
    : label (std::move (channelLabel))
{
    setOpaque (true);
}

float VuMeterComponent::ratioToFraction (float ratio) noexcept
{
    return juce::jlimit (pinLow, pinHigh, ratio / maxRatio);
}

float VuMeterComponent::fractionToAngle (float fraction) noexcept
{
    return juce::jmap (fraction, -halfSweep, halfSweep);
}

VuMeterComponent::Geometry VuMeterComponent::geometry() const
{
    Geometry geo;
    auto area = getLocalBounds().toFloat();
    geo.face  = area.reduced (area.getHeight() * 0.06f);

    const auto h = geo.face.getHeight();
    geo.pivot  = { geo.face.getCentreX(), geo.face.getBottom() + h * 0.28f };
    geo.radius = h * 0.98f;
    return geo;
}

bool VuMeterComponent::setState (float ratio, bool peakLit)
{
    const auto f = ratioToFraction (ratio);
    // ~1/4 pixel of needle-tip travel at typical sizes: don't repaint for less
    if (std::abs (f - needleFraction) < 0.0008f && peakLit == ledLit)
        return false;

    needleFraction = f;
    ledLit = peakLit;
    repaint();
    return true;
}

void VuMeterComponent::setReferenceText (juce::String text)
{
    if (text == referenceText)
        return;
    referenceText = std::move (text);
    renderBackground();
    repaint();
}

juce::Rectangle<float> VuMeterComponent::maxBox() const
{
    const auto face = geometry().face;
    const auto fh = face.getHeight();
    return { face.getRight() - fh * 0.05f - fh * 0.46f, face.getY() + fh * 0.04f, fh * 0.46f, fh * 0.12f };
}

void VuMeterComponent::setMaxReading (bool valid, float vu)
{
    if (valid == maxValid && (! valid || std::abs (vu - maxVu) < 0.05f))
        return;
    maxValid = valid;
    maxVu = vu;
    repaint (maxBox().expanded (2.0f).getSmallestIntegerContainer());
}

void VuMeterComponent::resized()
{
    renderBackground();
}

void VuMeterComponent::renderBackground()
{
    const auto scale = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr
                         ? (float) juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->scale
                         : 1.0f;
    const auto w = juce::jmax (1, juce::roundToInt ((float) getWidth()  * scale));
    const auto h = juce::jmax (1, juce::roundToInt ((float) getHeight() * scale));

    background = juce::Image (juce::Image::ARGB, w, h, true);
    juce::Graphics g (background);
    g.addTransform (juce::AffineTransform::scale (scale));

    const auto geo  = geometry();
    const auto face = geo.face;
    const auto fh   = face.getHeight();
    const auto& c   = geo.pivot;
    const auto r    = geo.radius;

    auto pointAt = [&c] (float angle, float radius)
    {
        return juce::Point<float> (c.x + radius * std::sin (angle), c.y - radius * std::cos (angle));
    };

    // --- bezel + face -----------------------------------------------------
    g.fillAll (bezel());
    const auto corner = fh * 0.04f;
    g.setGradientFill (juce::ColourGradient (faceTop, face.getCentreX(), face.getY(),
                                             faceBottom, face.getCentreX(), face.getBottom(), false));
    g.fillRoundedRectangle (face, corner);

    g.saveState();
    g.reduceClipRegion (face.toNearestInt());

    // --- scale arcs -------------------------------------------------------
    const auto zeroAngle = fractionToAngle (1.0f / maxRatio);
    const auto lineW = juce::jmax (1.0f, fh * 0.008f);

    juce::Path blackArc;
    blackArc.addCentredArc (c.x, c.y, r, r, 0.0f, -halfSweep, zeroAngle, true);
    g.setColour (ink);
    g.strokePath (blackArc, juce::PathStrokeType (lineW));

    juce::Path redBand;
    redBand.addCentredArc (c.x, c.y, r + fh * 0.02f, r + fh * 0.02f, 0.0f, zeroAngle, halfSweep, true);
    g.setColour (redInk);
    g.strokePath (redBand, juce::PathStrokeType (fh * 0.04f));

    // --- VU ticks & labels --------------------------------------------------
    struct Mark { float vu; const char* text; bool major; };
    static constexpr Mark marks[] = {
        { -20, "20", true }, { -10, "10", true }, { -7, "7", true }, { -5, "5", true },
        { -3, "3", true },   { -2, "2", true },   { -1, "1", true }, { 0, "0", true },
        { 1, "1", true },    { 2, "2", true },    { 3, "3", true },
        { -15, nullptr, false }, { -6, nullptr, false }, { -4, nullptr, false },
        { -0.5f, nullptr, false }, { 0.5f, nullptr, false }, { 1.5f, nullptr, false }, { 2.5f, nullptr, false },
    };

    g.setFont (font (fh * 0.095f, true));
    for (const auto& m : marks)
    {
        const auto ratio = juce::Decibels::decibelsToGain (m.vu);
        const auto a     = fractionToAngle (ratio / maxRatio);
        const auto len   = m.major ? fh * 0.075f : fh * 0.04f;

        g.setColour (m.vu >= 0.0f ? redInk : ink);
        g.drawLine (juce::Line<float> (pointAt (a, r), pointAt (a, r + len)), lineW * (m.major ? 1.4f : 1.0f));

        if (m.text != nullptr)
        {
            const auto p = pointAt (a, r + fh * 0.15f);
            g.drawText (m.text, juce::Rectangle<float> (fh * 0.2f, fh * 0.11f).withCentre (p),
                        juce::Justification::centred, false);
        }
    }

    // "+" / "-" either side of the scale
    g.setColour (ink);
    g.setFont (font (fh * 0.12f, true));
    g.drawText ("-", juce::Rectangle<float> (fh * 0.15f, fh * 0.15f).withCentre (pointAt (-halfSweep - 0.09f, r + fh * 0.1f)),
                juce::Justification::centred, false);
    g.setColour (redInk);
    g.drawText ("+", juce::Rectangle<float> (fh * 0.15f, fh * 0.15f).withCentre (pointAt (halfSweep + 0.09f, r + fh * 0.1f)),
                juce::Justification::centred, false);

    // --- percentage scale (inside the arc) ----------------------------------
    g.setColour (ink.withAlpha (0.75f));
    g.setFont (font (fh * 0.06f));
    for (int pct = 0; pct <= 100; pct += 20)
    {
        const auto a = fractionToAngle ((float) pct / 100.0f / maxRatio);
        g.drawLine (juce::Line<float> (pointAt (a, r - fh * 0.005f), pointAt (a, r - fh * 0.045f)), lineW);
        g.drawText (juce::String (pct), juce::Rectangle<float> (fh * 0.16f, fh * 0.07f).withCentre (pointAt (a, r - fh * 0.09f)),
                    juce::Justification::centred, false);
    }

    // --- legends -------------------------------------------------------------
    g.setColour (ink);
    g.setFont (font (fh * 0.17f, true));
    g.drawText ("VU", juce::Rectangle<float> (face.getWidth(), fh * 0.2f)
                          .withCentre ({ face.getCentreX(), face.getY() + fh * 0.6f }),
                juce::Justification::centred, false);

    g.setFont (font (fh * 0.075f));
    g.setColour (ink.withAlpha (0.7f));
    g.drawText (referenceText, juce::Rectangle<float> (face.getWidth(), fh * 0.09f)
                                   .withCentre ({ face.getCentreX(), face.getY() + fh * 0.73f }),
                juce::Justification::centred, false);

    g.setFont (font (fh * 0.1f, true));
    g.setColour (ink);
    g.drawText (label, face.reduced (fh * 0.05f).removeFromBottom (fh * 0.12f),
                juce::Justification::bottomLeft, false);

    g.setFont (font (fh * 0.055f));
    g.drawText ("PEAK", face.reduced (fh * 0.05f).removeFromBottom (fh * 0.08f),
                juce::Justification::bottomRight, false);

    // --- needle hood at the bottom -------------------------------------------
    const auto hoodR = fh * 0.48f;
    g.setColour (bezel());
    g.fillEllipse (juce::Rectangle<float> (hoodR * 2.0f, hoodR * 2.0f).withCentre (c));

    g.restoreState();

    // glass highlight
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.18f), face.getX(), face.getY(),
                                             juce::Colours::transparentWhite, face.getX(), face.getCentreY(), false));
    g.fillRoundedRectangle (face, corner);
}

void VuMeterComponent::paint (juce::Graphics& g)
{
    g.drawImage (background, getLocalBounds().toFloat());

    const auto geo = geometry();
    const auto fh  = geo.face.getHeight();
    const auto a   = fractionToAngle (needleFraction);
    const auto& c  = geo.pivot;

    g.saveState();
    g.reduceClipRegion (geo.face.toNearestInt());

    // clip the needle so it disappears under the hood
    juce::Path hood;
    const auto hoodR = fh * 0.48f;
    hood.addEllipse (juce::Rectangle<float> (hoodR * 2.0f, hoodR * 2.0f).withCentre (c));
    hood.setUsingNonZeroWinding (false);
    hood.addRectangle (geo.face.expanded (fh));
    g.reduceClipRegion (hood);

    const juce::Point<float> tip (c.x + (geo.radius + fh * 0.07f) * std::sin (a),
                                  c.y - (geo.radius + fh * 0.07f) * std::cos (a));

    // soft shadow, then needle
    g.setColour (juce::Colours::black.withAlpha (0.18f));
    g.drawLine (juce::Line<float> (c.translated (fh * 0.02f, fh * 0.015f), tip.translated (fh * 0.02f, fh * 0.015f)), fh * 0.012f);
    g.setColour (ink);
    g.drawLine (juce::Line<float> (c, tip), juce::jmax (1.0f, fh * 0.009f));

    g.restoreState();

    // max-reading hold, top right: the highest point the needle has reached
    {
        const auto box = maxBox();
        const bool hot = maxValid && maxVu >= 0.0f;
        g.setColour (bezel().withAlpha (0.88f));
        g.fillRoundedRectangle (box, box.getHeight() * 0.25f);
        g.setColour (hot ? albion::colours::accent : albion::colours::muted);
        g.drawRoundedRectangle (box, box.getHeight() * 0.25f, juce::jmax (1.0f, fh * 0.006f));

        auto inner = box.reduced (box.getHeight() * 0.25f, 0.0f);
        g.setColour (albion::colours::peach);
        g.setFont (font (box.getHeight() * 0.48f, true));
        g.drawText ("MAX", inner, juce::Justification::centredLeft, false);

        juce::String text ("--");
        if (maxValid)
            text = maxVu < -30.0f ? juce::String ("< -30")
                                  : (maxVu >= 0.05f ? "+" : "") + juce::String (maxVu, 1) + " dB";
        g.setColour (hot ? albion::colours::peach : juce::Colour (0xfff3f0e7));
        g.setFont (font (box.getHeight() * 0.6f, true));
        g.drawText (text, inner, juce::Justification::centredRight, false);
    }

    // peak LED, bottom right of the face
    const auto ledD = fh * 0.07f;
    const auto led  = juce::Rectangle<float> (ledD, ledD)
                          .withCentre ({ geo.face.getRight() - fh * 0.09f, geo.face.getBottom() - fh * 0.17f });
    if (ledLit)
    {
        g.setColour (juce::Colours::red.withAlpha (0.35f));
        g.fillEllipse (led.expanded (ledD * 0.35f));
    }
    g.setColour (ledLit ? juce::Colour (0xffff3020) : juce::Colour (0xff5a1a14));
    g.fillEllipse (led);
    g.setColour (ink);
    g.drawEllipse (led, juce::jmax (1.0f, fh * 0.006f));
}
