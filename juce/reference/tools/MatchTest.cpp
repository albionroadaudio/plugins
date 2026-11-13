// Headless functional test of the reference analyser + gentle match EQ.
#include "Common.h"
#include <chrono>

using namespace spectrum;

static float at (const Curve& c, float hz)
{
    int best = 0;
    for (int b = 0; b < numBands; ++b)
        if (std::abs (std::log2 (bandHz (b) / hz)) < std::abs (std::log2 (bandHz (best) / hz)))
            best = b;
    return c[(size_t) best];
}

static void printCurve (const char* label, const Curve& c)
{
    std::printf ("  %-26s", label);
    for (float hz : { 30.f, 60.f, 125.f, 250.f, 500.f, 1000.f, 2000.f, 4000.f, 8000.f, 14000.f })
        std::printf (" %6.1f", at (c, hz));
    std::puts ("");
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    auto proc = std::make_unique<AlbionReferenceAudioProcessor>();
    auto& ref = *proc;
    const double sr = 48000.0;
    proc->prepareToPlay (sr, 4096);
    Noise noise;

    std::printf ("latency: %d samples\n", proc->getLatencySamples());
    std::puts ("bands (Hz):                     30     60    125    250    500     1k     2k     4k     8k    14k");

    // --- learn a white-noise "mix" -----------------------------------------------------------
    feed (*proc, noise, 1.0, 0.1f);
    ref.startLearn();
    feed (*proc, noise, 8.0, 0.1f);
    ref.stopLearn();
    pumpMessages (50);
    std::printf ("learned %.1f s (white noise mix)\n", ref.getLearnedSeconds());
    printCurve ("learned mix (normalised)", ref.getLearnedMix());

    // --- reference: pink noise profile; limit 6 dB, amount 100 % -----------------------
    setParam (ref.apvts, ids::maxDb, 6.0f);
    setParam (ref.apvts, ids::amount, 100.0f);
    ref.setReference ("Pink", profiles::builtIns()[0].curve);
    pumpMessages (200);   // timer picks up the limit change and refits
    printCurve ("reference (pink)", ref.getReferenceCurve());
    printCurve ("target (limit +-6)", ref.getEqTarget());
    printCurve ("fitted EQ response", ref.getEqResponse());

    float worst = 0.0f;
    for (int b = 0; b < numBands; ++b)
        if (bandHz (b) >= 40.0f && bandHz (b) <= 12000.0f)
            worst = juce::jmax (worst, std::abs (ref.getEqTarget()[(size_t) b] - ref.getEqResponse()[(size_t) b]));
    std::printf ("fit error 40 Hz-12 kHz: max %.2f dB\n", worst);

    // --- does the audio path apply it? measure output vs input spectrum ---------------
    feed (*proc, noise, 6.0, 0.1f);   // let the EQ slew and the display settle
    Curve pre {}, post {};
    ref.analysis.getDisplay (pre, post);
    Curve applied {};
    for (int b = 0; b < numBands; ++b)
        applied[(size_t) b] = post[(size_t) b] - pre[(size_t) b];
    printCurve ("measured output - input", smoothed (applied, 0.33f));

    // --- limit 3 dB, amount 50 % (the gentle defaults) ----------------------------------
    setParam (ref.apvts, ids::maxDb, 3.0f);
    setParam (ref.apvts, ids::amount, 50.0f);
    pumpMessages (200);
    auto gentle = ref.getEqResponse();
    for (auto& v : gentle) v *= 0.5f;
    printCurve ("defaults: applied EQ", gentle);

    // --- EQ off: bit-exact pass-through ----------------------------------------------
    setParam (ref.apvts, ids::eqOn, 0.0f);
    {
        juce::AudioBuffer<float> buf (2, 4096), copy (2, 4096);
        juce::MidiBuffer midi;
        for (int b = 0; b < 40; ++b)   // let the EQ slew to flat
        {
            for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 4096; ++i) buf.setSample (ch, i, noise.next (0.1f));
            proc->processBlock (buf, midi);
        }
        float diff = 0.0f;
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 4096; ++i) buf.setSample (ch, i, noise.next (0.1f));
        copy.makeCopyOf (buf);
        proc->processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 4096; ++i) diff = juce::jmax (diff, std::abs (buf.getSample (ch, i) - copy.getSample (ch, i)));
        std::printf ("EQ off: max |out-in| = %g\n", diff);
    }
    setParam (ref.apvts, ids::eqOn, 1.0f);

    // --- capture: play a pink-ish "reference" through and capture it ------------------
    Noise pinkish; pinkish.pinkish = true;
    ref.startCapture();
    feed (*proc, pinkish, 6.0, 0.3f);
    ref.stopCapture();
    pumpMessages (50);
    std::printf ("captured reference: \"%s\"\n", ref.getReferenceName().toRawUTF8());
    printCurve ("captured (normalised)", ref.getReferenceCurve());

    // --- file analysis: write a white-noise WAV and load it as reference --------------
    {
        const auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("albion_ref_test.wav");
        f.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), 44100.0, 2, 24, {}, 0));
        juce::AudioBuffer<float> b (2, 44100);
        Noise wn;
        for (int s = 0; s < 10; ++s)
        {
            for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 44100; ++i) b.setSample (ch, i, wn.next (0.1f));
            w->writeFromAudioSampleBuffer (b, 0, 44100);
        }
        w.reset();
        ref.loadReferenceFile (f);
        for (int i = 0; i < 100 && (ref.isAnalysingFile() || ref.getReferenceName().startsWith ("Captured")); ++i)
            pumpMessages (50);
        std::printf ("file reference: \"%s\"\n", ref.getReferenceName().toRawUTF8());
        printCurve ("white-noise WAV (normalised)", ref.getReferenceCurve());
        f.deleteFile();
    }

    // --- CPU of the audio path with the EQ active ----------------------------------------
    setParam (ref.apvts, ids::amount, 100.0f);
    setParam (ref.apvts, ids::maxDb, 6.0f);
    ref.setReference ("Pink", profiles::builtIns()[0].curve);
    pumpMessages (200);
    {
        juce::AudioBuffer<float> buf (2, 256);
        juce::MidiBuffer midi;
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 256; ++i) buf.setSample (ch, i, noise.next (0.1f));
        for (int i = 0; i < 400; ++i) proc->processBlock (buf, midi);   // slew to target
        const int blocks = (int) (60.0 * sr / 256);
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < blocks; ++i) proc->processBlock (buf, midi);
        const auto s = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("CPU, audio thread, EQ active (15 bands, stereo, 48 kHz): %.3f%% of one core\n", 100.0 * s / 60.0);
    }
    proc->releaseResources();
    proc.reset();
    std::puts ("OK");
    return 0;
}
