#include "GrMeterComponent.h"
#include "AlbionLookAndFeel.h"

using namespace albion;

namespace
{
    constexpr float halfSweep = juce::degreesToRadians (48.0f);
    const juce::Colour faceTop    { 0xfffbf6ea };
    const juce::Colour faceBottom { 0xffeee2c8 };
    const juce::Colour faceInk    { 0xff24241f };   // face print: always dark on the cream face
    const juce::Colour faceRed    { 0xffb85f3e };

    juce::Font font (float h, bool bold = false)
    {
        return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
    }
}

GrMeterComponent::GrMeterComponent()
{
    setOpaque (true);
    setInterceptsMouseClicks (false, false);
}

float GrMeterComponent::grToAngle (float grDb) noexcept
{
    const auto fraction = juce::jlimit (-0.03f, 1.02f, juce::Decibels::decibelsToGain (-grDb));
    return juce::jmap (fraction, -halfSweep, halfSweep);
}

GrMeterComponent::Geometry GrMeterComponent::geometry() const
{
    Geometry geo;
    geo.face = getLocalBounds().toFloat().reduced ((float) getHeight() * 0.05f);
    const auto h = geo.face.getHeight();
    geo.pivot  = { geo.face.getCentreX(), geo.face.getBottom() + h * 0.22f };
    geo.radius = juce::jmin (h * 0.92f, geo.face.getWidth() * 0.55f);
    return geo;
}

void GrMeterComponent::setGainReduction (float grDb)
{
    if (std::abs (grDb - shownGr) < 0.02f)
        return;
    shownGr = grDb;
    repaint();
}

void GrMeterComponent::resized()
{
    renderBackground();
}

void GrMeterComponent::renderBackground()
{
    const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
    const auto scale = display != nullptr ? (float) display->scale : 1.0f;
    background = juce::Image (juce::Image::ARGB,
                              juce::jmax (1, juce::roundToInt ((float) getWidth()  * scale)),
                              juce::jmax (1, juce::roundToInt ((float) getHeight() * scale)), true);
    juce::Graphics g (background);
    g.addTransform (juce::AffineTransform::scale (scale));

    const auto geo  = geometry();
    const auto face = geo.face;
    const auto fh   = face.getHeight();
    const auto c    = geo.pivot;
    const auto r    = geo.radius;
    auto pointAt = [c] (float a, float rad) { return juce::Point<float> (c.x + rad * std::sin (a), c.y - rad * std::cos (a)); };

    g.fillAll (colours::dark);
    const auto corner = fh * 0.04f;
    g.setGradientFill (juce::ColourGradient (faceTop, face.getCentreX(), face.getY(),
                                             faceBottom, face.getCentreX(), face.getBottom(), false));
    g.fillRoundedRectangle (face, corner);

    g.saveState();
    g.reduceClipRegion (face.toNearestInt());

    const auto lineW = juce::jmax (1.0f, fh * 0.008f);
    juce::Path arc;
    arc.addCentredArc (c.x, c.y, r, r, 0.0f, -halfSweep, halfSweep, true);
    g.setColour (faceInk);
    g.strokePath (arc, juce::PathStrokeType (lineW));

    // heavy-compression zone (beyond -10 dB) in red
    juce::Path redBand;
    redBand.addCentredArc (c.x, c.y, r + fh * 0.02f, r + fh * 0.02f, 0.0f, -halfSweep, grToAngle (10.0f), true);
    g.setColour (faceRed);
    g.strokePath (redBand, juce::PathStrokeType (fh * 0.035f));

    struct Mark { float gr; const char* text; };
    static constexpr Mark marks[] = { { 20, "20" }, { 10, "10" }, { 7, "7" }, { 5, "5" }, { 3, "3" },
                                      { 2, "2" }, { 1, "1" }, { 0, "0" },
                                      { 15, nullptr }, { 4, nullptr }, { 6, nullptr }, { 8, nullptr },
                                      { 0.5f, nullptr }, { 1.5f, nullptr } };
    g.setFont (font (fh * 0.095f, true));
    for (const auto& m : marks)
    {
        const auto a = grToAngle (m.gr);
        const auto major = m.text != nullptr;
        g.setColour (m.gr >= 10.0f ? faceRed : faceInk);
        g.drawLine (juce::Line<float> (pointAt (a, r), pointAt (a, r + (major ? fh * 0.075f : fh * 0.04f))),
                    lineW * (major ? 1.4f : 1.0f));
        if (major)
            g.drawText (m.text, juce::Rectangle<float> (fh * 0.2f, fh * 0.11f).withCentre (pointAt (a, r + fh * 0.15f)),
                        juce::Justification::centred, false);
    }

    g.setColour (faceInk);
    g.setFont (font (fh * 0.09f, true));
    g.drawText ("GAIN REDUCTION", juce::Rectangle<float> (face.getWidth(), fh * 0.12f)
                                      .withCentre ({ face.getCentreX(), face.getY() + fh * 0.56f }),
                juce::Justification::centred, false);
    g.setFont (font (fh * 0.07f));
    g.drawText ("dB", juce::Rectangle<float> (face.getWidth(), fh * 0.1f)
                          .withCentre ({ face.getCentreX(), face.getY() + fh * 0.66f }),
                juce::Justification::centred, false);

    g.setColour (colours::dark);
    const auto hoodR = fh * 0.4f;
    g.fillEllipse (juce::Rectangle<float> (hoodR * 2.0f, hoodR * 2.0f).withCentre (c));

    g.restoreState();

    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.18f), face.getX(), face.getY(),
                                             juce::Colours::transparentWhite, face.getX(), face.getCentreY(), false));
    g.fillRoundedRectangle (face, corner);
}

void GrMeterComponent::paint (juce::Graphics& g)
{
    g.drawImage (background, getLocalBounds().toFloat());

    const auto geo = geometry();
    const auto fh  = geo.face.getHeight();
    const auto c   = geo.pivot;
    const auto a   = grToAngle (shownGr);

    g.saveState();
    g.reduceClipRegion (geo.face.toNearestInt());

    juce::Path outsideHood;
    const auto hoodR = fh * 0.4f;
    outsideHood.addEllipse (juce::Rectangle<float> (hoodR * 2.0f, hoodR * 2.0f).withCentre (c));
    outsideHood.setUsingNonZeroWinding (false);
    outsideHood.addRectangle (geo.face.expanded (fh));
    g.reduceClipRegion (outsideHood);

    const juce::Point<float> tip (c.x + (geo.radius + fh * 0.07f) * std::sin (a),
                                  c.y - (geo.radius + fh * 0.07f) * std::cos (a));
    g.setColour (juce::Colours::black.withAlpha (0.18f));
    g.drawLine (juce::Line<float> (c.translated (fh * 0.02f, fh * 0.015f), tip.translated (fh * 0.02f, fh * 0.015f)), fh * 0.012f);
    g.setColour (faceInk);
    g.drawLine (juce::Line<float> (c, tip), juce::jmax (1.0f, fh * 0.009f));
    g.restoreState();

    // numeric readout on the hood
    g.setColour (colours::peach);
    g.setFont (font (fh * 0.085f, true));
    const auto text = shownGr < 0.05f ? juce::String ("0.0 dB") : "-" + juce::String (shownGr, 1) + " dB";
    g.drawText (text, juce::Rectangle<float> (fh * 0.6f, fh * 0.1f).withCentre ({ c.x, geo.face.getBottom() - fh * 0.08f }),
                juce::Justification::centred, false);
}
