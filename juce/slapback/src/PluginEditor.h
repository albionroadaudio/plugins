#pragma once

#include "PluginProcessor.h"
#include "AlbionLookAndFeel.h"

class AlbionSlapbackAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                 private juce::Timer
{
public:
    explicit AlbionSlapbackAudioProcessorEditor (AlbionSlapbackAudioProcessor&);
    ~AlbionSlapbackAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    enum KnobIndex { kFeedback, kTone, kLowCut, kDrive, kWow, kSpread, kMix, kNumKnobs };

    void timerCallback() override;
    void setupKnob (Knob&, const juce::ParameterID&, const juce::String& name);

    AlbionSlapbackAudioProcessor& slap;
    AlbionLookAndFeel lnf;   // declared before the components that use it
    albion::ThemeToggle themeToggle;

    // the hero control: big Delay Time knob with its own ms scale
    juce::Slider timeKnob;
    juce::Label  timeLabel, timeValue;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> timeAttachment;
    juce::Rectangle<int> timePanel;
    std::array<Knob, kNumKnobs> knobs;

    juce::Label modeLabel;
    std::array<juce::TextButton, 3> modeButtons;
    std::unique_ptr<juce::ParameterAttachment> modeAttachment;

    juce::ComboBox presetBox;
    int shownPreset = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AlbionSlapbackAudioProcessorEditor)
};
