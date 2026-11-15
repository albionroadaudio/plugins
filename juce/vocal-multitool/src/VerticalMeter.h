#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Vertical gain-reduction meter: 0 dB at the top, the bar grows downwards.
// A brass tick marks the stage's recipe target (e.g. -10 dB) and a thin line
// holds the recent peak.
class VerticalMeter final : public juce::Component
{
public:
    VerticalMeter (float rangeDb, float targetDb);

    // Positive dB of gain reduction, already ballistically smoothed.
    void setGainReduction (float grDb, float peakHoldDb);
    void paint (juce::Graphics&) override;

private:
    float yFor (float db, juce::Rectangle<float> bar) const noexcept;

    float range, target;
    float shown = 0.0f, hold = 0.0f;
};
