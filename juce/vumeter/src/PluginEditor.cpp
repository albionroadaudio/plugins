#include "PluginEditor.h"

using namespace albion;

namespace
{
    constexpr int    baseWidth  = 760;
    constexpr int    baseHeight = 430;
    constexpr int    headerH    = 58;
    constexpr int    controlsH  = 104;
    constexpr double ledHoldMs  = 1500.0;

    juce::Font font (float h, bool bold = false)
    {
        return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
    }
}

VuMeterAudioProcessorEditor::VuMeterAudioProcessorEditor (VuMeterAudioProcessor& p)
    : AudioProcessorEditor (&p), vuProcessor (p)
{
    setLookAndFeel (&lnf);

    themeToggle.onClick = [this] { albion::switchTheme (*this, lnf); };
    addAndMakeVisible (themeToggle);

    for (auto* m : meters)
    {
        m->onClick = [this] { resetHolds(); };   // click a meter to reset MAX + peak LEDs
        addAndMakeVisible (*m);
    }

    setupKnob (referenceKnob, "reference", "0 VU REFERENCE");
    setupKnob (peakKnob,      "peakled",   "PEAK LED");
    referenceKnob.slider.setTooltip ("The level (dBFS RMS of a sine) that reads 0 VU");
    peakKnob.slider.setTooltip ("Sample peak level that lights the red LED");

    resetButton.setTooltip ("Reset the MAX readings and peak LEDs (or click a meter)");
    resetButton.onClick = [this] { resetHolds(); };
    addAndMakeVisible (resetButton);

    setResizable (true, true);
    setResizeLimits (baseWidth * 2 / 3, baseHeight * 2 / 3, baseWidth * 3, baseHeight * 3);
    getConstrainer()->setFixedAspectRatio ((double) baseWidth / (double) baseHeight);
    setSize (baseWidth, baseHeight);

    startTimerHz (60);
}

VuMeterAudioProcessorEditor::~VuMeterAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void VuMeterAudioProcessorEditor::setupKnob (Knob& k, const juce::String& paramId, const juce::String& name)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 90, 20);
    k.slider.setTextValueSuffix (" dBFS");
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    k.slider.setColour (juce::Slider::textBoxTextColourId, colours::cream);
    addAndMakeVisible (k.slider);

    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centredLeft);
    k.label.setColour (juce::Label::textColourId, colours::brass);
    addAndMakeVisible (k.label);

    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (vuProcessor.apvts, paramId, k.slider);
    auto* param = vuProcessor.apvts.getParameter (paramId);
    k.slider.setDoubleClickReturnValue (true, (double) param->convertFrom0to1 (param->getDefaultValue()));
}

void VuMeterAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto s = (float) getHeight() / (float) baseHeight;
    auto bounds = getLocalBounds().toFloat();

    g.setGradientFill (juce::ColourGradient (colours::panelLight, 0.0f, 0.0f,
                                             colours::panel, 0.0f, bounds.getBottom(), false));
    g.fillAll();

    // header: shared Albion Road Audio header (logo, wordmark, product)
    auto header = bounds.removeFromTop ((float) headerH * s);
    albion::drawHeader (g, header, juce::String::fromUTF8 ("VU Meter"), s);

    for (auto r : { meterPanel, controlPanel })
    {
        g.setColour (colours::panelDark.withAlpha (0.35f));
        g.fillRoundedRectangle (r.toFloat(), 8.0f * s);
    }

}

void VuMeterAudioProcessorEditor::resized()
{
    const auto s = (float) getHeight() / (float) baseHeight;
    lnf.uiScale = s;
    auto px = [s] (float v) { return juce::roundToInt (v * s); };

    auto area = getLocalBounds();
    auto header = area.removeFromTop (px ((float) headerH));
    themeToggle.setBounds (header.removeFromRight (px (54)).withSizeKeepingCentre (px (30), px (30)));
    area = area.reduced (px (14));

    controlPanel = area.removeFromBottom (px ((float) controlsH));
    area.removeFromBottom (px (12));
    meterPanel = area;

    // --- meters -----------------------------------------------------------------
    auto meterArea = meterPanel.reduced (px (10));
    if (shownChannels == 1)
    {
        meters[0]->setBounds (meterArea.withSizeKeepingCentre (meterArea.getWidth() / 2, meterArea.getHeight()));
        meters[1]->setVisible (false);
    }
    else
    {
        const auto gap = px (10);
        auto left = meterArea.removeFromLeft ((meterArea.getWidth() - gap) / 2);
        meterArea.removeFromLeft (gap);
        meters[0]->setBounds (left);
        meters[1]->setBounds (meterArea);
        meters[1]->setVisible (true);
    }

    // --- controls: [ref knob + value] [peak knob + value] ......... [RESET] --------
    auto c = controlPanel.reduced (px (14), px (10));
    resetButton.setBounds (c.removeFromRight (px (96)).withSizeKeepingCentre (px (96), px (32)));
    c.removeFromRight (px (12));

    // compact group: caption above, knob with its value right beside it
    auto placeKnob = [&] (Knob& k, juce::Rectangle<int> r)
    {
        auto group = r.withSizeKeepingCentre (juce::jmin (r.getWidth(), px (190)), r.getHeight());
        k.label.setFont (font (12.0f * s, true).withExtraKerningFactor (0.12f));
        k.label.setBounds (group.removeFromTop (px (16)));
        k.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, px (96), px (20));
        k.slider.setBounds (group.withWidth (px (68) + px (96)));
    };
    const auto half = c.getWidth() / 2;
    placeKnob (referenceKnob, c.removeFromLeft (half).withTrimmedRight (px (12)));
    placeKnob (peakKnob, c);
}

void VuMeterAudioProcessorEditor::resetHolds()
{
    ledOffTime.fill (0.0);
    vuProcessor.resetMaxReadings();
}

void VuMeterAudioProcessorEditor::timerCallback()
{
    const auto channels = vuProcessor.getNumMeteredChannels();
    if (channels != shownChannels)
    {
        shownChannels = channels;
        resized();
    }

    const auto now       = juce::Time::getMillisecondCounterHiRes();
    const auto ledThresh = juce::Decibels::decibelsToGain (vuProcessor.getPeakLedDbfs());
    const auto refText   = "0 VU = " + juce::String (vuProcessor.getReferenceDbfs(), 1) + " dBFS";

    for (int ch = 0; ch < shownChannels; ++ch)
    {
        if (vuProcessor.takePeak (ch) >= ledThresh)
            ledOffTime[(size_t) ch] = now + ledHoldMs;

        auto* meter = meters[(size_t) ch];
        meter->setReferenceText (refText);
        meter->setState (vuProcessor.getVuRatio (ch), now < ledOffTime[(size_t) ch]);

        float vu = 0.0f;
        const bool valid = vuProcessor.getMaxReadingVu (ch, vu);
        meter->setMaxReading (valid, vu);
    }
}
