#include "AnalyserView.h"
#include "AlbionLookAndFeel.h"

using namespace albion;
using namespace spectrum;

namespace
{
    constexpr float minHz = 20.0f, maxHz = 20000.0f;
    constexpr float gridHz[] = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };

    juce::String hzLabel (float hz)
    {
        return hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz);
    }

    float tilted (float db, int band, float tilt) noexcept
    {
        return db + tilt * std::log2 (bandHz (band) / 1000.0f);
    }
}

void AnalyserView::setData (const Data& d)
{
    data = d;

    // auto-range: keep the loudest part of the live (tilted) spectrum near the top
    if (data.hasLive)
    {
        float peak = -200.0f;
        for (int b = 0; b < numBands; ++b)
            peak = juce::jmax (peak, tilted (data.live[(size_t) b], b, data.tiltDbPerOct));
        const auto wanted = std::ceil ((peak + 6.0f) / 6.0f) * 6.0f;
        topDb += (wanted > topDb ? 0.5f : 0.03f) * (wanted - topDb);
    }
    repaint();
}

juce::Rectangle<float> AnalyserView::plotArea() const
{
    return getLocalBounds().toFloat().reduced (12.0f).withTrimmedLeft (30.0f).withTrimmedBottom (16.0f).withTrimmedTop (22.0f);
}

float AnalyserView::xFor (float hz) const noexcept
{
    const auto r = plotArea();
    return r.getX() + r.getWidth() * std::log (hz / minHz) / std::log (maxHz / minHz);
}

float AnalyserView::yFor (float db) const noexcept
{
    const auto r = plotArea();
    return r.getY() + r.getHeight() * (topDb - db) / rangeDb;
}

