#include "PluginEditor.h"
#include "Presets.h"

using namespace albion;

namespace
{
    constexpr int baseWidth  = 1180;
    constexpr int baseHeight = 430;
    constexpr int headerH    = 58;

    constexpr float knobStart = juce::degreesToRadians (225.0f);
    constexpr float knobEnd   = juce::degreesToRadians (495.0f);

    // meter ballistics (GUI side)
    constexpr double needleAttackS  = 0.010;
    constexpr double needleReleaseS = 0.250;
    constexpr double holdSeconds    = 1.5;

    struct SectionText { const char* title; const char* caption; float weight; const juce::ParameterID* onId; const char* tip; };
    const SectionText sections[] = {
        { "DENOISE",    "spectral gate \xc2\xb7 -60 dB",                1.08f, &ids::denoiseOn, "Denoiser on/off" },
        { "HIGH-PASS",  "120 Hz \xc2\xb7 24 dB/oct",                    1.22f, &ids::hpf,       "High-pass filter on/off (120 Hz, 24 dB/oct)" },
        { "COMPRESSOR", "1 ms \xc2\xb7 80 ms \xc2\xb7 4:1",              1.30f, &ids::comp1On,   "Compressor on/off" },
        { "DE-ESSER",   "above 4.5 kHz",                                 1.22f, &ids::deessOn,   "De-esser on/off" },
        { "EQ",         "wide cut \xc2\xb7 narrow cut \xc2\xb7 shelf",      1.55f, &ids::eqOn,      "EQ on/off" },
        { "GLUE",       "30 ms \xc2\xb7 30 ms \xc2\xb7 -60 dB",           1.30f, &ids::comp2On,   "Glue compressor on/off" },
        { "OUTPUT",     "trim",                                          0.78f, nullptr,         nullptr },
    };
}

void PowerSwitch::paintButton (juce::Graphics& g, bool over, bool)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const auto track = r.withSizeKeepingCentre (r.getWidth(), juce::jmin (r.getHeight(), r.getWidth() * 0.56f));
    const auto rad = track.getHeight() * 0.5f;
    const auto on = getToggleState();

    g.setColour (on ? colours::brass : colours::panelDark);
    g.fillRoundedRectangle (track, rad);
    g.setColour (on ? colours::brass : (over ? colours::brass : colours::lineDark));
    g.drawRoundedRectangle (track, rad, 1.0f);

    const auto knobD = track.getHeight() - 4.0f;
    const auto x = on ? track.getRight() - 2.0f - knobD : track.getX() + 2.0f;
    g.setColour (on ? colours::panel : colours::muted);
    g.fillEllipse (x, track.getY() + 2.0f, knobD, knobD);
}

namespace
{
}

VocalMultitoolEditor::VocalMultitoolEditor (VocalMultitoolProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);

    themeToggle.onClick = [this] { albion::switchTheme (*this, lnf); };
    addAndMakeVisible (themeToggle);

    setupKnob (knobs[kDenoise], ids::denoise, "AMOUNT",
               "How hard the noise floor is pulled down (100 % = up to 24 dB). Only sound below the -60 dB threshold is touched");
    setupKnob (knobs[kClamp],   ids::comp1,   "AMOUNT",
               "100 % = the recipe: -10 dB on the loudest peaks, +10 dB makeup. The threshold follows your vocal automatically");
    setupKnob (knobs[kDeEss],   ids::deess,   "AMOUNT",
               "How hard \"s\" and \"t\" sounds are turned down (up to 12 dB above 4.5 kHz)");
    setupKnob (knobs[kMud],     ids::mud,     "MUD",   "Wide cut at 200 Hz: clears room muddiness");
    setupKnob (knobs[kNasal],   ids::nasal,   "NASAL", "Narrow cut at 2.6 kHz: removes nasal or piercing tones");
    setupKnob (knobs[kAir],     ids::air,     "AIR",   "Gentle high-shelf boost at 11 kHz: air and clarity");
    setupKnob (knobs[kGlue],    ids::comp2,   "AMOUNT",
               "100 % = the recipe: about -5 dB of gentle, low-threshold compression with makeup to match");
    setupKnob (knobs[kOutput],  ids::output,  "LEVEL", "Output trim");

    for (auto* m : { &clampMeter, &deEssMeter, &glueMeter })
        addAndMakeVisible (*m);

    for (int i = 0; i < kNumSections - 1; ++i)
    {
        auto& sw = switches[(size_t) i];
        sw.setTooltip (sections[i].tip);
        sw.onStateChange = [this, i] { sectionSwitched (i); };
        switchAttachments[(size_t) i] = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            proc.apvts, sections[i].onId->getParamID(), sw);
        addAndMakeVisible (sw);
        sectionSwitched (i);
    }

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

    lastFrameMs = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (60);
}

