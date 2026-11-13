#include "KitViews.h"

using namespace albion;

namespace
{
    juce::Font font (float h, bool bold = false)
    {
        return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
    }
}

//==============================================================================
PadComponent::PadComponent (int n, int i, juce::String nm)
    : note (n), instrument (i), name (nm.replaceCharacter ('_', ' ').replaceCharacter ('-', ' '))
{
    setRepaintsOnMouseActivity (true);
}

void PadComponent::setHit (float velocity) noexcept
{
    glow = juce::jmax (glow, juce::jlimit (0.25f, 1.0f, velocity));
    repaint();
}

bool PadComponent::decay (float amount) noexcept
{
    if (glow <= 0.0f)
        return false;
    glow = juce::jmax (0.0f, glow - amount);
    return true;
}

void PadComponent::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (4.0f);
    const auto corner = juce::jmin (10.0f, r.getHeight() * 0.18f);

    g.setGradientFill (juce::ColourGradient (colours::paperLight, r.getX(), r.getY(),
                                             colours::paperDeep, r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, corner);

    if (glow > 0.0f)
    {
        g.setColour (colours::accent.withAlpha (0.85f * glow));
        g.fillRoundedRectangle (r, corner);
    }

    g.setColour (isMouseOver() ? colours::brass : colours::brassDark.withAlpha (0.8f));
    g.drawRoundedRectangle (r, corner, isMouseOver() ? 1.6f : 1.0f);

    auto inner = r.reduced (8.0f, 6.0f);
    g.setColour (glow > 0.3f ? colours::paper : colours::ink);
    g.setFont (font (juce::jlimit (11.0f, 16.0f, r.getHeight() * 0.24f), true));
    g.drawFittedText (name, inner.removeFromTop (inner.getHeight() * 0.66f).toNearestInt(),
                      juce::Justification::centredLeft, 2, 0.9f);

    g.setColour ((glow > 0.3f ? colours::paper : colours::muted).withAlpha (0.9f));
    g.setFont (font (juce::jlimit (9.0f, 12.0f, r.getHeight() * 0.18f)));
    g.drawText (juce::String (note) + "  " + juce::MidiMessage::getMidiNoteName (note, true, true, 3),
                inner.toNearestInt(), juce::Justification::centredLeft, false);
}

void PadComponent::mouseDown (const juce::MouseEvent& e)
{
    if (onAudition == nullptr)
        return;
    // top of the pad = full velocity, bottom = soft
    const auto v = 1.0f - juce::jlimit (0.0f, 1.0f, (float) e.y / (float) juce::jmax (1, getHeight()));
    onAudition (note, juce::jlimit (0.05f, 1.0f, 0.15f + 0.85f * v));
}

//==============================================================================
void PadGrid::setPads (std::vector<std::unique_ptr<PadComponent>> newPads)
{
    for (auto& p : pads)
        removeChildComponent (p.get());
    pads = std::move (newPads);
    for (auto& p : pads)
        addAndMakeVisible (*p);
    resized();
    repaint();
}

int PadGrid::layoutFor (int width)
{
    const int columns = juce::jmax (2, width / 112);
    const int rows    = ((int) pads.size() + columns - 1) / columns;
    return juce::jmax (1, rows) * 74;
}

void PadGrid::resized()
{
    const int columns = juce::jmax (2, getWidth() / 112);
    const int padW = getWidth() / columns;
    for (size_t i = 0; i < pads.size(); ++i)
        pads[i]->setBounds ((int) (i % (size_t) columns) * padW, (int) (i / (size_t) columns) * 74, padW, 74);
}

void PadGrid::paint (juce::Graphics& g)
{
    if (! pads.empty())
        return;
    g.setColour (colours::cream.withAlpha (0.55f));
    g.setFont (font (16.0f));
    g.drawFittedText (emptyMessage, getLocalBounds().reduced (20), juce::Justification::centred, 3);
}