juce::Path AnalyserView::curvePath (const Curve& c, float offset) const
{
    juce::Path p;
    for (int b = 0; b < numBands; ++b)
    {
        const auto x = xFor (bandHz (b));
        const auto y = yFor (tilted (c[(size_t) b] + offset, b, data.tiltDbPerOct));
        if (b == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    return p;
}

void AnalyserView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (colours::paperLight);
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (colours::line);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

    const auto r = plotArea();

    // --- grid -----------------------------------------------------------------------
    g.setFont (mono (10.0f));
    for (auto hz : gridHz)
    {
        const auto x = xFor (hz);
        g.setColour (colours::line);
        g.drawVerticalLine (juce::roundToInt (x), r.getY(), r.getBottom());
        g.setColour (colours::muted);
        g.drawText (hzLabel (hz), juce::Rectangle<float> (40.0f, 14.0f).withCentre ({ x, r.getBottom() + 9.0f }),
                    juce::Justification::centred, false);
    }
    for (float db = std::floor (topDb / 12.0f) * 12.0f; db > topDb - rangeDb; db -= 12.0f)
    {
        const auto y = yFor (db);
        if (y < r.getY() + 4.0f)
            continue;
        g.setColour (colours::line);
        g.drawHorizontalLine (juce::roundToInt (y), r.getX(), r.getRight());
        g.setColour (colours::muted);
        g.drawText (juce::String ((int) db), juce::Rectangle<float> (r.getX() - 34.0f, y - 7.0f, 30.0f, 14.0f),
                    juce::Justification::centredRight, false);
    }

    g.saveState();
    g.reduceClipRegion (r.toNearestInt());

    // reference / learned mix are shapes: align them to the live output level
    const auto liveMean = data.hasLive ? midMean (data.live) : topDb - 30.0f;

    if (data.hasLive)
    {
        auto line = curvePath (data.live, 0.0f);
        auto fill = line;
        fill.lineTo (r.getRight(), r.getBottom());
        fill.lineTo (r.getX(), r.getBottom());
        fill.closeSubPath();
        g.setColour (colours::ink.withAlpha (0.08f));
        g.fillPath (fill);
        g.setColour (colours::ink.withAlpha (0.85f));
        g.strokePath (line, juce::PathStrokeType (1.4f));
    }

    if (data.hasMix)
    {
        juce::Path dashed;
        const float dashes[] = { 5.0f, 4.0f };
        juce::PathStrokeType (1.6f).createDashedStroke (dashed, curvePath (data.mix, liveMean), dashes, 2);
        g.setColour (colours::olive);
        g.fillPath (dashed);
    }

    if (data.hasReference)
    {
        g.setColour (colours::accent);
        g.strokePath (curvePath (data.reference, liveMean), juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    g.restoreState();

    // --- legend ------------------------------------------------------------------------
    auto legend = getLocalBounds().toFloat().reduced (14.0f, 6.0f).removeFromTop (16.0f).withTrimmedLeft (30.0f);
    auto item = [&] (juce::Colour c, const juce::String& text, bool dashedLine)
    {
        g.setColour (c);
        const auto sw = legend.removeFromLeft (18.0f).withSizeKeepingCentre (18.0f, 2.0f);
        if (dashedLine) { g.fillRect (sw.withWidth (6.0f)); g.fillRect (sw.withTrimmedLeft (10.0f)); }
        else g.fillRect (sw);
        legend.removeFromLeft (6.0f);
        g.setColour (colours::ink);
        g.setFont (mono (10.0f).withExtraKerningFactor (0.08f));
        const auto w = juce::jmin (legend.getWidth(), (float) text.length() * 7.0f + 8.0f);
        g.drawText (text, legend.removeFromLeft (w), juce::Justification::centredLeft, false);
        legend.removeFromLeft (14.0f);
    };
    item (colours::ink, "OUTPUT", false);
    if (data.hasReference) item (colours::accent, "REFERENCE: " + data.referenceName.toUpperCase(), false);
    if (data.hasMix)       item (colours::olive, "LEARNED MIX", true);


    if (! data.hasLive)
    {
        g.setColour (colours::muted);
        g.setFont (sans (14.0f));
        g.drawText ("Play audio through the plugin to see its spectrum", r, juce::Justification::centred, false);
    }
}

//==============================================================================
void EqView::setCurve (const Curve& c, float s, bool a, const juce::String& cap)
{
    curve = c;
    scale = s;
    active = a;
    caption = cap;
    repaint();
}

void EqView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (colours::paperLight);
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (colours::line);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

    const auto r = bounds.reduced (12.0f).withTrimmedLeft (30.0f).withTrimmedTop (14.0f);
    auto xFor = [&r] (float hz) { return r.getX() + r.getWidth() * std::log (hz / minHz) / std::log (maxHz / minHz); };
    auto yFor = [&] (float db) { return r.getCentreY() - (r.getHeight() * 0.5f) * db / scale; };

    g.setColour (colours::line);
    for (auto hz : gridHz)
        g.drawVerticalLine (juce::roundToInt (xFor (hz)), r.getY(), r.getBottom());
    g.setColour (colours::lineDark);
    g.drawHorizontalLine (juce::roundToInt (yFor (0.0f)), r.getX(), r.getRight());

    g.setColour (colours::muted);
    g.setFont (mono (10.0f));
    for (auto db : { scale, -scale })
        g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), juce::Rectangle<float> (r.getX() - 34.0f, yFor (db) - 7.0f, 30.0f, 14.0f),
                    juce::Justification::centredRight, false);
    g.drawText (caption, bounds.reduced (14.0f, 4.0f).removeFromTop (14.0f).withTrimmedLeft (30.0f), juce::Justification::centredLeft, false);

    juce::Path line;
    for (int b = 0; b < numBands; ++b)
    {
        const auto x = xFor (bandHz (b));
        const auto y = yFor (juce::jlimit (-scale, scale, curve[(size_t) b]));
        if (b == 0) line.startNewSubPath (x, y); else line.lineTo (x, y);
    }
    auto fill = line;
    fill.lineTo (r.getRight(), yFor (0.0f));
    fill.lineTo (r.getX(), yFor (0.0f));
    fill.closeSubPath();

    const auto c = active ? colours::accent : colours::muted;
    g.setColour (c.withAlpha (0.15f));
    g.fillPath (fill);
    g.setColour (c);
    g.strokePath (line, juce::PathStrokeType (1.8f));
}
