#include "PluginEditor.h"

using namespace albion;

namespace
{
    constexpr int baseWidth  = 1060;
    constexpr int baseHeight = 700;
    constexpr int headerH    = 70;
    constexpr int cardsH     = 156;
    constexpr int eqH        = 104;
    constexpr int statusH    = 24;

    const float tiltValues[] = { 0.0f, 3.0f, 4.5f };
}

AlbionReferenceAudioProcessorEditor::AlbionReferenceAudioProcessorEditor (AlbionReferenceAudioProcessor& p)
    : AudioProcessorEditor (&p), ref (p)
{
    setLookAndFeel (&lnf);

    themeToggle.onClick = [this] { albion::switchTheme (*this, lnf); };
    addAndMakeVisible (themeToggle);

    addAndMakeVisible (analyser);
    addAndMakeVisible (eqView);

    // --- reference ----------------------------------------------------------------------
    profileBox.setTextWhenNothingSelected ("Choose a reference profile...");
    profileBox.onChange = [this]
    {
        const auto idx = profileBox.getSelectedId() - 1;
        if (juce::isPositiveAndBelow (idx, (int) profileList.size()))
            ref.setReference (profileList[(size_t) idx].name, profileList[(size_t) idx].curve);
    };
    addAndMakeVisible (profileBox);

    captureButton.setTooltip ("Play a reference track through this plugin, press CAPTURE, then press again to stop. "
                              "Its average spectrum becomes the reference.");
    captureButton.onClick = [this]
    {
        if (ref.analysis.isRunning (AnalysisEngine::capture))
            ref.stopCapture();
        else
            ref.startCapture();
        updateButtons();
    };
    loadButton.setTooltip ("Analyse an audio file (WAV, AIFF, FLAC, OGG, MP3) as the reference");
    loadButton.onClick = [this] { chooseFile(); };
    saveButton.setTooltip ("Save the current reference as a profile, available in every project");
    saveButton.onClick = [this] { askProfileName(); };
    for (auto* b : { &captureButton, &loadButton, &saveButton })
        addAndMakeVisible (*b);

    // --- match EQ --------------------------------------------------------------------------
    learnButton.setTooltip ("Press, play your mix (the main, loudest sections are best), press again. "
                            "The match EQ is fitted to move that average gently towards the reference.");
    learnButton.onClick = [this]
    {
        if (ref.analysis.isRunning (AnalysisEngine::learn))
            ref.stopLearn();
        else
            ref.startLearn();
        updateButtons();
    };
    addAndMakeVisible (learnButton);

    eqOnButton.setClickingTogglesState (true);
    eqOnButton.setTooltip ("Bypass / enable the match EQ (the analyser keeps running)");
    eqOnAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (ref.apvts, ids::eqOn.getParamID(), eqOnButton);
    addAndMakeVisible (eqOnButton);

    setupKnob (amountKnob, ids::amount,    "AMOUNT");
    setupKnob (limitKnob,  ids::maxDb,     "LIMIT");
    setupKnob (smoothKnob, ids::smoothing, "SMOOTHING");
    setupKnob (outputKnob, ids::output,    "OUTPUT");
    amountKnob.slider.setTooltip ("How much of the fitted correction is applied");
    limitKnob.slider.setTooltip ("Largest boost or cut the match EQ may apply at any frequency");
    smoothKnob.slider.setTooltip ("How broad the correction is: wider = gentler, more tonal-balance-like");

    tiltBox.addItemList ({ "Tilt 0 dB/oct", "Tilt 3 dB/oct", "Tilt 4.5 dB/oct" }, 1);
    tiltBox.setTooltip ("Analyser slope compensation (3 dB/oct shows pink noise flat)");
    tiltAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (ref.apvts, ids::tilt.getParamID(), tiltBox);
    addAndMakeVisible (tiltBox);

    ref.addChangeListener (this);
    rebuildProfileMenu();

    setResizable (true, true);
    setResizeLimits (900, 620, 2400, 1600);
    setSize (baseWidth, baseHeight);
    updateButtons();
    startTimerHz (30);
}

AlbionReferenceAudioProcessorEditor::~AlbionReferenceAudioProcessorEditor()
{
    stopTimer();
    ref.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void AlbionReferenceAudioProcessorEditor::setupKnob (Knob& k, const juce::ParameterID& id, const juce::String& caption)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 84, 18);
    k.slider.setColour (juce::Slider::textBoxTextColourId, colours::ink);
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (k.slider);

    k.label.setText (caption, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setColour (juce::Label::textColourId, colours::accentDark);
    k.label.setFont (mono (10.5f).withExtraKerningFactor (0.14f));
    addAndMakeVisible (k.label);

    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (ref.apvts, id.getParamID(), k.slider);
    auto* param = ref.apvts.getParameter (id.getParamID());
    k.slider.setDoubleClickReturnValue (true, (double) param->convertFrom0to1 (param->getDefaultValue()));
}

