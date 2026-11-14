#include "PluginEditor.h"
#include "Presets.h"

using namespace albion;

namespace
{
    constexpr int baseWidth  = 1000;
    constexpr int baseHeight = 460;
    constexpr int headerH    = 58;

    constexpr float knobStart = juce::degreesToRadians (225.0f);
    constexpr float knobEnd   = juce::degreesToRadians (495.0f);
    constexpr float scaleMarks[] = { 0.2f, 0.5f, 1.0f, 1.5f, 2.0f, 3.0f };

    const char* groupNames[] = { "BASS", "DISTORTED RHYTHM", "DISTORTED LEAD" };
    const char* groupKeys[]  = { "BASS", "RHYTHM", "LEAD" };

    juce::String zoneText (const Zone& z, juce::Slider& s)
    {
        return s.getTextFromValue (z.lo) + juce::String::fromUTF8 (" \xe2\x80\x93 ") + s.getTextFromValue (z.hi);
    }
}

AlbionGuitarReverbAudioProcessorEditor::AlbionGuitarReverbAudioProcessorEditor (AlbionGuitarReverbAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);

    themeToggle.onClick = [this] { albion::switchTheme (*this, lnf); };
    addAndMakeVisible (themeToggle);

    // --- Decay: big knob, seconds scale drawn in paint(), editable readout -------
    decayKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    decayKnob.setRotaryParameters (knobStart, knobEnd, true);
    decayKnob.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    decayKnob.setMouseDragSensitivity (320);
    decayKnob.getProperties().set ("customScale", true);
    decayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, ids::decay.getParamID(), decayKnob);
    addAndMakeVisible (decayKnob);

    decayLabel.setText ("DECAY", juce::dontSendNotification);
    decayLabel.setJustificationType (juce::Justification::centred);
    decayLabel.setColour (juce::Label::textColourId, colours::brass);
    addAndMakeVisible (decayLabel);

    decayValue.setJustificationType (juce::Justification::centred);
    decayValue.setColour (juce::Label::textColourId, colours::cream);
    decayValue.setColour (juce::Label::textWhenEditingColourId, colours::cream);
    decayValue.setColour (juce::Label::backgroundWhenEditingColourId, colours::panelDark);
    decayValue.setColour (juce::Label::outlineWhenEditingColourId, colours::brass);
    decayValue.setEditable (false, true);   // double-click to type a value
    decayValue.onTextChange = [this] { decayKnob.setValue (decayValue.getText().getDoubleValue(), juce::sendNotificationSync); };
    addAndMakeVisible (decayValue);
    decayKnob.onValueChange = [this] { decayValue.setText (decayKnob.getTextFromValue (decayKnob.getValue()), juce::dontSendNotification); };
    decayKnob.onValueChange();

    setupKnob (knobs[kPreDelay], ids::predelay, "PRE-DELAY");
    setupKnob (knobs[kLowCut],   ids::lowcut,   "LOW CUT");
    setupKnob (knobs[kHighCut],  ids::highcut,  "HIGH CUT");
    setupKnob (knobs[kWidth],    ids::width,    "WIDTH");
    setupKnob (knobs[kDuck],     ids::duck,     "DUCK");
    setupKnob (knobs[kMix],      ids::mix,      "MIX");
    knobs[kLowCut].slider.setTooltip ("Steep (24 dB/oct) high-pass before the reverb");
    knobs[kHighCut].slider.setTooltip ("Low-pass before the reverb (12 dB/oct)");
    knobs[kDuck].slider.setTooltip ("Turns the reverb down while you play and lets it bloom in the gaps");
    knobs[kMix].slider.setTooltip ("Up to 50%: dry stays at unity. 100%: reverb only (use on a send / FX track)");

    // --- voice buttons ------------------------------------------------------------
    voiceLabel.setText ("VOICE", juce::dontSendNotification);
    voiceLabel.setJustificationType (juce::Justification::centredLeft);
    voiceLabel.setColour (juce::Label::textColourId, colours::brass);
    addAndMakeVisible (voiceLabel);

    for (int g = 0; g < 3; ++g)
    {
        auto& l = groupLabels[(size_t) g];
        l.setText (groupNames[g], juce::dontSendNotification);
        l.setJustificationType (juce::Justification::bottomLeft);
        l.setColour (juce::Label::textColourId, colours::muted);
        addAndMakeVisible (l);
    }

    blurb.setJustificationType (juce::Justification::topLeft);
    blurb.setColour (juce::Label::textColourId, colours::muted);
    blurb.setMinimumHorizontalScale (1.0f);
    addAndMakeVisible (blurb);

    auto* voiceParam = proc.apvts.getParameter (ids::voice.getParamID());
    voiceAttachment = std::make_unique<juce::ParameterAttachment> (*voiceParam, [this] (float index)
    {
        const auto idx = juce::jlimit (0, voices::count - 1, juce::roundToInt (index));
        for (int i = 0; i < voices::count; ++i)
            voiceButtons[(size_t) i].setToggleState (i == idx, juce::dontSendNotification);
        blurb.setText (voices::table[(size_t) idx].blurb, juce::dontSendNotification);
        shownVoice = idx;
        repaint();
    });

    for (int i = 0; i < voices::count; ++i)
    {
        auto& b = voiceButtons[(size_t) i];
        b.setButtonText (juce::String::fromUTF8 (voices::table[(size_t) i].button));
        b.setTooltip (juce::String (voices::table[(size_t) i].name) + ": click to load its recommended settings");
        b.onClick = [this, i] { proc.selectVoice (i); };
        addAndMakeVisible (b);
    }
    voiceAttachment->sendInitialUpdate();

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
            proc.setCurrentProgram (idx);
        }
    };
    addAndMakeVisible (presetBox);

    setResizable (true, true);
    setResizeLimits (baseWidth * 2 / 3, baseHeight * 2 / 3, baseWidth * 2, baseHeight * 2);
    getConstrainer()->setFixedAspectRatio ((double) baseWidth / (double) baseHeight);
    setSize (baseWidth, baseHeight);

    startTimerHz (10);   // only watches for host-side preset changes
}

