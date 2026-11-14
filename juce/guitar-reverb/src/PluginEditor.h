#pragma once

#include "PluginProcessor.h"
#include "AlbionLookAndFeel.h"

class AlbionGuitarReverbAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                     private juce::Timer
{
public:
    explicit AlbionGuitarReverbAudioProcessorEditor (AlbionGuitarReverbAudioProcessor&);
    ~AlbionGuitarReverbAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    enum KnobIndex { kPreDelay, kLowCut, kHighCut, kWidth, kDuck, kMix, kNumKnobs };

    void timerCallback() override;
    void setupKnob (Knob&, const juce::ParameterID&, const juce::String& name);
    void drawZone (juce::Graphics&, juce::Slider&, const Zone&, float radiusScale, float thickness);

    AlbionGuitarReverbAudioProcessor& proc;
    AlbionLookAndFeel lnf;   // declared before the components that use it
    albion::ThemeToggle themeToggle;

    // hero control: big Decay knob with its own seconds scale
    juce::Slider decayKnob;
    juce::Label  decayLabel, decayValue;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> decayAttachment;
    juce::Rectangle<int> decayPanel, voicePanel, knobPanel;
    std::array<Knob, kNumKnobs> knobs;

    juce::Label voiceLabel, blurb;
    std::array<juce::Label, 3> groupLabels;
    std::array<juce::TextButton, voices::count> voiceButtons;
    std::unique_ptr<juce::ParameterAttachment> voiceAttachment;
    int shownVoice = -1;

    juce::ComboBox presetBox;
    int shownPreset = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AlbionGuitarReverbAudioProcessorEditor)
};
