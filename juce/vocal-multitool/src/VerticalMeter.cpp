#include "VerticalMeter.h"
#include "AlbionLookAndFeel.h"

using namespace albion;

VerticalMeter::VerticalMeter (float rangeDb, float targetDb) : range (rangeDb), target (targetDb)
{
    setInterceptsMouseClicks (false, false);
}

void VerticalMeter::setGainReduction (float grDb, float peakHoldDb)
{
    if (std::abs (grDb - shown) < 0.02f && std::abs (peakHoldDb - hold) < 0.02f)
        return;
    shown = grDb;
    hold = peakHoldDb;
    repaint();
}

float VerticalMeter::yFor (float db, juce::Rectangle<float> bar) const noexcept
{
    return bar.getY() + bar.getHeight() * juce::jlimit (0.0f, 1.0f, db / range);
}

void VerticalMeter::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const auto w = b.getWidth();
    const auto fontH = juce::jlimit (8.0f, 13.0f, w * 0.26f);

    auto area = b;
    auto readout = area.removeFromBottom (fontH * 1.6f);
    area.removeFromTop (fontH * 0.5f);
    area.removeFromBottom (fontH * 0.5f);

    // bezel and bar slot on the left, scale on the right
    auto bezel = area.removeFromLeft (w * 0.46f);
    const auto corner = juce::jmin (4.0f, bezel.getWidth() * 0.2f);
    g.setColour (colours::dark);
    g.fillRoundedRectangle (bezel, corner);
    auto bar = bezel.reduced (bezel.getWidth() * 0.2f, bezel.getWidth() * 0.2f);

    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRect (bar);

    // reduction bar, peach above the target, accent past it
    const auto yNow = yFor (shown, bar);
    if (yNow > bar.getY() + 0.5f)
    {
        const auto yTarget = yFor (target, bar);
        auto fill = bar.withBottom (yNow);
        auto under = fill.removeFromTop (juce::jmax (0.0f, juce::jmin (fill.getHeight(), yTarget - fill.getY())));
        g.setColour (colours::peach);
        g.fillRect (under);
        g.setColour (colours::accent);
        g.fillRect (fill);
    }

    // peak hold
    if (hold > 0.05f)
    {
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.fillRect (bar.withY (yFor (hold, bar) - 1.0f).withHeight (2.0f));
    }

    // scale
    auto scale = area.withTrimmedLeft (w * 0.06f);
    g.setFont (albion::mono (fontH * 0.82f));
    for (float m : { 0.0f, 3.0f, 6.0f, 10.0f, 15.0f, 20.0f })
    {
        if (m > range)
            continue;
        const auto y = yFor (m, bar);
        const auto isTarget = std::abs (m - target) < 0.01f;
        g.setColour (isTarget ? colours::brass : colours::muted);
        g.fillRect (juce::Rectangle<float> (bezel.getRight() + 1.0f, y - 0.5f, w * 0.08f, isTarget ? 2.0f : 1.0f));
        g.drawText (juce::String ((int) m), scale.withY (y - fontH * 0.5f).withHeight (fontH).withTrimmedLeft (w * 0.1f),
                    juce::Justification::centredLeft, false);
    }

    // target tick across the slot
    g.setColour (colours::brass);
    g.fillRect (bezel.withY (yFor (target, bar) - 1.0f).withHeight (2.0f));

    g.setColour (colours::cream);
    g.setFont (albion::mono (fontH));
    g.drawText (shown < 0.05f ? juce::String ("0.0") : "-" + juce::String (shown, 1), readout,
                juce::Justification::centred, false);
}
