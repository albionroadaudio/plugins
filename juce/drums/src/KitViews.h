#pragma once

#include "AlbionLookAndFeel.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <functional>

// One drum pad: shows the instrument and its MIDI note, flashes on hits,
// click to audition (higher up the pad = harder hit).
class PadComponent final : public juce::Component
{
public:
    PadComponent (int note, int instrument, juce::String name);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    void setHit (float velocity) noexcept;    // called by the editor's timer
    bool decay (float amount) noexcept;       // returns true if it needs a repaint

    std::function<void (int note, float velocity)> onAudition;

    const int note, instrument;

private:
    juce::String name;
    float glow = 0.0f;
};

class PadGrid final : public juce::Component
{
public:
    void setPads (std::vector<std::unique_ptr<PadComponent>> newPads);
    int layoutFor (int width);                // returns the height needed
    void resized() override;
    void paint (juce::Graphics&) override;

    std::vector<std::unique_ptr<PadComponent>> pads;
    juce::String emptyMessage { "Load a DrumGizmo kit to start" };
};

// One mixer strip bound to a mixer ValueTree node: a mic channel (with pan)
// or a drum instrument (no pan; its fader scales every mic of that drum, and
// it has Gate, Bleed and Tune knobs for that drum).
class MixerStrip final : public juce::Component
{
public:
    MixerStrip (juce::ValueTree channelState, bool showPan, float uiScale = 1.0f);

    void paint (juce::Graphics&) override;
    void resized() override;
    void setMeter (float peak) noexcept;

private:
    juce::ValueTree state;
    juce::String title;
    bool hasPan = true;
    juce::Label readout;
    juce::Slider pan, gate, bleed, tune, fader;
    juce::Rectangle<int> gateText, bleedText, tuneText;

    void setupDrumKnob (juce::Slider&, const juce::Identifier& property, double defaultValue);
    void drawKnobText (juce::Graphics&, juce::Rectangle<int>, const juce::String& text, bool active);
    juce::TextButton mute { "M" }, solo { "S" };
    float meterLevel = 0.0f, meterHold = 0.0f;
    float scale = 1.0f;
};
