#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace spectrum;

AlbionReferenceAudioProcessor::AlbionReferenceAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "AlbionReferenceState", createParameterLayout())
{
    amountParam    = apvts.getRawParameterValue (ids::amount.getParamID());
    maxDbParam     = apvts.getRawParameterValue (ids::maxDb.getParamID());
    smoothingParam = apvts.getRawParameterValue (ids::smoothing.getParamID());
    eqOnParam      = apvts.getRawParameterValue (ids::eqOn.getParamID());
    outputParam    = apvts.getRawParameterValue (ids::output.getParamID());
    for (auto& g : eqGains)
        g = 0.0f;
    startTimerHz (10);
}

AlbionReferenceAudioProcessor::~AlbionReferenceAudioProcessor()
{
    stopTimer();
    cancelFileJob = true;
    pool.removeAllJobs (true, 5000);
}

juce::AudioProcessorValueTreeState::ParameterLayout AlbionReferenceAudioProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterFloat> (ids::amount, "Match Amount", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " %"; }).withLabel ("%")));

    layout.add (std::make_unique<AudioParameterFloat> (ids::maxDb, "Match Limit", NormalisableRange<float> (1.0f, 6.0f, 0.1f), 3.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String::fromUTF8 ("\xc2\xb1") + String (v, 1) + " dB"; }).withLabel ("dB")));

    layout.add (std::make_unique<AudioParameterFloat> (ids::smoothing, "Match Smoothing", NormalisableRange<float> (0.33f, 2.0f, 0.01f), 1.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 2) + " oct"; }).withLabel ("oct")));

    layout.add (std::make_unique<AudioParameterBool> (ids::eqOn, "Match EQ On", true));

    layout.add (std::make_unique<AudioParameterFloat> (ids::output, "Output", NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v > 0 ? "+" : "") + String (v, 1) + " dB"; }).withLabel ("dB")));

    layout.add (std::make_unique<AudioParameterChoice> (ids::tilt, "Analyser Tilt",
        StringArray { "0 dB/oct", "3 dB/oct (pink flat)", "4.5 dB/oct" }, 1));

    return layout;
}

//==============================================================================
bool AlbionReferenceAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

void AlbionReferenceAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    analysis.setSampleRate (sampleRate);
    eq.prepare (sampleRate, 2);
    preCopy.setSize (2, juce::jmax (64, samplesPerBlock), false, true, false);
    outputGain.reset (sampleRate, 0.05);
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outputParam->load()));
    setLatencySamples (0);
}

void AlbionReferenceAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numIn  = getTotalNumInputChannels();
    const auto numOut = getTotalNumOutputChannels();
    for (auto ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    const int channels = juce::jmin (2, numIn);
    if (channels == 0)
        return;

    // target band gains = fitted gains x amount (or flat when off)
    const auto scale = (*eqOnParam > 0.5f ? amountParam->load() * 0.01f : 0.0f);
    matcheq::Gains target {};
    for (int i = 0; i < matcheq::numBands; ++i)
        target[(size_t) i] = eqGains[(size_t) i].load (std::memory_order_relaxed) * scale;
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (outputParam->load()));

    // process in chunks that fit the analysis copy buffer
    const int total = buffer.getNumSamples();
    for (int start = 0; start < total;)
    {
        const int n = juce::jmin (preCopy.getNumSamples(), total - start);
        float* ch[2] = { buffer.getWritePointer (0, start), buffer.getWritePointer (channels > 1 ? 1 : 0, start) };

        for (int c = 0; c < channels; ++c)
            preCopy.copyFrom (c, 0, ch[c], n);

        eq.process (ch, channels, n, target);

        if (outputGain.isSmoothing() || std::abs (outputGain.getTargetValue() - 1.0f) > 1.0e-6f)
            for (int i = 0; i < n; ++i)
            {
                const auto g = outputGain.getNextValue();
                for (int c = 0; c < channels; ++c)
                    ch[c][i] *= g;
            }

        const float* pre[2] = { preCopy.getReadPointer (0), preCopy.getReadPointer (channels > 1 ? 1 : 0) };
        analysis.push (pre[0], pre[1], ch[0], ch[1], n);
        start += n;
    }
}

//==============================================================================
void AlbionReferenceAudioProcessor::setStatus (const juce::String& s)
{
    statusMessage = s;
    sendChangeMessage();
}

void AlbionReferenceAudioProcessor::setReference (const juce::String& name, const Curve& curve)
{
    referenceName = name;
    referenceCurve = normalised (curve);
    referenceValid = true;
    recomputeEq();
    setStatus ("Reference: " + name);
}

void AlbionReferenceAudioProcessor::clearReference()
{
    referenceValid = false;
    referenceName.clear();
    recomputeEq();
    setStatus ("Reference cleared");
}

void AlbionReferenceAudioProcessor::startCapture()
{
    analysis.start (AnalysisEngine::capture);
    setStatus ("Capturing reference: play your reference track through the plugin...");
}

bool AlbionReferenceAudioProcessor::stopCapture()
{
    Curve c {};
    const auto seconds = analysis.secondsHeard (AnalysisEngine::capture);
    if (! analysis.stop (AnalysisEngine::capture, c))
    {
        setStatus ("Capture stopped: nothing was heard (play audio above -60 dBFS while capturing)");
        return false;
    }
    setReference ("Captured " + juce::Time::getCurrentTime().formatted ("%H:%M") + " (" + juce::String (juce::roundToInt (seconds)) + " s)", c);
    return true;
}

