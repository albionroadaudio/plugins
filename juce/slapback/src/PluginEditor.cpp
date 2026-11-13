#include "PluginEditor.h"
#include "Presets.h"

using namespace albion;

namespace
{
    constexpr int baseWidth  = 900;
    constexpr int baseHeight = 380;
    constexpr int headerH    = 58;

    const char* modeNames[] = { "TAPE", "BBD", "DIGITAL" };

    constexpr float knobStart = juce::degreesToRadians (225.0f);
    constexpr float knobEnd   = juce::degreesToRadians (495.0f);
    constexpr int   scaleMarks[] = { 30, 50, 75, 100, 150, 200, 300 };
}

AlbionSlapbackAudioProcessorEditor::AlbionSlapbackAudioProcessorEditor (AlbionSlapbackAudioProcessor& p)
    : AudioProcessorEditor (&p), slap (p)
{
    setLookAndFeel (&lnf);

    themeToggle.onClick = [this] { albion::switchTheme (*this, lnf); };
    addAndMakeVisible (themeToggle);

    // --- Delay Time: big knob, ms scale drawn in paint(), editable readout ------
    timeKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    timeKnob.setRotaryParameters (knobStart, knobEnd, true);
    timeKnob.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    timeKnob.setMouseDragSensitivity (320);
    timeKnob.getProperties().set ("customScale", true);
    timeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (slap.apvts, ids::time.getParamID(), timeKnob);
    timeKnob.setDoubleClickReturnValue (true, 120.0);
    addAndMakeVisible (timeKnob);

    timeLabel.setText ("DELAY TIME", juce::dontSendNotification);
    timeLabel.setJustificationType (juce::Justification::centred);
    timeLabel.setColour (juce::Label::textColourId, colours::brass);
    addAndMakeVisible (timeLabel);

    timeValue.setJustificationType (juce::Justification::centred);
    timeValue.setColour (juce::Label::textColourId, colours::cream);
    timeValue.setColour (juce::Label::textWhenEditingColourId, colours::cream);
    timeValue.setColour (juce::Label::backgroundWhenEditingColourId, colours::panelDark);
    timeValue.setColour (juce::Label::outlineWhenEditingColourId, colours::brass);
    timeValue.setEditable (false, true);   // double-click to type a value
    timeValue.onTextChange = [this] { timeKnob.setValue (timeValue.getText().getDoubleValue(), juce::sendNotificationSync); };
    addAndMakeVisible (timeValue);
    timeKnob.onValueChange = [this] { timeValue.setText (timeKnob.getTextFromValue (timeKnob.getValue()), juce::dontSendNotification); };
    timeKnob.onValueChange();

    setupKnob (knobs[kFeedback], ids::feedback, "FEEDBACK");
    setupKnob (knobs[kTone],     ids::tone,     "TONE");
    setupKnob (knobs[kLowCut],   ids::lowcut,   "LOW CUT");
    setupKnob (knobs[kDrive],    ids::drive,    "DRIVE");
    setupKnob (knobs[kWow],      ids::wow,      "WOW / FLUTTER");
    setupKnob (knobs[kSpread],   ids::spread,   "SPREAD");
    setupKnob (knobs[kMix],      ids::mix,      "MIX");

    // --- mode push-buttons ------------------------------------------------------
    modeLabel.setText ("MODE", juce::dontSendNotification);
    modeLabel.setJustificationType (juce::Justification::centred);
    modeLabel.setColour (juce::Label::textColourId, colours::brass);
    addAndMakeVisible (modeLabel);

    auto* modeParam = slap.apvts.getParameter (ids::mode.getParamID());
    modeAttachment = std::make_unique<juce::ParameterAttachment> (*modeParam, [this] (float index)
    {
        const auto idx = juce::jlimit (0, 2, juce::roundToInt (index));
        for (int i = 0; i < (int) modeButtons.size(); ++i)
            modeButtons[(size_t) i].setToggleState (i == idx, juce::dontSendNotification);
    });

    for (int i = 0; i < (int) modeButtons.size(); ++i)
    {
        auto& b = modeButtons[(size_t) i];
        b.setButtonText (modeNames[i]);
        b.onClick = [this, i] { modeAttachment->setValueAsCompleteGesture ((float) i); };
        addAndMakeVisible (b);
    }
    modeAttachment->sendInitialUpdate();

    // --- presets ----------------------------------------------------------------
    for (int i = 0; i < (int) presets::factory.size(); ++i)
        presetBox.addItem (presets::factory[(size_t) i].name, i + 1);
    presetBox.setTextWhenNothingSelected ("Presets");
    presetBox.onChange = [this]
    {
        const auto idx = presetBox.getSelectedId() - 1;
        if (idx >= 0 && idx != shownPreset)
        {
            shownPreset = idx;
            slap.setCurrentProgram (idx);
        }
    };
    addAndMakeVisible (presetBox);

    setResizable (true, true);
    setResizeLimits (baseWidth * 2 / 3, baseHeight * 2 / 3, baseWidth * 2, baseHeight * 2);
    getConstrainer()->setFixedAspectRatio ((double) baseWidth / (double) baseHeight);
    setSize (baseWidth, baseHeight);

    startTimerHz (10);   // only watches for host-side preset changes
}