VocalMultitoolEditor::~VocalMultitoolEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

std::vector<juce::Component*> VocalMultitoolEditor::componentsOf (int section)
{
    auto knob = [this] (KnobIndex k) -> std::vector<juce::Component*> { return { &knobs[(size_t) k].slider, &knobs[(size_t) k].label }; };
    switch (section)
    {
        case sDenoise: return knob (kDenoise);
        case sClamp:   { auto c = knob (kClamp); c.push_back (&clampMeter); return c; }
        case sDeEss:   { auto c = knob (kDeEss); c.push_back (&deEssMeter); return c; }
        case sEq:      { auto c = knob (kMud); for (auto k : { kNasal, kAir }) for (auto* x : knob (k)) c.push_back (x); return c; }
        case sGlue:    { auto c = knob (kGlue); c.push_back (&glueMeter); return c; }
        default:       return {};
    }
}

// A switched-off section stays usable but is drawn faded.
void VocalMultitoolEditor::sectionSwitched (int section)
{
    const auto on = switches[(size_t) section].getToggleState();
    for (auto* c : componentsOf (section))
        c->setAlpha (on ? 1.0f : 0.35f);
    repaint (panels[(size_t) section]);
}

void VocalMultitoolEditor::setupKnob (Knob& k, const juce::ParameterID& id, const juce::String& name, const juce::String& tip)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setRotaryParameters (knobStart, knobEnd, true);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 18);
    k.slider.setTooltip (tip);
    addAndMakeVisible (k.slider);

    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setColour (juce::Label::textColourId, colours::muted);
    addAndMakeVisible (k.label);

    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id.getParamID(), k.slider);

    auto* param = proc.apvts.getParameter (id.getParamID());
    k.slider.setDoubleClickReturnValue (true, (double) param->convertFrom0to1 (param->getDefaultValue()));
}

