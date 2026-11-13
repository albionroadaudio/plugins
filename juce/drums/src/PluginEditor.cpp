#include "PluginEditor.h"
#include <numeric>

using namespace albion;

namespace
{
    constexpr int baseWidth  = 1120;
    constexpr int baseHeight = 960;
    constexpr int headerH    = 58;
    constexpr int infoH      = 40;
    constexpr int mixerH     = 460;   // tall faders, plus Gate / Bleed / Tune on drum strips
    constexpr int stripW     = 74;
    constexpr int drumStripW = 70;

    juce::Font font (float h, bool bold = false)
    {
        return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
    }

    juce::String bytesToText (std::uint64_t b)
    {
        if (b >= (1ull << 30)) return juce::String ((double) b / (double) (1ull << 30), 2) + " GB";
        return juce::String (juce::roundToInt ((double) b / (double) (1ull << 20))) + " MB";
    }
}

AlbionDrumsAudioProcessorEditor::AlbionDrumsAudioProcessorEditor (AlbionDrumsAudioProcessor& p)
    : AudioProcessorEditor (&p), drums (p)
{
    setLookAndFeel (&lnf);

    themeToggle.onClick = [this] { albion::switchTheme (*this, lnf); };
    addAndMakeVisible (themeToggle);

    loadButton.onClick = [this] { chooseKit(); };
    addAndMakeVisible (loadButton);

    variantBox.setTextWhenNothingSelected ("Kit variant");
    variantBox.onChange = [this]
    {
        const auto idx = variantBox.getSelectedItemIndex();
        if (const auto* kit = drums.getPublishedKit(); kit != nullptr && juce::isPositiveAndBelow (idx, variantFiles.size())
                                                       && variantFiles[idx] != kit->kitFile)
            drums.loadKit (variantFiles[idx]);
    };
    addAndMakeVisible (variantBox);

    midimapBox.setTextWhenNothingSelected ("MIDI map");
    midimapBox.onChange = [this]
    {
        const auto idx = midimapBox.getSelectedItemIndex();
        if (const auto* kit = drums.getPublishedKit(); kit != nullptr && juce::isPositiveAndBelow (idx, midimapFiles.size())
                                                       && midimapFiles[idx] != kit->midimapFile)
            drums.loadKit (kit->kitFile, midimapFiles[idx]);
    };
    addAndMakeVisible (midimapBox);

    padView.setViewedComponent (&padGrid, false);
    padView.setScrollBarsShown (true, false);
    addAndMakeVisible (padView);

    for (auto* tab : { &micsTab, &drumsTab })
    {
        tab->setClickingTogglesState (false);
        tab->getProperties().set ("onColour", (juce::int64) colours::ink.getARGB());
        addAndMakeVisible (*tab);
    }
    micsTab.onClick  = [this] { setMixerView (false); };
    drumsTab.onClick = [this] { setMixerView (true); };
    micsTab.setTooltip ("Mixer per microphone channel (pan, level, mute, solo)");
    drumsTab.setTooltip ("Mixer per drum: gate, bleed and tune for each drum, and a fader that sets "
                         "its level in every mic, bleed included");
    showDrums = (bool) drums.getUiState().getProperty ("drumsView", false);

    mixerView.setViewedComponent (&mixerContent, false);
    mixerView.setScrollBarsShown (false, true);
    addAndMakeVisible (mixerView);

    setupKnob (knobs[kMaster],   ids::master,   "MASTER");
    setupKnob (knobs[kTune],     ids::tune,     "TUNE");
    setupKnob (knobs[kDynamics], ids::dynamics, "DYNAMICS");
    setupKnob (knobs[kBleed],    ids::bleed,    "BLEED");
    setupKnob (knobs[kHumanize], ids::humanize, "HUMANIZE");
    knobs[kBleed].slider.setTooltip ("Master mic bleed (non-main channels) for the whole kit; "
                                     "each drum's BLEED knob in the DRUMS mixer scales it further");
    knobs[kTune].slider.setTooltip ("Pitch of the whole kit in semitones; each drum's TUNE knob "
                                    "in the DRUMS mixer adds to it");

    drums.addChangeListener (this);

    setResizable (true, true);
    setResizeLimits (920, 700, 2400, 1800);
    setSize (baseWidth, baseHeight);

    rebuildForKit();
    startTimerHz (30);
}

