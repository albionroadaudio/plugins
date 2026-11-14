#pragma once

#include "PluginProcessor.h"
#include "AlbionLookAndFeel.h"
#include "KitViews.h"

class AlbionDrumsAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                              private juce::Timer,
                                              private juce::ChangeListener
{
public:
    explicit AlbionDrumsAudioProcessorEditor (AlbionDrumsAudioProcessor&);
    ~AlbionDrumsAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    enum { kMaster, kTune, kDynamics, kBleed, kHumanize, kNumKnobs };

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void rebuildForKit();
    void chooseKit();
    void setupKnob (Knob&, const juce::ParameterID&, const juce::String& name);

    AlbionDrumsAudioProcessor& drums;
    AlbionLookAndFeel lnf;
    albion::ThemeToggle themeToggle;
    juce::TooltipWindow tooltipWindow { this, 600 };

    juce::TextButton loadButton { "LOAD KIT" };
    juce::ComboBox variantBox, midimapBox;
    juce::Array<juce::File> variantFiles, midimapFiles;

    juce::Viewport padView, mixerView;
    juce::TextButton micsTab { "MICS" }, drumsTab { "DRUMS" };
    juce::TextButton saveDefaultsButton { "SAVE AS KIT DEFAULT" }, recallDefaultsButton { "RECALL DEFAULT" };
    void updateDefaultsButtons();
    std::vector<std::unique_ptr<MixerStrip>> drumStrips;
    std::vector<int> drumStripInstrument;   // strip index -> kit instrument
    bool showDrums = false;
    void setMixerView (bool drumsView);
    PadGrid padGrid;
    juce::Component mixerContent;
    std::vector<std::unique_ptr<MixerStrip>> strips;

    std::array<Knob, kNumKnobs> knobs;
    juce::Image kitImage;

    std::unique_ptr<juce::FileChooser> chooser;
    const Kit* shownKit = nullptr;   // identity only; always re-fetch from the processor

    juce::Rectangle<int> infoArea, padPanel, sidePanel, mixerPanel, imageArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AlbionDrumsAudioProcessorEditor)
};