void AlbionReferenceAudioProcessor::loadReferenceFile (const juce::File& file)
{
    if (fileJobRunning.exchange (true))
        return;
    cancelFileJob = false;
    fileProgress = 0.0f;
    setStatus ("Analysing " + file.getFileName() + "...");

    pool.addJob ([this, file]
    {
        Curve c {};
        juce::String error;
        const bool ok = profiles::analyseFile (file, c, error, fileProgress, [this] { return ! cancelFileJob.load(); });
        const juce::ScopedLock sl (fileLock);
        fileResult = c;
        fileResultOk = ok;
        fileResultError = error;
        fileResultName = file.getFileNameWithoutExtension();
        fileResultReady = true;
        fileJobRunning = false;
    });
}

void AlbionReferenceAudioProcessor::startLearn()
{
    analysis.start (AnalysisEngine::learn);
    setStatus ("Learning your mix: play the loudest representative section, then press LEARN again");
}

bool AlbionReferenceAudioProcessor::stopLearn()
{
    Curve c {};
    const auto seconds = analysis.secondsHeard (AnalysisEngine::learn);
    if (! analysis.stop (AnalysisEngine::learn, c))
    {
        setStatus ("Learn stopped: nothing was heard (play your mix while learning)");
        return false;
    }
    mixCurve = normalised (c);
    mixValid = true;
    mixSeconds = seconds;
    recomputeEq();
    setStatus ("Learned " + juce::String (juce::roundToInt (seconds)) + " s of your mix"
               + (referenceValid ? juce::String() : juce::String (": now choose a reference")));
    return true;
}

void AlbionReferenceAudioProcessor::clearLearnedMix()
{
    mixValid = false;
    mixSeconds = 0.0;
    recomputeEq();
    setStatus ("Learned mix cleared");
}

std::vector<profiles::Profile> AlbionReferenceAudioProcessor::getProfiles() const
{
    auto all = profiles::builtIns();
    for (auto& p : profiles::loadUserProfiles())
        all.push_back (std::move (p));
    return all;
}

juce::File AlbionReferenceAudioProcessor::saveReferenceAsProfile (const juce::String& name)
{
    if (! referenceValid)
        return {};
    auto f = profiles::save (name, referenceCurve);
    referenceName = name.trim();
    setStatus ("Saved profile \"" + referenceName + "\"");
    return f;
}

void AlbionReferenceAudioProcessor::recomputeEq()
{
    matcheq::Gains gains {};
    if (referenceValid && mixValid)
    {
        matcheq::Settings s;
        s.maxDb = maxDbParam->load();
        s.smoothingOct = smoothingParam->load();
        eqTarget = matcheq::targetCurve (referenceCurve, mixCurve, s);
        gains = matcheq::fit (eqTarget, s.maxDb);
    }
    else
        eqTarget.fill (0.0f);

    eqResponse = matcheq::response (gains);
    for (int i = 0; i < matcheq::numBands; ++i)
        eqGains[(size_t) i] = gains[(size_t) i];
    lastMax = maxDbParam->load();
    lastSmoothing = smoothingParam->load();
    sendChangeMessage();
}

void AlbionReferenceAudioProcessor::timerCallback()
{
    if (! juce::approximatelyEqual (lastMax, maxDbParam->load()) || ! juce::approximatelyEqual (lastSmoothing, smoothingParam->load()))
        recomputeEq();

    bool ready = false, ok = false;
    Curve c {};
    juce::String name, error;
    {
        const juce::ScopedLock sl (fileLock);
        if (fileResultReady)
        {
            ready = true;
            ok = fileResultOk;
            c = fileResult;
            name = fileResultName;
            error = fileResultError;
            fileResultReady = false;
        }
    }
    if (ready)
    {
        if (ok)
            setReference (name, c);
        else
            setStatus (error);
    }
}

//==============================================================================
juce::AudioProcessorEditor* AlbionReferenceAudioProcessor::createEditor()
{
    return new AlbionReferenceAudioProcessorEditor (*this);
}

void AlbionReferenceAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.removeChild (state.getChildWithName ("REF"), nullptr);
    state.removeChild (state.getChildWithName ("MIX"), nullptr);
    if (referenceValid)
    {
        juce::ValueTree ref ("REF");
        ref.setProperty ("name", referenceName, nullptr);
        ref.setProperty ("curve", toString (referenceCurve), nullptr);
        state.appendChild (ref, nullptr);
    }
    if (mixValid)
    {
        juce::ValueTree mix ("MIX");
        mix.setProperty ("seconds", mixSeconds, nullptr);
        mix.setProperty ("curve", toString (mixCurve), nullptr);
        state.appendChild (mix, nullptr);
    }
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void AlbionReferenceAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    const auto ref = state.getChildWithName ("REF");
    const auto mix = state.getChildWithName ("MIX");
    apvts.replaceState (state);

    Curve c {};
    referenceValid = ref.isValid() && fromString (ref["curve"].toString(), c);
    if (referenceValid)
    {
        referenceCurve = c;
        referenceName = ref["name"].toString();
    }
    mixValid = mix.isValid() && fromString (mix["curve"].toString(), c);
    if (mixValid)
    {
        mixCurve = c;
        mixSeconds = (double) mix["seconds"];
    }
    recomputeEq();
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AlbionReferenceAudioProcessor();
}
