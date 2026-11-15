#pragma once

#include "PluginProcessor.h"
#include "AlbionLookAndFeel.h"
#include "VerticalMeter.h"

// Small pill-shaped on/off switch for a section's title row.
class PowerSwitch final : public juce::Button
{
public:
    PowerSwitch() : juce::Button ("power") { setClickingTogglesState (true); }
    void paintButton (juce::Graphics&, bool isMouseOver, bool isButtonDown) override;
};

class VocalMultitoolEditor final : public juce::AudioProcessorEditor,
                                   private juce::Timer
{
public:
    explicit VocalMultitoolEditor (VocalMultitoolProcessor&);
    ~VocalMultitoolEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    enum KnobIndex { kDenoise, kClamp, kDeEss, kMud, kNasal, kAir, kGlue, kOutput, kNumKnobs };
    enum Section   { sDenoise, sHpf, sClamp, sDeEss, sEq, sGlue, sOutput, kNumSections };

    struct MeterState { float needle = 0.0f, hold = 0.0f, holdAge = 0.0f; };

    void timerCallback() override;
    void setupKnob (Knob&, const juce::ParameterID&, const juce::String& name, const juce::String& tip);

    VocalMultitoolProcessor& proc;
    AlbionLookAndFeel lnf;   // declared before the components that use it
    albion::ThemeToggle themeToggle;

    std::array<Knob, kNumKnobs> knobs;
    // one switch per stage (sDenoise .. sGlue); the High-Pass switch is the filter itself
    std::array<PowerSwitch, kNumSections - 1> switches;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>, kNumSections - 1> switchAttachments;
    std::array<juce::Rectangle<int>, kNumSections> titleRows;
    void sectionSwitched (int section);
    std::vector<juce::Component*> componentsOf (int section);

    VerticalMeter clampMeter { 20.0f, (float) chain::clampTargetDb };
    VerticalMeter deEssMeter { 12.0f, 6.0f };
    VerticalMeter glueMeter  { 12.0f, (float) chain::glueTargetDb };
    std::array<MeterState, 3> meterStates;

    std::array<juce::Rectangle<int>, kNumSections> panels;
    juce::Rectangle<int> hpfCurveArea;

    juce::ComboBox presetBox;
    int shownPreset = -1;
    double lastFrameMs = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalMultitoolEditor)
};
