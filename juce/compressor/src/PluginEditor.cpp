#include "PluginEditor.h"
#include "Presets.h"

using namespace albion;

namespace
{
    constexpr int baseWidth  = 900;
    constexpr int baseHeight = 380;
    constexpr int headerH    = 58;

    // GR needle ballistics (GUI side): quick to deflect, gentle return
    constexpr double needleAttackS  = 0.010;
    constexpr double needleReleaseS = 0.300;

    const char* ratioNames[] = { "4", "8", "12", "20", "ALL" };
}

Albion76AudioProcessorEditor::Albion76AudioProcessorEditor (Albion76AudioProcessor& p)
    : AudioProcessorEditor (&p), albion (p)
{
    setLookAndFeel (&lnf);

    themeToggle.onClick = [this] { albion::switchTheme (*this, lnf); };
    addAndMakeVisible (themeToggle);

    addAndMakeVisible (grMeter);

    setupKnob (knobs[kGain],      ids::gain,      "GAIN");
    setupKnob (knobs[kThreshold], ids::threshold, "THRESHOLD");
    setupKnob (knobs[kKnee],      ids::knee,      "KNEE");
    setupKnob (knobs[kAttack],    ids::attack,    "ATTACK");
    setupKnob (knobs[kRelease],   ids::release,   "RELEASE");
    setupKnob (knobs[kMakeup],    ids::makeup,    "MAKEUP");
    setupKnob (knobs[kMix],       ids::mix,       "MIX");

    // --- ratio push-buttons, one lit at a time -------------------------------
    ratioLabel.setText ("RATIO", juce::dontSendNotification);
    ratioLabel.setJustificationType (juce::Justification::centred);
    ratioLabel.setColour (juce::Label::textColourId, colours::brass);
    addAndMakeVisible (ratioLabel);

    auto* ratioParam = albion.apvts.getParameter (ids::ratio.getParamID());
    ratioAttachment = std::make_unique<juce::ParameterAttachment> (*ratioParam, [this] (float index)
    {
        const auto idx = juce::roundToInt (index);
        for (int i = 0; i < (int) ratioButtons.size(); ++i)
            ratioButtons[(size_t) i].setToggleState (i == idx, juce::dontSendNotification);
    });

    for (int i = 0; i < (int) ratioButtons.size(); ++i)
    {
        auto& b = ratioButtons[(size_t) i];
        b.setButtonText (ratioNames[i]);
        b.setClickingTogglesState (false);
        b.onClick = [this, i] { ratioAttachment->setValueAsCompleteGesture ((float) i); };
        addAndMakeVisible (b);
    }
    ratioAttachment->sendInitialUpdate();

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
            albion.setCurrentProgram (idx);
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

Albion76AudioProcessorEditor::~Albion76AudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void Albion76AudioProcessorEditor::setupKnob (Knob& k, const juce::ParameterID& id, const juce::String& name)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 18);
    k.slider.setDoubleClickReturnValue (true, (double) albion.apvts.getParameterRange (id.getParamID())
                                                  .convertFrom0to1 (albion.apvts.getParameter (id.getParamID())->getDefaultValue()));
    addAndMakeVisible (k.slider);

    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setColour (juce::Label::textColourId, colours::brass);
    addAndMakeVisible (k.label);

    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (albion.apvts, id.getParamID(), k.slider);
}

void Albion76AudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto s = (float) getHeight() / (float) baseHeight;
    auto bounds = getLocalBounds().toFloat();

    g.setGradientFill (juce::ColourGradient (colours::panelLight, 0.0f, 0.0f,
                                             colours::panel, 0.0f, bounds.getBottom(), false));
    g.fillAll();

    // header: shared Albion Road Audio header (logo, wordmark, product)
    auto header = bounds.removeFromTop ((float) headerH * s);
    albion::drawHeader (g, header, juce::String::fromUTF8 ("76 Compressor  ·  FET-style"), s);

    // subtle inset behind the knob section
    auto body = getLocalBounds().toFloat().withTrimmedTop ((float) headerH * s).reduced (14.0f * s);
    auto knobArea = body.withTrimmedLeft (body.getWidth() * 0.37f + 14.0f * s);
    g.setColour (colours::panelDark.withAlpha (0.35f));
    g.fillRoundedRectangle (knobArea, 8.0f * s);

}

void Albion76AudioProcessorEditor::resized()
{
    const auto s = (float) getHeight() / (float) baseHeight;
    lnf.uiScale = s;
    auto px = [s] (float v) { return juce::roundToInt (v * s); };

    auto area = getLocalBounds();
    auto header = area.removeFromTop (px ((float) headerH));
    themeToggle.setBounds (header.removeFromRight (px (50)).withSizeKeepingCentre (px (30), px (30)));
    presetBox.setBounds (header.removeFromRight (px (290)).reduced (px (16), px (14)));

    auto body = area.reduced (px (14));
    auto meterArea = body.removeFromLeft (juce::roundToInt ((float) body.getWidth() * 0.37f));
    body.removeFromLeft (px (14));
    grMeter.setBounds (meterArea.withSizeKeepingCentre (meterArea.getWidth(), juce::jmin (meterArea.getHeight(), juce::roundToInt ((float) meterArea.getWidth() * 0.72f))));

    // 4 x 2 grid: [Gain, Threshold, Ratio, Knee] / [Attack, Release, Makeup, Mix]
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

    placeKnob (knobs[kGain],      cell (0, 0));
    placeKnob (knobs[kThreshold], cell (1, 0));
    placeKnob (knobs[kKnee],      cell (3, 0));
    placeKnob (knobs[kAttack],    cell (0, 1));
    placeKnob (knobs[kRelease],   cell (1, 1));
    placeKnob (knobs[kMakeup],    cell (2, 1));
    placeKnob (knobs[kMix],       cell (3, 1));

    auto ratioCell = cell (2, 0);
    ratioLabel.setFont (juce::Font (juce::FontOptions (fontH, juce::Font::bold)).withExtraKerningFactor (0.12f));
    ratioLabel.setBounds (ratioCell.removeFromTop (labelH));
    auto buttons = ratioCell.reduced (px (22), px (4));
    const auto bh = buttons.getHeight() / (int) ratioButtons.size();
    for (auto& b : ratioButtons)
        b.setBounds (buttons.removeFromTop (bh).reduced (0, px (2)));
}

void Albion76AudioProcessorEditor::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto dt  = juce::jlimit (0.001, 0.1, (now - lastFrameMs) * 0.001);
    lastFrameMs = now;

    const auto gr = albion.takeGainReductionDb();
    const auto tau = gr > needleGr ? needleAttackS : needleReleaseS;
    needleGr += (float) (1.0 - std::exp (-dt / tau)) * (gr - needleGr);
    grMeter.setGainReduction (needleGr);

    const auto program = albion.getCurrentProgram();
    if (program != shownPreset)
    {
        shownPreset = program;
        presetBox.setSelectedId (program + 1, juce::dontSendNotification);
    }
}