void VocalMultitoolEditor::paint (juce::Graphics& g)
{
    const auto s = (float) getHeight() / (float) baseHeight;
    auto bounds = getLocalBounds().toFloat();

    g.setGradientFill (juce::ColourGradient (colours::panelLight, 0.0f, 0.0f,
                                             colours::panel, 0.0f, bounds.getBottom(), false));
    g.fillAll();

    auto header = bounds.removeFromTop ((float) headerH * s);
    albion::drawHeader (g, header, juce::String::fromUTF8 ("Vocal Multitool  \xc2\xb7  six-stage vocal chain"), s);

    for (int i = 0; i < kNumSections; ++i)
    {
        const auto r = panels[(size_t) i].toFloat();
        const auto& t = sections[i];
        const auto hasSwitch = t.onId != nullptr;
        const auto on = ! hasSwitch || switches[(size_t) i].getToggleState();
        g.setColour (colours::panelDark.withAlpha (i == sOutput || ! on ? 0.2f : 0.35f));
        g.fillRoundedRectangle (r, 8.0f * s);

        // title row: on/off switch (placed in resized) + name
        g.setColour (hasSwitch && on ? colours::cream : colours::muted);
        g.setFont (albion::sans (14.0f * s, true).withExtraKerningFactor (0.14f));
        g.drawFittedText (t.title, titleRows[(size_t) i], hasSwitch ? juce::Justification::centredLeft : juce::Justification::centred, 1, 0.7f);

        // caption: the stage's fixed settings
        auto caption = r.reduced (8.0f * s, 8.0f * s).removeFromBottom (16.0f * s);
        g.setColour (colours::muted);
        g.setFont (albion::mono (10.5f * s));
        g.drawFittedText (juce::String::fromUTF8 (t.caption), caption.toNearestInt(), juce::Justification::centred, 1, 0.8f);

        // signal-flow chevron in the gap to the next panel
        if (i + 1 < kNumSections)
        {
            const auto next = panels[(size_t) i + 1].toFloat();
            const auto cx = (r.getRight() + next.getX()) * 0.5f, cy = r.getCentreY();
            juce::Path chevron;
            chevron.startNewSubPath (cx - 2.5f * s, cy - 5.0f * s);
            chevron.lineTo (cx + 2.5f * s, cy);
            chevron.lineTo (cx - 2.5f * s, cy + 5.0f * s);
            g.setColour (colours::muted.withAlpha (0.7f));
            g.strokePath (chevron, juce::PathStrokeType (1.6f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    // --- EQ band frequencies under the knobs ---------------------------------------------
    {
        const char* freqs[] = { "200 Hz", "2.6 kHz", "11 kHz" };
        g.setFont (albion::mono (10.5f * s));
        g.setColour (colours::brass);
        int i = 0;
        for (auto idx : { kMud, kNasal, kAir })
        {
            const auto kb = knobs[(size_t) idx].slider.getBounds();
            g.drawText (freqs[i++], juce::Rectangle<int> (kb.getX(), kb.getBottom() + juce::roundToInt (2.0f * s), kb.getWidth(), juce::roundToInt (14.0f * s)),
                        juce::Justification::centred, false);
        }
    }

    // --- high-pass response sketch -------------------------------------------------
    {
        const auto area = hpfCurveArea.toFloat();
        const auto on = switches[sHpf].getToggleState();
        g.setColour (colours::dark);
        g.fillRoundedRectangle (area, 5.0f * s);

        const auto plot = area.reduced (6.0f * s);
        auto xFor = [&] (double f) { return plot.getX() + plot.getWidth() * (float) (std::log (f / 20.0) / std::log (1000.0)); };
        auto yFor = [&] (double db) { return plot.getY() + plot.getHeight() * (float) juce::jlimit (0.0, 1.0, (6.0 - db) / 42.0); };

        g.setColour (juce::Colours::white.withAlpha (0.08f));
        for (double f : { 100.0, 1000.0, 10000.0 })
            g.fillRect (juce::Rectangle<float> (xFor (f), plot.getY(), 1.0f, plot.getHeight()));

        juce::Path curve;
        for (int i = 0; i <= 80; ++i)
        {
            const auto f = 20.0 * std::pow (1000.0, i / 80.0);
            const auto ratio = f / chain::hpfHz;
            const auto db = on ? -10.0 * std::log10 (1.0 + std::pow (ratio, -8.0)) : 0.0;   // 4th-order Butterworth
            const auto pt = juce::Point<float> (xFor (f), yFor (db));
            if (i == 0) curve.startNewSubPath (pt); else curve.lineTo (pt);
        }
        g.setColour (on ? colours::peach : colours::muted);
        g.strokePath (curve, juce::PathStrokeType (2.0f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.setFont (albion::mono (9.0f * s));
        g.drawText ("120", juce::Rectangle<float> (xFor (chain::hpfHz) - 15.0f * s, plot.getBottom() - 11.0f * s, 30.0f * s, 11.0f * s),
                    juce::Justification::centred, false);
    }
}

void VocalMultitoolEditor::resized()
{
    const auto s = (float) getHeight() / (float) baseHeight;
    lnf.uiScale = s;
    auto px = [s] (float v) { return juce::roundToInt (v * s); };

    auto area = getLocalBounds();
    auto header = area.removeFromTop (px ((float) headerH));
    themeToggle.setBounds (header.removeFromRight (px (50)).withSizeKeepingCentre (px (30), px (30)));
    presetBox.setBounds (header.removeFromRight (px (300)).reduced (px (16), px (14)));

    // --- panels in a row ---------------------------------------------------------------
    auto body = area.reduced (px (14));
    const auto gap = px (16);
    float totalWeight = 0.0f;
    for (const auto& t : sections)
        totalWeight += t.weight;
    const auto usable = (float) (body.getWidth() - gap * (kNumSections - 1));
    for (int i = 0; i < kNumSections; ++i)
    {
        const auto w = i + 1 < kNumSections ? juce::roundToInt (usable * sections[i].weight / totalWeight) : body.getWidth();
        panels[(size_t) i] = body.removeFromLeft (w);
        body.removeFromLeft (gap);
    }

    // title rows: switch on the left, then the name
    for (int i = 0; i < kNumSections; ++i)
    {
        auto row = panels[(size_t) i].reduced (px (12), px (8)).removeFromTop (px (22));
        if (sections[i].onId != nullptr)
        {
            switches[(size_t) i].setBounds (row.removeFromLeft (px (34)).withSizeKeepingCentre (px (34), px (20)));
            row.removeFromLeft (px (8));
        }
        titleRows[(size_t) i] = row;
    }

    // inside each panel: title (30) on top, caption (24) at the bottom
    auto inner = [&] (int i) { return panels[(size_t) i].reduced (px (10), 0).withTrimmedTop (px (36)).withTrimmedBottom (px (28)); };

    const auto labelFont = albion::sans (11.5f * s, true).withExtraKerningFactor (0.12f);
    auto placeKnob = [&] (Knob& k, juce::Rectangle<int> r, int maxSize)
    {
        k.label.setFont (labelFont);
        k.label.setBounds (r.removeFromTop (px (18)));
        k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, px (80), px (18));
        const auto size = juce::jmin (r.getWidth(), r.getHeight(), maxSize);
        k.slider.setBounds (r.withSizeKeepingCentre (r.getWidth(), size));
    };

    // big single knobs centred vertically
    auto centredKnob = [&] (Knob& k, juce::Rectangle<int> r, int size = 0)
    {
        if (size == 0) size = px (92);
        placeKnob (k, r.withSizeKeepingCentre (r.getWidth(), juce::jmin (r.getHeight(), size + px (18) + px (24))), size + px (24));
    };

    centredKnob (knobs[kDenoise], inner (sDenoise));

    {
        auto r = inner (sHpf);
        hpfCurveArea = r.withSizeKeepingCentre (r.getWidth(), juce::jmin (r.getHeight(), r.getWidth())).reduced (px (2), px (6));
    }

    auto knobWithMeter = [&] (Knob& k, VerticalMeter& m, juce::Rectangle<int> r)
    {
        m.setBounds (r.removeFromRight (px (44)).reduced (0, px (4)));
        r.removeFromRight (px (4));
        centredKnob (k, r);
    };
    knobWithMeter (knobs[kClamp], clampMeter, inner (sClamp));
    knobWithMeter (knobs[kDeEss], deEssMeter, inner (sDeEss));
    knobWithMeter (knobs[kGlue],  glueMeter,  inner (sGlue));

    {
        auto r = inner (sEq);
        const auto w = r.getWidth() / 3;
        for (auto idx : { kMud, kNasal, kAir })
            centredKnob (knobs[(size_t) idx], r.removeFromLeft (w).translated (0, -px (8)), px (64));
    }

    centredKnob (knobs[kOutput], inner (sOutput));
}

void VocalMultitoolEditor::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto dt  = juce::jlimit (0.001, 0.1, (now - lastFrameMs) * 0.001);
    lastFrameMs = now;

    const std::array<VocalMultitoolProcessor::Meter, 3> sources { VocalMultitoolProcessor::mClamp,
                                                                  VocalMultitoolProcessor::mDeEss,
                                                                  VocalMultitoolProcessor::mGlue };
    const std::array<VerticalMeter*, 3> views { &clampMeter, &deEssMeter, &glueMeter };

    for (size_t i = 0; i < 3; ++i)
    {
        auto& st = meterStates[i];
        const auto gr = proc.takeMeterDb (sources[i]);
        const auto tau = gr > st.needle ? needleAttackS : needleReleaseS;
        st.needle += (float) (1.0 - std::exp (-dt / tau)) * (gr - st.needle);

        st.holdAge += (float) dt;
        if (gr >= st.hold || st.holdAge > holdSeconds)
        {
            st.hold = gr;
            st.holdAge = 0.0f;
        }
        views[i]->setGainReduction (st.needle, st.hold);
    }
    juce::ignoreUnused (proc.takeMeterDb (VocalMultitoolProcessor::mDenoise));

    const auto program = proc.getCurrentProgram();
    if (program != shownPreset)
    {
        shownPreset = program;
        presetBox.setSelectedId (program + 1, juce::dontSendNotification);
    }
}