//==============================================================================
void AlbionReferenceAudioProcessorEditor::rebuildProfileMenu()
{
    profileList = ref.getProfiles();
    profileBox.clear (juce::dontSendNotification);
    profileBox.addSectionHeading ("Generic tonal balance (slopes)");
    bool userHeading = false;
    for (int i = 0; i < (int) profileList.size(); ++i)
    {
        if (! profileList[(size_t) i].builtIn && ! userHeading)
        {
            profileBox.addSeparator();
            profileBox.addSectionHeading ("Your profiles");
            userHeading = true;
        }
        profileBox.addItem (profileList[(size_t) i].name, i + 1);
    }

    // reflect the current reference (may be a capture or file that isn't in the list)
    int match = 0;
    for (int i = 0; i < (int) profileList.size(); ++i)
        if (ref.hasReference() && profileList[(size_t) i].name == ref.getReferenceName())
            match = i + 1;
    if (match > 0)
        profileBox.setSelectedId (match, juce::dontSendNotification);
    else
        profileBox.setText (ref.hasReference() ? ref.getReferenceName() : juce::String(), juce::dontSendNotification);
}

void AlbionReferenceAudioProcessorEditor::chooseFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a reference track", juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f.existsAsFile())
                                  ref.loadReferenceFile (f);
                          });
}

void AlbionReferenceAudioProcessorEditor::askProfileName()
{
    if (! ref.hasReference())
        return;
    auto* w = new juce::AlertWindow ("Save reference profile", "Name this profile (it will be available in every project):",
                                     juce::MessageBoxIconType::NoIcon, this);
    w->setLookAndFeel (&lnf);
    w->addTextEditor ("name", ref.getReferenceName());
    w->addButton ("SAVE", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int result)
    {
        if (result == 1 && w->getTextEditorContents ("name").trim().isNotEmpty())
        {
            ref.saveReferenceAsProfile (w->getTextEditorContents ("name"));
            rebuildProfileMenu();
        }
    }), true);
}

void AlbionReferenceAudioProcessorEditor::updateButtons()
{
    const bool capturing = ref.analysis.isRunning (AnalysisEngine::capture);
    const bool learning  = ref.analysis.isRunning (AnalysisEngine::learn);

    captureButton.setToggleState (capturing, juce::dontSendNotification);
    captureButton.setButtonText (capturing ? "STOP  " + juce::String (juce::roundToInt (ref.analysis.secondsHeard (AnalysisEngine::capture))) + " s"
                                           : juce::String ("CAPTURE"));
    learnButton.setToggleState (learning, juce::dontSendNotification);
    learnButton.setButtonText (learning ? "STOP  " + juce::String (juce::roundToInt (ref.analysis.secondsHeard (AnalysisEngine::learn))) + " s"
                                        : juce::String (ref.hasLearnedMix() ? "RE-LEARN MIX" : "LEARN MIX"));
    saveButton.setEnabled (ref.hasReference());
    loadButton.setEnabled (! ref.isAnalysingFile());
}

void AlbionReferenceAudioProcessorEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    rebuildProfileMenu();
    updateButtons();
    repaint();
}

void AlbionReferenceAudioProcessorEditor::timerCallback()
{
    AnalyserView::Data d;
    spectrum::Curve post {};
    d.hasLive = ref.analysis.getDisplay (d.live, post);
    if (d.hasLive)
        d.live = post;   // show what you hear (after the match EQ)
    d.hasReference = ref.hasReference();
    d.reference = ref.getReferenceCurve();
    d.referenceName = ref.getReferenceName();
    d.hasMix = ref.hasLearnedMix();
    d.mix = ref.getLearnedMix();
    d.tiltDbPerOct = tiltValues[juce::jlimit (0, 2, tiltBox.getSelectedItemIndex())];
    analyser.setData (d);

    // EQ strip shows what is applied right now (fit x amount, or flat when off)
    const bool on = eqOnButton.getToggleState();
    const auto amount = (float) amountKnob.slider.getValue() * 0.01f;
    auto applied = ref.getEqResponse();
    for (auto& v : applied)
        v *= on ? amount : 0.0f;
    juce::String caption = "MATCH EQ";
    if (! ref.isMatchReady())
        caption << (ref.hasReference() ? "  -  LEARN YOUR MIX TO FIT IT" : "  -  CHOOSE A REFERENCE AND LEARN YOUR MIX");
    else
        caption << "  -  " << (on ? juce::String (juce::roundToInt (amount * 100.0f)) + "% OF THE FIT" : juce::String ("BYPASSED"));
    eqView.setCurve (applied, (float) limitKnob.slider.getValue() + 1.0f, on && ref.isMatchReady(), caption);

    if (ref.analysis.isRunning (AnalysisEngine::capture) || ref.analysis.isRunning (AnalysisEngine::learn) || ref.isAnalysingFile())
    {
        updateButtons();
        repaint (statusArea);
    }
}

