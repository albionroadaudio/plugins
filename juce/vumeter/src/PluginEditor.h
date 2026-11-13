#pragma once

#include "PluginProcessor.h"
#include "VuMeterComponent.h"
#include "AlbionLookAndFeel.h"

class VuMeterAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                          private juce::Timer
{
public:
    explicit VuMeterAudioProcessorEditor (VuMeterAudioProcessor&);
    ~VuMeterAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void timerCallback() override;
    void resetHolds();
    void setupKnob (Knob&, const juce::String& paramId, const juce::String& name);

    VuMeterAudioProcessor& vuProcessor;
    AlbionLookAndFeel lnf;   // declared before the components that use it
    albion::ThemeToggle themeToggle;
    juce::TooltipWindow tooltipWindow { this, 600 };

    VuMeterComponent meterL { "L" }, meterR { "R" };
    std::array<VuMeterComponent*, VuMeterAudioProcessor::maxChannels> meters { &meterL, &meterR };
    std::array<double, VuMeterAudioProcessor::maxChannels> ledOffTime {};   // ms timestamp
    int shownChannels = VuMeterAudioProcessor::maxChannels;

    Knob referenceKnob, peakKnob;
    juce::TextButton resetButton { "RESET" };
    juce::Rectangle<int> meterPanel, controlPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VuMeterAudioProcessorEditor)
};