AlbionGuitarReverbAudioProcessorEditor::~AlbionGuitarReverbAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void AlbionGuitarReverbAudioProcessorEditor::setupKnob (Knob& k, const juce::ParameterID& id, const juce::String& name)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setRotaryParameters (knobStart, knobEnd, true);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 18);
    addAndMakeVisible (k.slider);

    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setColour (juce::Label::textColourId, colours::brass);
    addAndMakeVisible (k.label);

    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id.getParamID(), k.slider);

    auto* param = proc.apvts.getParameter (id.getParamID());
    k.slider.setDoubleClickReturnValue (true, (double) param->convertFrom0to1 (param->getDefaultValue()));
}

// Brass arc around a knob marking the current voice's recommended zone.
void AlbionGuitarReverbAudioProcessorEditor::drawZone (juce::Graphics& g, juce::Slider& s, const Zone& z,
                                                       float radiusScale, float thickness)
{
    auto rotary = s.getLookAndFeel().getSliderLayout (s).sliderBounds.toFloat() + s.getPosition().toFloat();
    const auto centre = rotary.getCentre();
    const auto radius = juce::jmin (rotary.getWidth(), rotary.getHeight()) * 0.5f * radiusScale;
    auto angleFor = [&s] (double v) { return knobStart + (float) s.valueToProportionOfLength (v) * (knobEnd - knobStart); };

    juce::Path arc;
    arc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, angleFor (z.lo), angleFor (z.hi), true);
    g.setColour (colours::brass.withAlpha (0.5f));
    g.strokePath (arc, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void AlbionGuitarReverbAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto s = (float) getHeight() / (float) baseHeight;
    auto bounds = getLocalBounds().toFloat();

    g.setGradientFill (juce::ColourGradient (colours::panelLight, 0.0f, 0.0f,
                                             colours::panel, 0.0f, bounds.getBottom(), false));
    g.fillAll();

    auto header = bounds.removeFromTop ((float) headerH * s);
    albion::drawHeader (g, header, juce::String::fromUTF8 ("Guitar Reverb  \xc2\xb7  Bass \xc2\xb7 Rhythm \xc2\xb7 Lead"), s);

    g.setColour (colours::panelDark.withAlpha (0.35f));
    for (auto r : { voicePanel, decayPanel, knobPanel })
        g.fillRoundedRectangle (r.toFloat(), 8.0f * s);

    const auto& voice = voices::table[(size_t) juce::jlimit (0, voices::count - 1, shownVoice)];

    // --- Decay scale: zone arc, ticks and second labels --------------------------
    const auto kb     = decayKnob.getBounds().toFloat();
    const auto centre = kb.getCentre();
    const auto radius = juce::jmin (kb.getWidth(), kb.getHeight()) * 0.5f;
    auto angleFor = [this] (double v) { return knobStart + (float) decayKnob.valueToProportionOfLength (v) * (knobEnd - knobStart); };

    drawZone (g, decayKnob, voice.decay, 1.04f, 3.0f * s);

    g.setFont (juce::Font (juce::FontOptions (12.0f * s, juce::Font::bold)));
    for (auto mark : scaleMarks)
    {
        const auto a = angleFor (mark);
        g.setColour (colours::cream.withAlpha (0.7f));
        g.drawLine (juce::Line<float> (centre.getPointOnCircumference (radius * 0.90f, a),
                                       centre.getPointOnCircumference (radius * 0.98f, a)), 1.5f * s);
        g.setColour (colours::cream.withAlpha (0.85f));
        g.drawText (juce::String (mark, (mark < 0.9f || (mark > 1.4f && mark < 1.6f)) ? 1 : 0),
                    juce::Rectangle<float> (34.0f * s, 14.0f * s).withCentre (centre.getPointOnCircumference (radius * 1.2f, a)),
                    juce::Justification::centred, false);
    }

    // suggested range under the readout
    g.setColour (colours::muted);
    g.setFont (albion::mono (11.0f * s));
    auto hint = decayValue.getBounds().toFloat().translated (0.0f, decayValue.getHeight() * 0.85f);
    g.drawText ("suggested " + zoneText (voice.decay, decayKnob), hint, juce::Justification::centred, false);

    // --- zones on the small knobs ------------------------------------------------
    drawZone (g, knobs[kPreDelay].slider, voice.preDelay, 1.03f, 2.5f * s);
    drawZone (g, knobs[kLowCut].slider,   voice.lowCut,   1.03f, 2.5f * s);
    drawZone (g, knobs[kHighCut].slider,  voice.highCut,  1.03f, 2.5f * s);

    // legend
    auto legend = knobPanel.toFloat().reduced (12.0f * s, 6.0f * s).removeFromBottom (14.0f * s);
    g.setColour (colours::brass.withAlpha (0.5f));
    g.fillRoundedRectangle (legend.removeFromLeft (16.0f * s).withSizeKeepingCentre (16.0f * s, 3.0f * s), 1.5f * s);
    legend.removeFromLeft (6.0f * s);
    g.setColour (colours::muted);
    g.setFont (albion::mono (10.5f * s));
    g.drawText ("recommended zone for the selected voice",
                legend, juce::Justification::centredLeft, true);
}