AlbionDrumsAudioProcessorEditor::~AlbionDrumsAudioProcessorEditor()
{
    stopTimer();
    drums.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void AlbionDrumsAudioProcessorEditor::setupKnob (Knob& k, const juce::ParameterID& id, const juce::String& name)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 84, 18);
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    k.slider.setColour (juce::Slider::textBoxTextColourId, colours::cream);
    addAndMakeVisible (k.slider);

    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setColour (juce::Label::textColourId, colours::brass);
    k.label.setFont (font (12.0f, true).withExtraKerningFactor (0.12f));
    addAndMakeVisible (k.label);

    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (drums.apvts, id.getParamID(), k.slider);
    auto* param = drums.apvts.getParameter (id.getParamID());
    k.slider.setDoubleClickReturnValue (true, (double) param->convertFrom0to1 (param->getDefaultValue()));
}

//==============================================================================
void AlbionDrumsAudioProcessorEditor::chooseKit()
{
    auto start = drums.getRequestedKit().getParentDirectory();
    if (! start.isDirectory())
        start = juce::File::getSpecialLocation (juce::File::userHomeDirectory);

    chooser = std::make_unique<juce::FileChooser> ("Choose a DrumGizmo kit (the drumkit .xml file)", start, "*.xml");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f.existsAsFile())
                                  drums.loadKit (f);
                          });
}

void AlbionDrumsAudioProcessorEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (drums.getPublishedKit() != shownKit)
        rebuildForKit();
    repaint();
}

void AlbionDrumsAudioProcessorEditor::rebuildForKit()
{
    const auto* kit = drums.getPublishedKit();
    shownKit = kit;

    // --- pads: one per midimap entry, in note order ----------------------------
    std::vector<std::unique_ptr<PadComponent>> pads;
    if (kit != nullptr)
    {
        auto entries = kit->midimap;
        std::stable_sort (entries.begin(), entries.end(), [] (const auto& a, const auto& b) { return a.note < b.note; });
        for (const auto& e : entries)
        {
            auto pad = std::make_unique<PadComponent> (e.note, e.instrument, kit->instruments[(size_t) e.instrument].name);
            pad->onAudition = [this] (int note, float v) { drums.audition (note, v); };
            pads.push_back (std::move (pad));
        }
        padGrid.emptyMessage = "This MIDI map doesn't map any notes to this kit's instruments.";
    }
    padGrid.setPads (std::move (pads));

    // --- mixer strips: mics and drums ------------------------------------------------
    strips.clear();
    drumStrips.clear();
    if (kit != nullptr)
    {
        for (const auto& name : kit->channelNames)
        {
            auto strip = std::make_unique<MixerStrip> (drums.getChannelState (name), true);
            mixerContent.addChildComponent (*strip);
            strips.push_back (std::move (strip));
        }
        // drum strips in MIDI-note order (same as the pads), unmapped drums last
        std::vector<int> order (kit->instruments.size());
        std::iota (order.begin(), order.end(), 0);
        auto lowestNote = [kit] (int instrument)
        {
            int best = 128;
            for (const auto& e : kit->midimap)
                if (e.instrument == instrument)
                    best = juce::jmin (best, e.note);
            return best;
        };
        std::stable_sort (order.begin(), order.end(), [&] (int a, int b) { return lowestNote (a) < lowestNote (b); });
        drumStripInstrument = order;

        for (auto i : order)
        {
            auto strip = std::make_unique<MixerStrip> (drums.getInstrumentState (kit->instruments[(size_t) i].name), false);
            mixerContent.addChildComponent (*strip);
            drumStrips.push_back (std::move (strip));
        }
    }
    setMixerView (showDrums);

    // --- variant / midimap pickers ----------------------------------------------------
    variantBox.clear (juce::dontSendNotification);
    midimapBox.clear (juce::dontSendNotification);
    variantFiles.clear();
    midimapFiles.clear();
    if (kit != nullptr)
    {
        const auto dir = kit->kitFile.getParentDirectory();
        variantFiles = kitfiles::findKitVariants (dir);
        midimapFiles = kitfiles::findMidimaps (dir);
        for (int i = 0; i < variantFiles.size(); ++i)
        {
            variantBox.addItem (variantFiles[i].getFileNameWithoutExtension(), i + 1);
            if (variantFiles[i] == kit->kitFile)
                variantBox.setSelectedId (i + 1, juce::dontSendNotification);
        }
        for (int i = 0; i < midimapFiles.size(); ++i)
        {
            midimapBox.addItem (midimapFiles[i].getFileNameWithoutExtension(), i + 1);
            if (midimapFiles[i] == kit->midimapFile)
                midimapBox.setSelectedId (i + 1, juce::dontSendNotification);
        }
    }

    kitImage = {};
    if (kit != nullptr && kit->imageFile.existsAsFile())
        kitImage = juce::ImageFileFormat::loadFrom (kit->imageFile);

    resized();
    repaint();
}

