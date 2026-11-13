#pragma once

#include "Spectrum.h"
#include <juce_gui_basics/juce_gui_basics.h>

// Spectrum analyser: live output, reference and learned mix on one log-frequency
// axis. Reference and learned mix are tonal shapes (loudness-normalised), so they
// are drawn level-aligned to the live output for comparison.
class AnalyserView final : public juce::Component
{
public:
    struct Data
    {
        spectrum::Curve live {};   bool hasLive = false;
        spectrum::Curve reference {}; bool hasReference = false;
        spectrum::Curve mix {};    bool hasMix = false;
        float tiltDbPerOct = 3.0f;
        juce::String referenceName;
    };

    void setData (const Data&);
    void paint (juce::Graphics&) override;

private:
    float xFor (float hz) const noexcept;
    float yFor (float db) const noexcept;
    juce::Path curvePath (const spectrum::Curve&, float offset) const;
    juce::Rectangle<float> plotArea() const;

    Data data;
    float topDb = -30.0f;      // auto-ranging top of the display
    static constexpr float rangeDb = 66.0f;
};

// The match EQ curve (what is actually applied), on its own +-dB strip.
class EqView final : public juce::Component
{
public:
    void setCurve (const spectrum::Curve& appliedDb, float scaleDb, bool active, const juce::String& caption);
    void paint (juce::Graphics&) override;

private:
    spectrum::Curve curve {};
    float scale = 6.0f;
    bool active = false;
    juce::String caption;
};
