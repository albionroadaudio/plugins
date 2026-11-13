#pragma once

#include "PluginProcessor.h"
#include "GrMeterComponent.h"
#include "AlbionLookAndFeel.h"

class Albion76AudioProcessorEditor final : public juce::AudioProcessorEditor,
                                           private juce::Timer
{
public:
    explicit Albion76AudioProcessorEditor (Albion76AudioProcessor&);
    ~Albion76AudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    enum KnobIndex { kGain, kThreshold, kKnee, kAttack, kRelease, kMakeup, kMix, kNumKnobs };

    void timerCallback() override;
    void setupKnob (Knob&, const juce::ParameterID&, const juce::String& name);

    Albion76AudioProcessor& albion;
    AlbionLookAndFeel lnf;   // declared before the components that use it
    albion::ThemeToggle themeToggle;

    GrMeterComponent grMeter;
    std::array<Knob, kNumKnobs> knobs;

    juce::Label ratioLabel;
    std::array<juce::TextButton, 5> ratioButtons;
    std::unique_ptr<juce::ParameterAttachment> ratioAttachment;

    juce::ComboBox presetBox;
    int shownPreset = -1;

    float needleGr = 0.0f;
    double lastFrameMs = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Albion76AudioProcessorEditor)
};