void AlbionDrumsAudioProcessorEditor::setMixerView (bool drumsView)
{
    showDrums = drumsView;
    drums.getUiState().setProperty ("drumsView", drumsView, nullptr);
    micsTab.setToggleState (! drumsView, juce::dontSendNotification);
    drumsTab.setToggleState (drumsView, juce::dontSendNotification);
    for (auto& s : strips)     s->setVisible (! drumsView);
    for (auto& s : drumStrips) s->setVisible (drumsView);
    mixerView.setViewPosition (0, 0);
    resized();
}

//==============================================================================
void AlbionDrumsAudioProcessorEditor::timerCallback()
{
    const auto* kit = drums.getPublishedKit();
    if (kit != shownKit)
    {
        rebuildForKit();
        kit = shownKit;
    }

    if (kit != nullptr)
    {
        auto* mutableKit = const_cast<Kit*> (kit);   // only the UI-facing atomics are touched
        for (auto& pad : padGrid.pads)
        {
            const auto v = mutableKit->hit[(size_t) pad->instrument]->exchange (0.0f, std::memory_order_relaxed);
            if (v > 0.0f)
                pad->setHit (v);
            else if (pad->decay (0.08f))
                pad->repaint();
        }
        if (showDrums)
            for (size_t i = 0; i < drumStrips.size(); ++i)
                drumStrips[i]->setMeter (mutableKit->instrumentMeter[(size_t) drumStripInstrument[i]]->exchange (0.0f, std::memory_order_relaxed));
        else
            for (size_t i = 0; i < strips.size(); ++i)
                strips[i]->setMeter (mutableKit->meter[i].exchange (0.0f, std::memory_order_relaxed));
    }

    if (drums.isLoading())
        repaint (infoArea);
    if (! imageArea.isEmpty() && ! kitImage.isValid())
        repaint (imageArea.withTrimmedTop (juce::jmax (0, imageArea.getHeight() - 24)));
}

