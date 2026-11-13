#pragma once

#include "PluginProcessor.h"
#include "AnalyserView.h"
#include "AlbionLookAndFeel.h"

class AlbionReferenceAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                  private juce::Timer,
                                                  private juce::ChangeListener
{
public:
    explicit AlbionReferenceAudioProcessorEditor (AlbionReferenceAudioProcessor&);
    ~AlbionReferenceAudioProcessorEditor() override;

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
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void setupKnob (Knob&, const juce::ParameterID&, const juce::String& caption);
    void rebuildProfileMenu();
    void chooseFile();
    void askProfileName();
    void updateButtons();

    AlbionReferenceAudioProcessor& ref;
    AlbionLookAndFeel lnf;
    albion::ThemeToggle themeToggle;
    juce::TooltipWindow tooltipWindow { this, 600 };

    AnalyserView analyser;
    EqView eqView;

    juce::ComboBox profileBox;
    std::vector<profiles::Profile> profileList;

    juce::TextButton captureButton { "CAPTURE" }, loadButton { "LOAD FILE" }, saveButton { "SAVE" };
    juce::TextButton learnButton { "LEARN MIX" }, eqOnButton { "MATCH EQ ON" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> eqOnAttachment;
    Knob amountKnob, limitKnob, smoothKnob, outputKnob;
    juce::ComboBox tiltBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> tiltAttachment;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> refCard, matchCard, outCard, statusArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AlbionReferenceAudioProcessorEditor)
};