//==============================================================================
MixerStrip::MixerStrip (juce::ValueTree channelState, bool showPan, float uiScale)
    : state (std::move (channelState)), hasPan (showPan), scale (uiScale)
{
    title = state["name"].toString().replaceCharacter ('_', ' ');

    pan.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    pan.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    pan.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    pan.setRange (-1.0, 1.0, 0.01);
    pan.setDoubleClickReturnValue (true, 0.0);
    pan.getValueObject().referTo (state.getPropertyAsValue ("pan", nullptr));
    pan.textFromValueFunction = [] (double v)
    {
        if (std::abs (v) < 0.005) return juce::String ("C");
        return juce::String (juce::roundToInt (std::abs (v) * 100.0)) + (v < 0 ? " L" : " R");
    };
    pan.setPopupDisplayEnabled (true, true, nullptr);
    if (hasPan)
        addAndMakeVisible (pan);

    // per-hit gate (drum strips only): Off .. -3 dB below each hit's own peak
    if (! hasPan)
    {
        if (! state.hasProperty ("gate"))
            state.setProperty ("gate", -60.0, nullptr);
        gate.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        gate.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
        gate.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        gate.setRange (-60.0, -12.0, 0.5);
        gate.setSkewFactorFromMidPoint (-30.0);
        gate.setDoubleClickReturnValue (true, -60.0);
        gate.getValueObject().referTo (state.getPropertyAsValue ("gate", nullptr));
        gate.textFromValueFunction = [] (double v)
        {
            return v <= -59.95 ? juce::String ("Off") : juce::String (v, v > -10.0 ? 1 : 0) + " dB";
        };
        gate.setPopupDisplayEnabled (true, true, nullptr);
        gate.setTooltip ("Gate: cuts each hit once its level (RMS of the drum's main mics) has fallen this many dB "
                         "below that hit's own loudest point, then closes over 20 ms. Relative to each hit, not dBFS, "
                         "so soft and hard hits are shortened alike. Double-click for Off.");
        gate.onValueChange = [this] { repaint (gateText); };
        addAndMakeVisible (gate);
    }

    for (auto* b : { &mute, &solo })
    {
        b->setClickingTogglesState (true);
        addAndMakeVisible (*b);
    }
    mute.getProperties().set ("onColour", (juce::int64) colours::accent.getARGB());
    solo.getProperties().set ("onColour", (juce::int64) colours::olive.getARGB());
    mute.getToggleStateValue().referTo (state.getPropertyAsValue ("mute", nullptr));
    solo.getToggleStateValue().referTo (state.getPropertyAsValue ("solo", nullptr));

    fader.setSliderStyle (juce::Slider::LinearVertical);
    fader.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    fader.setRange (-60.0, 12.0, 0.1);
    fader.setSkewFactorFromMidPoint (-12.0);
    fader.setDoubleClickReturnValue (true, 0.0);
    fader.getValueObject().referTo (state.getPropertyAsValue ("level", nullptr));
    fader.onValueChange = [this]
    {
        const auto v = fader.getValue();
        readout.setText (v <= -59.95 ? juce::String ("-inf") : juce::String (v, 1), juce::dontSendNotification);
    };
    addAndMakeVisible (fader);

    readout.setJustificationType (juce::Justification::centred);
    readout.setColour (juce::Label::textColourId, colours::muted);
    readout.setFont (font (11.0f * scale));
    addAndMakeVisible (readout);
    fader.onValueChange();
}

void MixerStrip::setMeter (float peak) noexcept
{
    // fast attack, ~300 ms visual release, short peak hold
    meterLevel = juce::jmax (peak, meterLevel * 0.85f);
    meterHold  = juce::jmax (peak, meterHold * 0.97f);
    repaint (fader.getBounds().withX (fader.getRight()).withWidth (juce::roundToInt (10 * scale)).expanded (2));
}

void MixerStrip::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (3.0f, 2.0f);
    g.setColour (colours::paperLight);
    g.fillRoundedRectangle (r, 6.0f * scale);

    g.setColour (colours::cream);
    g.setFont (font (11.5f * scale, true));
    g.drawFittedText (title, getLocalBounds().reduced (juce::roundToInt (5 * scale), juce::roundToInt (4 * scale))
                                 .removeFromTop (juce::roundToInt ((hasPan ? 18 : 30) * scale)),
                      juce::Justification::centred, hasPan ? 1 : 2, 0.7f);

    if (! hasPan)
    {
        const bool on = gate.getValue() > -59.95;
        g.setColour (on ? colours::accent : colours::muted.withAlpha (0.7f));
        g.setFont (font (10.0f * scale, true));
        g.drawText (on ? "GATE " + gate.getTextFromValue (gate.getValue()).upToFirstOccurrenceOf (" ", false, false)
                       : juce::String ("GATE OFF"),
                    gateText, juce::Justification::centred, false);
    }

    // meter next to the fader: cream -> brass -> red, -48..+6 dB
    auto meter = fader.getBounds().toFloat().withX ((float) fader.getRight()).withWidth (6.0f * scale).reduced (0.0f, 6.0f);
    g.setColour (colours::panelDark);
    g.fillRoundedRectangle (meter, 2.0f);

    auto toY = [&meter] (float lin)
    {
        const auto db = juce::Decibels::gainToDecibels (lin, -48.0f);
        return meter.getBottom() - meter.getHeight() * juce::jlimit (0.0f, 1.0f, (db + 48.0f) / 54.0f);
    };
    const auto top = toY (meterLevel);
    g.setGradientFill (juce::ColourGradient (colours::olive.withAlpha (0.75f), 0.0f, meter.getBottom(),
                                             colours::accent, 0.0f, meter.getY(), false));
    g.fillRect (meter.withTop (top));
    if (meterHold > 0.001f)
    {
        g.setColour (meterHold >= 1.0f ? colours::accentDark : colours::ink);
        g.fillRect (meter.withTop (toY (meterHold)).withHeight (2.0f));
    }
}

void MixerStrip::resized()
{
    auto r = getLocalBounds().reduced (juce::roundToInt (6 * scale), juce::roundToInt (5 * scale));
    r.removeFromTop (juce::roundToInt ((hasPan ? 18 : 30) * scale));
    if (hasPan)
        pan.setBounds (r.removeFromTop (juce::roundToInt (36 * scale)).reduced (juce::roundToInt (8 * scale), 0));
    else
    {
        gate.setBounds (r.removeFromTop (juce::roundToInt (30 * scale)).reduced (juce::roundToInt (10 * scale), 0));
        gateText = r.removeFromTop (juce::roundToInt (13 * scale));
    }
    auto buttons = r.removeFromTop (juce::roundToInt (22 * scale));
    mute.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (2, 1));
    solo.setBounds (buttons.reduced (2, 1));
    readout.setBounds (r.removeFromBottom (juce::roundToInt (16 * scale)));
    r.removeFromTop (juce::roundToInt (4 * scale));
    fader.setBounds (r.withTrimmedRight (juce::roundToInt (10 * scale)));
}