//==============================================================================
void AlbionDrumsAudioProcessorEditor::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (colours::panelLight, 0.0f, 0.0f, colours::panel, 0.0f, bounds.getBottom(), false));
    g.fillAll();

    // header: shared Albion Road Audio header (logo, wordmark, product)
    auto header = bounds.removeFromTop ((float) headerH);
    albion::drawHeader (g, header, juce::String::fromUTF8 ("Drums  ·  Kit player for DrumGizmo kits"), 1.0f);

    // --- info bar -------------------------------------------------------------------------
    const auto* kit = drums.getPublishedKit();
    auto info = infoArea.toFloat().reduced (14.0f, 6.0f);
    if (drums.isLoading())
    {
        const auto progress = drums.getLoadProgress();
        g.setColour (colours::cream);
        g.setFont (font (14.0f, true));
        g.drawText ("Loading " + drums.getRequestedKit().getFileNameWithoutExtension() + "  "
                        + juce::String (juce::roundToInt (progress * 100.0f)) + "%",
                    info.removeFromLeft (360.0f), juce::Justification::centredLeft, false);
        auto bar = info.withSizeKeepingCentre (info.getWidth(), 6.0f);
        g.setColour (colours::line);
        g.fillRoundedRectangle (bar, 3.0f);
        g.setColour (colours::brass);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * progress), 3.0f);
    }
    else if (drums.getLastError().isNotEmpty())
    {
        g.setColour (colours::accentDark);
        g.setFont (font (14.0f, true));
        g.drawFittedText (drums.getLastError(), info.toNearestInt(), juce::Justification::centredLeft, 1);
    }
    else if (kit != nullptr)
    {
        g.setColour (colours::cream);
        g.setFont (font (16.0f, true));
        const auto nameW = juce::jmin (420.0f, info.getWidth() * 0.45f);
        g.drawFittedText (kit->name, info.removeFromLeft (nameW).toNearestInt(), juce::Justification::centredLeft, 1);
        g.setColour (colours::cream.withAlpha (0.6f));
        g.setFont (font (13.0f));
        const auto dot = juce::String::fromUTF8 ("  \xc2\xb7  ");   // middle dot; plain literals aren't UTF-8 in juce::String
        const auto stats = juce::String (kit->numChannels()) + " mics" + dot + juce::String ((int) kit->instruments.size())
                         + " instruments" + dot + juce::String (kit->sampleRate / 1000.0, 1) + " kHz" + dot
                         + bytesToText (kit->totalBytes) + " streamed";
        g.drawText (stats, info, juce::Justification::centredRight, false);
    }
    else
    {
        g.setColour (colours::cream.withAlpha (0.6f));
        g.setFont (font (14.0f));
        g.drawText ("No kit loaded. Click LOAD KIT and pick a DrumGizmo kit's drumkit .xml file.",
                    info, juce::Justification::centredLeft, false);
    }

    // --- panels -----------------------------------------------------------------------------
    for (auto r : { padPanel, sidePanel, mixerPanel })
    {
        g.setColour (colours::panelDark.withAlpha (0.35f));
        g.fillRoundedRectangle (r.toFloat(), 10.0f);
    }

    g.setColour (colours::brass);
    g.setFont (font (12.0f, true).withExtraKerningFactor (0.18f));
    g.drawText ("PADS", padPanel.reduced (14, 8).removeFromTop (16), juce::Justification::centredLeft, false);
    g.drawText ("MIXER", mixerPanel.reduced (14, 8).removeFromTop (16), juce::Justification::centredLeft, false);
    g.drawText ("KIT", sidePanel.reduced (14, 8).removeFromTop (16), juce::Justification::centredLeft, false);

    if (kitImage.isValid() && ! imageArea.isEmpty())
    {
        g.saveState();
        juce::Path clip;
        clip.addRoundedRectangle (imageArea.toFloat(), 8.0f);
        g.reduceClipRegion (clip);
        g.drawImage (kitImage, imageArea.toFloat(), juce::RectanglePlacement::centred | juce::RectanglePlacement::fillDestination);
        g.restoreState();
        g.setColour (colours::brassDark);
        g.drawRoundedRectangle (imageArea.toFloat(), 8.0f, 1.0f);
    }
    else if (! imageArea.isEmpty())
    {
        auto r = imageArea.reduced (10, 6);
        g.setColour (colours::panelDark.withAlpha (0.5f));
        g.fillRoundedRectangle (imageArea.toFloat(), 8.0f);

        if (kit != nullptr)
        {
            g.setColour (colours::brass);
            g.setFont (font (12.0f, true).withExtraKerningFactor (0.1f));
            g.drawText ("VOICES  " + juce::String (drums.getActiveVoices()), r.removeFromBottom (16),
                        juce::Justification::bottomLeft, false);
        }

        if (kit != nullptr && kit->description.isNotEmpty() && r.getHeight() >= 80)
        {
            g.setColour (colours::cream.withAlpha (0.75f));
            g.setFont (font (13.0f, true));
            g.drawFittedText (kit->description, r.removeFromTop (36), juce::Justification::topLeft, 2);
        }

        g.setColour (colours::cream.withAlpha (0.5f));
        g.setFont (font (12.0f));
        const juce::String tips = kit != nullptr
            ? juce::String ("Click a pad to audition: top of the pad = hard hit.\n"
                            "Poly aftertouch chokes cymbals. Hi-hats choke each other.\n"
                            "Enable the Direct outputs in your host for per-mic tracks.")
            : juce::String ("Plays DrumGizmo kits (drumgizmo.org) in any CLAP or VST3 host.");
        g.drawFittedText (tips, r, juce::Justification::topLeft, juce::jmax (1, r.getHeight() / 15), 0.9f);
    }
}

void AlbionDrumsAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop (headerH);
    themeToggle.setBounds (header.removeFromRight (50).withSizeKeepingCentre (30, 30));
    auto controls = header.reduced (14, 13);
    const auto comboW = juce::jlimit (130, 190, (getWidth() - 500 - 104 - 16) / 2);
    midimapBox.setBounds (controls.removeFromRight (comboW));
    controls.removeFromRight (8);
    variantBox.setBounds (controls.removeFromRight (comboW));
    controls.removeFromRight (8);
    loadButton.setBounds (controls.removeFromRight (104));

    infoArea = area.removeFromTop (infoH);

    auto body = area.reduced (12, 0).withTrimmedBottom (12);
    mixerPanel = body.removeFromBottom (juce::jmax (juce::jmin (mixerH, body.getHeight() / 2), (int) ((float) body.getHeight() * 0.45f)));
    body.removeFromBottom (12);
    sidePanel = body.removeFromRight (juce::jmax (300, body.getWidth() / 3));
    body.removeFromRight (12);
    padPanel = body;

    // pads
    auto padInner = padPanel.reduced (10).withTrimmedTop (20);
    padView.setBounds (padInner);
    const auto padW = padInner.getWidth() - padView.getScrollBarThickness();
    padGrid.setSize (padW, juce::jmax (padInner.getHeight(), padGrid.layoutFor (padW)));

    // side: knobs (3 + 2) and the kit image / description
    auto side = sidePanel.reduced (10).withTrimmedTop (22);
    auto row1 = side.removeFromTop (juce::jmin (100, side.getHeight() / 2));
    side.removeFromTop (6);
    auto row2 = side.removeFromTop (juce::jmin (100, side.getHeight()));
    auto place = [] (Knob& k, juce::Rectangle<int> r)
    {
        k.label.setBounds (r.removeFromTop (16));
        k.slider.setBounds (r.reduced (4, 0));
    };
    const auto w1 = row1.getWidth() / 3;
    place (knobs[kMaster],   row1.removeFromLeft (w1));
    place (knobs[kTune],     row1.removeFromLeft (w1));
    place (knobs[kDynamics], row1);
    const auto w2 = row2.getWidth() / 3;
    place (knobs[kBleed],    row2.removeFromLeft (w2));
    place (knobs[kHumanize], row2.removeFromLeft (w2));
    imageArea = side.reduced (4, 4);
    if (imageArea.getHeight() < 40)
        imageArea = {};

    // mixer: MICS / DRUMS tabs beside the panel title
    auto tabs = mixerPanel.reduced (14, 6).removeFromTop (22).withTrimmedLeft (70);
    micsTab.setBounds (tabs.removeFromLeft (64));
    tabs.removeFromLeft (6);
    drumsTab.setBounds (tabs.removeFromLeft (72));

    auto mixInner = mixerPanel.reduced (10).withTrimmedTop (22);
    mixerView.setBounds (mixInner);
    const auto stripH = mixInner.getHeight() - mixerView.getScrollBarThickness();
    const auto& shown = showDrums ? drumStrips : strips;
    const auto width  = showDrums ? drumStripW : stripW;
    mixerContent.setSize (juce::jmax (mixInner.getWidth(), (int) shown.size() * width), stripH);
    for (size_t i = 0; i < shown.size(); ++i)
        shown[i]->setBounds ((int) i * width, 0, width, stripH);
}