AlbionSlapbackAudioProcessorEditor::~AlbionSlapbackAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void AlbionSlapbackAudioProcessorEditor::setupKnob (Knob& k, const juce::ParameterID& id, const juce::String& name)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 18);
    addAndMakeVisible (k.slider);

    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setColour (juce::Label::textColourId, colours::brass);
    addAndMakeVisible (k.label);

    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (slap.apvts, id.getParamID(), k.slider);

    auto* param = slap.apvts.getParameter (id.getParamID());
    k.slider.setDoubleClickReturnValue (true, (double) param->convertFrom0to1 (param->getDefaultValue()));
}

void AlbionSlapbackAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto s = (float) getHeight() / (float) baseHeight;
    auto bounds = getLocalBounds().toFloat();

    g.setGradientFill (juce::ColourGradient (colours::panelLight, 0.0f, 0.0f,
                                             colours::panel, 0.0f, bounds.getBottom(), false));
    g.fillAll();

    // header: shared Albion Road Audio header (logo, wordmark, product)
    auto header = bounds.removeFromTop ((float) headerH * s);
    albion::drawHeader (g, header, juce::String::fromUTF8 ("Slapback  ·  Tape / BBD / Digital echo"), s);

    auto body = getLocalBounds().toFloat().withTrimmedTop ((float) headerH * s).reduced (14.0f * s);
    auto knobArea = body.withTrimmedLeft (body.getWidth() * 0.37f + 14.0f * s);
    g.setColour (colours::panelDark.withAlpha (0.35f));
    g.fillRoundedRectangle (knobArea, 8.0f * s);
    g.fillRoundedRectangle (timePanel.toFloat(), 8.0f * s);

    // --- Delay Time scale: sweet-spot arc, ticks and ms labels ---------------
    const auto kb     = timeKnob.getBounds().toFloat();
    const auto centre = kb.getCentre();
    const auto radius = juce::jmin (kb.getWidth(), kb.getHeight()) * 0.5f;
    auto angleFor = [this] (double ms) { return knobStart + (float) timeKnob.valueToProportionOfLength (ms) * (knobEnd - knobStart); };

    const auto arcR = radius * 1.04f;
    juce::Path sweet;
    sweet.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, angleFor (80.0), angleFor (180.0), true);
    g.setColour (colours::brass.withAlpha (0.45f));
    g.strokePath (sweet, juce::PathStrokeType (3.0f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setFont (juce::Font (juce::FontOptions (12.0f * s, juce::Font::bold)));
    for (auto ms : scaleMarks)
    {
        const auto a = angleFor (ms);
        g.setColour (colours::cream.withAlpha (0.7f));
        g.drawLine (juce::Line<float> (centre.getPointOnCircumference (radius * 0.90f, a),
                                       centre.getPointOnCircumference (radius * 0.98f, a)), 1.5f * s);
        g.setColour (colours::cream.withAlpha (0.85f));
        g.drawText (juce::String (ms), juce::Rectangle<float> (34.0f * s, 14.0f * s)
                                           .withCentre (centre.getPointOnCircumference (radius * 1.2f, a)),
                    juce::Justification::centred, false);
    }

}

void AlbionSlapbackAudioProcessorEditor::resized()
{
    const auto s = (float) getHeight() / (float) baseHeight;
    lnf.uiScale = s;
    auto px = [s] (float v) { return juce::roundToInt (v * s); };

    auto area = getLocalBounds();
    auto header = area.removeFromTop (px ((float) headerH));
    themeToggle.setBounds (header.removeFromRight (px (50)).withSizeKeepingCentre (px (30), px (30)));
    presetBox.setBounds (header.removeFromRight (px (290)).reduced (px (16), px (14)));

    auto body = area.reduced (px (14));
    auto dialArea = body.removeFromLeft (juce::roundToInt ((float) body.getWidth() * 0.37f));
    body.removeFromLeft (px (14));
    timePanel = dialArea;
    {
        auto r = dialArea.reduced (px (10), px (8));
        timeLabel.setFont (juce::Font (juce::FontOptions (15.0f * s, juce::Font::bold)).withExtraKerningFactor (0.18f));
        timeLabel.setBounds (r.removeFromTop (px (22)));
        timeValue.setFont (juce::Font (juce::FontOptions (20.0f * s, juce::Font::bold)));
        timeValue.setBounds (r.removeFromBottom (px (28)));
        // leave room around the knob for the ms labels
        const auto knobSize = juce::jmin (r.getWidth(), r.getHeight()) - px (44);
        timeKnob.setBounds (r.withSizeKeepingCentre (knobSize, knobSize));
    }

    // 4 x 2 grid: [Feedback, Mode, Tone, Low Cut] / [Drive, Wow/Flutter, Spread, Mix]
    auto grid = body.reduced (px (8), px (6));
    const auto cellW = grid.getWidth() / 4;
    const auto cellH = grid.getHeight() / 2;
    auto cell = [&] (int col, int row) { return juce::Rectangle<int> (grid.getX() + col * cellW, grid.getY() + row * cellH, cellW, cellH); };

    const auto labelH = px (18);
    const auto fontH  = 13.0f * s;
    auto placeKnob = [&] (Knob& k, juce::Rectangle<int> r)
    {
        k.label.setFont (juce::Font (juce::FontOptions (fontH, juce::Font::bold)).withExtraKerningFactor (0.12f));
        k.label.setBounds (r.removeFromTop (labelH));
        k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, px (90), px (18));
        k.slider.setBounds (r.reduced (px (6), 0));
    };

    placeKnob (knobs[kFeedback], cell (0, 0));
    placeKnob (knobs[kTone],     cell (2, 0));
    placeKnob (knobs[kLowCut],   cell (3, 0));
    placeKnob (knobs[kDrive],    cell (0, 1));
    placeKnob (knobs[kWow],      cell (1, 1));
    placeKnob (knobs[kSpread],   cell (2, 1));
    placeKnob (knobs[kMix],      cell (3, 1));

    auto modeCell = cell (1, 0);
    modeLabel.setFont (juce::Font (juce::FontOptions (fontH, juce::Font::bold)).withExtraKerningFactor (0.12f));
    modeLabel.setBounds (modeCell.removeFromTop (labelH));
    auto buttons = modeCell.reduced (px (16), px (10));
    const auto bh = buttons.getHeight() / (int) modeButtons.size();
    for (auto& b : modeButtons)
        b.setBounds (buttons.removeFromTop (bh).reduced (0, px (3)));
}

void AlbionSlapbackAudioProcessorEditor::timerCallback()
{
    const auto program = slap.getCurrentProgram();
    if (program != shownPreset)
    {
        shownPreset = program;
        presetBox.setSelectedId (program + 1, juce::dontSendNotification);
    }
}