void AlbionGuitarReverbAudioProcessorEditor::resized()
{
    const auto s = (float) getHeight() / (float) baseHeight;
    lnf.uiScale = s;
    auto px = [s] (float v) { return juce::roundToInt (v * s); };

    auto area = getLocalBounds();
    auto header = area.removeFromTop (px ((float) headerH));
    themeToggle.setBounds (header.removeFromRight (px (50)).withSizeKeepingCentre (px (30), px (30)));
    presetBox.setBounds (header.removeFromRight (px (300)).reduced (px (16), px (14)));

    auto body = area.reduced (px (14));
    voicePanel = body.removeFromLeft (juce::roundToInt ((float) body.getWidth() * 0.31f));
    body.removeFromLeft (px (12));
    decayPanel = body.removeFromLeft (juce::roundToInt ((float) body.getWidth() * 0.37f));
    body.removeFromLeft (px (12));
    knobPanel = body;

    const auto captionFont = juce::Font (juce::FontOptions (13.0f * s, juce::Font::bold)).withExtraKerningFactor (0.12f);

    // --- voices -------------------------------------------------------------------
    {
        auto r = voicePanel.reduced (px (12), px (8));
        voiceLabel.setFont (juce::Font (juce::FontOptions (15.0f * s, juce::Font::bold)).withExtraKerningFactor (0.18f));
        voiceLabel.setBounds (r.removeFromTop (px (22)));

        const auto rowH = px (36), gap = px (6), capH = px (26);
        auto row = [&] { auto x = r.removeFromTop (rowH); r.removeFromTop (gap); return x; };

        for (int grp = 0; grp < 3; ++grp)
        {
            groupLabels[(size_t) grp].setFont (albion::mono (10.5f * s));
            groupLabels[(size_t) grp].setBounds (r.removeFromTop (capH));

            std::vector<int> members;
            for (int i = 0; i < voices::count; ++i)
                if (juce::String (voices::table[(size_t) i].instrument) == groupKeys[grp])
                    members.push_back (i);

            for (size_t m = 0; m < members.size(); m += 2)
            {
                auto line = row();
                if (m + 1 < members.size())
                {
                    voiceButtons[(size_t) members[m]].setBounds (line.removeFromLeft (line.getWidth() / 2 - gap / 2));
                    line.removeFromLeft (gap);
                    voiceButtons[(size_t) members[m + 1]].setBounds (line);
                }
                else
                {
                    voiceButtons[(size_t) members[m]].setBounds (line);
                }
            }
        }
        r.removeFromTop (px (4));
        blurb.setFont (juce::Font (juce::FontOptions (12.5f * s)));
        blurb.setBounds (r);
    }

    // --- decay --------------------------------------------------------------------
    {
        auto r = decayPanel.reduced (px (10), px (8));
        decayLabel.setFont (juce::Font (juce::FontOptions (15.0f * s, juce::Font::bold)).withExtraKerningFactor (0.18f));
        decayLabel.setBounds (r.removeFromTop (px (22)));
        r.removeFromBottom (px (16));   // suggested-range hint
        decayValue.setFont (juce::Font (juce::FontOptions (22.0f * s, juce::Font::bold)));
        decayValue.setBounds (r.removeFromBottom (px (28)));
        const auto knobSize = juce::jmin (r.getWidth(), r.getHeight()) - px (44);
        decayKnob.setBounds (r.withSizeKeepingCentre (knobSize, knobSize));
    }

    // --- 3 x 2 knob grid ------------------------------------------------------------
    auto grid = knobPanel.reduced (px (8), px (6));
    grid.removeFromBottom (px (16));   // legend
    const auto cellW = grid.getWidth() / 3;
    const auto cellH = grid.getHeight() / 2;
    auto cell = [&] (int col, int row) { return juce::Rectangle<int> (grid.getX() + col * cellW, grid.getY() + row * cellH, cellW, cellH); };

    const auto labelH = px (20);
    auto placeKnob = [&] (Knob& k, juce::Rectangle<int> r)
    {
        k.label.setFont (captionFont);
        k.label.setBounds (r.removeFromTop (labelH));
        r.removeFromTop (px (4));
        k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, px (90), px (18));
        k.slider.setBounds (r.reduced (px (6), 0).withTrimmedBottom (px (10)));
    };

    placeKnob (knobs[kPreDelay], cell (0, 0));
    placeKnob (knobs[kLowCut],   cell (1, 0));
    placeKnob (knobs[kHighCut],  cell (2, 0));
    placeKnob (knobs[kWidth],    cell (0, 1));
    placeKnob (knobs[kDuck],     cell (1, 1));
    placeKnob (knobs[kMix],      cell (2, 1));
}

void AlbionGuitarReverbAudioProcessorEditor::timerCallback()
{
    const auto program = proc.getCurrentProgram();
    if (program != shownPreset)
    {
        shownPreset = program;
        presetBox.setSelectedId (program + 1, juce::dontSendNotification);
    }
}