//==============================================================================
void AlbionReferenceAudioProcessorEditor::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (colours::paperLight, 0.0f, 0.0f, colours::paper, 0.0f, bounds.getBottom(), false));
    g.fillAll();

    albion::drawHeader (g, bounds.removeFromTop ((float) headerH), juce::String::fromUTF8 ("Reference  \xc2\xb7  analyser & gentle match EQ"), 1.0f);

    struct Card { juce::Rectangle<int> r; const char* title; };
    for (const auto& c : { Card { refCard, "REFERENCE" }, Card { matchCard, "MATCH EQ" }, Card { outCard, "OUTPUT" } })
    {
        g.setColour (colours::paperDeep.withAlpha (0.6f));
        g.fillRoundedRectangle (c.r.toFloat(), 8.0f);
        g.setColour (colours::line);
        g.drawRoundedRectangle (c.r.toFloat().reduced (0.5f), 8.0f, 1.0f);
        g.setColour (colours::accentDark);
        g.setFont (mono (10.5f).withExtraKerningFactor (0.16f));
        g.drawText (c.title, c.r.reduced (14, 8).removeFromTop (14), juce::Justification::centredLeft, false);
    }

    // reference name inside its card
    auto refInfo = refCard.reduced (14, 10).removeFromBottom (36);
    g.setColour (ref.hasReference() ? colours::ink : colours::muted);
    g.setFont (sans (13.0f, ref.hasReference()));
    g.drawFittedText (ref.hasReference() ? ref.getReferenceName() : juce::String ("No reference yet"),
                      refInfo, juce::Justification::centredLeft, 2);

    // status line
    auto status = ref.getStatusMessage();
    if (ref.isAnalysingFile())
        status = "Analysing reference file... " + juce::String (juce::roundToInt (ref.getFileProgress() * 100.0f)) + "%";
    g.setColour (colours::muted);
    g.setFont (sans (12.5f));
    g.drawText (status, statusArea, juce::Justification::centredLeft, true);
    g.drawText (ref.hasLearnedMix() ? "Learned mix: " + juce::String (juce::roundToInt (ref.getLearnedSeconds())) + " s" : juce::String ("No mix learned"),
                statusArea, juce::Justification::centredRight, false);
}

void AlbionReferenceAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop (headerH);
    themeToggle.setBounds (header.removeFromRight (50).withSizeKeepingCentre (30, 30));
    profileBox.setBounds (header.removeFromRight (340).reduced (18, 19));

    area = area.reduced (14, 10);
    statusArea = area.removeFromBottom (statusH).reduced (4, 0);
    auto cards = area.removeFromBottom (cardsH);
    area.removeFromBottom (10);
    eqView.setBounds (area.removeFromBottom (eqH));
    area.removeFromBottom (10);
    analyser.setBounds (area);
    // tilt picker lives in the analyser's top-right corner
    tiltBox.setBounds (area.getRight() - 162, area.getY() + 6, 150, 22);

    // cards: reference (wide) | match EQ (widest) | output
    const auto gap = 10;
    refCard   = cards.removeFromLeft (juce::roundToInt ((float) cards.getWidth() * 0.30f));
    cards.removeFromLeft (gap);
    outCard   = cards.removeFromRight (juce::roundToInt ((float) cards.getWidth() * 0.16f));
    cards.removeFromRight (gap);
    matchCard = cards;

    {
        auto r = refCard.reduced (14, 10).withTrimmedTop (20);
        auto row = r.removeFromTop (32);
        const auto w = (row.getWidth() - 12) / 3;
        captureButton.setBounds (row.removeFromLeft (w));
        row.removeFromLeft (6);
        loadButton.setBounds (row.removeFromLeft (w));
        row.removeFromLeft (6);
        saveButton.setBounds (row);
    }
    {
        auto r = matchCard.reduced (14, 10).withTrimmedTop (20);
        auto buttons = r.removeFromLeft (150);
        learnButton.setBounds (buttons.removeFromTop (34));
        buttons.removeFromTop (10);
        eqOnButton.setBounds (buttons.removeFromTop (30));
        r.removeFromLeft (10);
        const auto w = r.getWidth() / 3;
        for (auto* k : { &amountKnob, &limitKnob, &smoothKnob })
        {
            auto cell = r.removeFromLeft (w);
            k->label.setBounds (cell.removeFromTop (14));
            k->slider.setBounds (cell);
        }
    }
    {
        auto r = outCard.reduced (14, 10).withTrimmedTop (20);
        outputKnob.label.setBounds (r.removeFromTop (14));
        outputKnob.slider.setBounds (r);
    }
}
