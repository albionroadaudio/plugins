// Dev tool: measures every stage of the Vocal Multitool against the recipe.
//   AlbionVocalMultitoolDspTest
#include <juce_audio_processors/juce_audio_processors.h>
#include "../src/PluginProcessor.h"
#include "../src/Presets.h"
#include <chrono>
#include <random>

namespace
{
    constexpr double sr = 48000.0;
    constexpr int block = 256;
    int failures = 0;

    void check (bool ok, const char* what, double value, const char* unit)
    {
        std::printf ("  [%s] %-58s %8.2f %s\n", ok ? "PASS" : "FAIL", what, value, unit);
        if (! ok) ++failures;
    }

    std::unique_ptr<VocalMultitoolProcessor> make (int preset)
    {
        auto p = std::make_unique<VocalMultitoolProcessor>();
        p->setCurrentProgram (preset);
        p->prepareToPlay (sr, block);
        return p;
    }

    void setParam (VocalMultitoolProcessor& p, const juce::ParameterID& id, float value)
    {
        auto* param = p.apvts.getParameter (id.getParamID());
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    // Clean Slate (amounts at zero), with the section switches back on so a single knob can be tested.
    void allOff (VocalMultitoolProcessor& p)
    {
        p.setCurrentProgram ((int) presets::factory.size() - 1);
        for (const auto* id : { &ids::denoiseOn, &ids::comp1On, &ids::deessOn, &ids::eqOn, &ids::comp2On })
            setParam (p, *id, 1.0f);
    }

    // Runs a mono signal (duplicated to stereo) through; returns the left output.
    std::vector<float> run (VocalMultitoolProcessor& p, const std::vector<float>& in, float* maxGr = nullptr,
                            VocalMultitoolProcessor::Meter m = VocalMultitoolProcessor::mClamp, size_t grFrom = 0)
    {
        std::vector<float> out (in.size());
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        for (size_t s = 0; s + block <= in.size(); s += block)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                    buf.setSample (ch, i, in[s + (size_t) i]);
            p.processBlock (buf, midi);
            const auto gr = p.takeMeterDb (m);
            if (maxGr != nullptr && s >= grFrom)
                *maxGr = std::max (*maxGr, gr);
            for (int i = 0; i < block; ++i)
                out[s + (size_t) i] = buf.getSample (0, i);
        }
        return out;
    }

    double rmsDb (const std::vector<float>& x, size_t a, size_t b)
    {
        double sum = 0.0;
        for (auto i = a; i < b; ++i) sum += (double) x[i] * x[i];
        return 10.0 * std::log10 (sum / (double) (b - a) + 1e-30);
    }

    double peakDb (const std::vector<float>& x, size_t a, size_t b)
    {
        float pk = 0.0f;
        for (auto i = a; i < b; ++i) pk = std::max (pk, std::abs (x[i]));
        return 20.0 * std::log10 (pk + 1e-30);
    }

    std::vector<float> sine (double f, double amp, double seconds)
    {
        std::vector<float> x ((size_t) (seconds * sr));
        for (size_t i = 0; i < x.size(); ++i)
            x[i] = (float) (amp * std::sin (2.0 * juce::MathConstants<double>::pi * f * (double) i / sr));
        return x;
    }

    // "Sung" syllables: a 220 Hz harmonic tone (1/k partials up to 4 kHz), 350 ms on / 250 ms off,
    // peak levels cycling through -6, -12, -9, -15 dBFS.
    std::vector<float> syllables (double seconds, bool withGaps = true)
    {
        std::vector<float> x ((size_t) (seconds * sr));
        const double levels[] = { -6.0, -12.0, -9.0, -15.0 };
        for (size_t i = 0; i < x.size(); ++i)
        {
            const auto t = (double) i / sr;
            const auto syl = (int) (t / 0.6);
            const auto ph = std::fmod (t, 0.6);
            double env = withGaps ? (ph < 0.35 ? std::sin (juce::MathConstants<double>::pi * ph / 0.35) : 0.0) : 1.0;
            env *= std::pow (10.0, levels[syl % 4] / 20.0) / 1.9;   // partial sum peaks near 1.9
            double v = 0.0;
            for (int k = 1; k * 220.0 < 4000.0; ++k)
                v += std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * k * t + 0.3 * k) / k;
            x[i] = (float) (env * v);
        }
        return x;
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    std::mt19937 rng (1);
    std::normal_distribution<float> gauss (0.0f, 1.0f);

    // ---------------------------------------------------------------------------
    std::puts ("Latency / transparency");
    {
        auto p = make ((int) presets::factory.size() - 1);
        const auto lat = p->getLatencySamples();
        check (lat == 2048, "reported latency at 48 kHz", lat, "samples");
        std::vector<float> in (48128);
        for (auto& v : in) v = 0.1f * gauss (rng);
        const auto out = run (*p, in);
        double err = 0.0, ref = 0.0;
        for (size_t i = 8000; i < in.size(); ++i)
        {
            const auto e = out[i] - in[i - (size_t) lat];
            err += (double) e * e; ref += (double) in[i - (size_t) lat] * in[i - (size_t) lat];
        }
        check (10.0 * std::log10 (err / ref) < -90.0, "all stages off: output = input delayed (error)", 10.0 * std::log10 (err / ref), "dB");
    }

    // ---------------------------------------------------------------------------
    std::puts ("1. Denoise");
    {
        auto p = make (0); allOff (*p);
        setParam (*p, ids::denoise, 100.0f);
        auto in = syllables (6.0);
        std::vector<float> noise (in.size());
        for (auto& v : noise) v = 0.000316f * gauss (rng);   // -70 dBFS white noise
        for (size_t i = 0; i < in.size(); ++i) in[i] += noise[i];
        const auto out = run (*p, in);
        const auto lat = (size_t) p->getLatencySamples();
        // gap 4.8 + 0.40 .. 0.55 s; syllable 4.8 + 0.10 .. 0.25 s
        const auto g0 = (size_t) ((4.8 + 0.47) * sr), g1 = (size_t) ((4.8 + 0.58) * sr);
        const auto s0 = (size_t) ((4.8 + 0.10) * sr), s1 = (size_t) ((4.8 + 0.25) * sr);
        const auto noiseCut = rmsDb (in, g0, g1) - rmsDb (out, g0 + lat, g1 + lat);
        check (noiseCut > 15.0, "noise in the gaps (-70 dBFS white) reduced by", noiseCut, "dB");
        const auto vocalDiff = rmsDb (out, s0 + lat, s1 + lat) - rmsDb (in, s0, s1);
        check (std::abs (vocalDiff) < 0.5, "vocal level change", vocalDiff, "dB");

        {
            auto r = make (0); allOff (*r);
            setParam (*r, ids::denoise, 100.0f);
            std::vector<float> nz (96000);
            for (auto& v : nz) v = 0.000316f * gauss (rng);
            const auto o = run (*r, nz);
            const auto steady = rmsDb (nz, 40000, 90000) - rmsDb (o, 40000 + lat, 90000 + lat);
            check (steady > 18.0, "steady -70 dBFS noise alone reduced by", steady, "dB");
        }

        auto q = make (0); allOff (*q);
        setParam (*q, ids::denoise, 100.0f);
        const auto loudNoise = [&] { std::vector<float> v (in.size()); for (auto& s : v) s = 0.003f * gauss (rng); return v; }();
        const auto outLoud = run (*q, loudNoise);
        const auto d = rmsDb (outLoud, g0 + lat, g1 + lat) - rmsDb (loudNoise, g0, g1);
        check (std::abs (d) < 1.0, "noise well above threshold (-50 dBFS) untouched", d, "dB");
    }

    // ---------------------------------------------------------------------------
    std::puts ("2. High-pass 120 Hz");
    for (auto [f, expect, tol] : { std::tuple { 60.0, -24.1, 0.5 }, std::tuple { 120.0, -3.0, 0.3 }, std::tuple { 1000.0, 0.0, 0.1 } })
    {
        auto p = make (0); allOff (*p);
        setParam (*p, ids::hpf, 1.0f);
        const auto in = sine (f, 0.1, 1.0);
        const auto out = run (*p, in);
        const auto g = rmsDb (out, 30000, 46000) - rmsDb (in, 30000, 46000);
        char what[80]; std::snprintf (what, sizeof what, "gain at %.0f Hz (expect %.1f)", f, expect);
        check (std::abs (g - expect) < tol, what, g, "dB");
    }

    // ---------------------------------------------------------------------------
    std::puts ("3. Clamp compressor (1 ms / 80 ms / 4:1, -10 dB on loudest peaks, +10 makeup)");
    {
        auto p = make (0); allOff (*p);
        setParam (*p, ids::comp1, 100.0f);
        const auto in = syllables (12.0);
        float gr = 0.0f;
        const auto out = run (*p, in, &gr, VocalMultitoolProcessor::mClamp, (size_t) (4.0 * sr));
        check (gr > 9.0f && gr < 10.5f, "max gain reduction on the loudest syllables", gr, "dB");
        const auto lat = (size_t) p->getLatencySamples();
        const auto a = (size_t) (6.0 * sr), b = (size_t) (11.0 * sr);
        const auto pk = peakDb (out, a + lat, b + lat) - peakDb (in, a, b);
        check (std::abs (pk) < 2.0, "loudest peak level in -> out", pk, "dB");
        // quietest syllable (-15 dBFS) is raised relative to the loudest (-6): dynamic range squeezed
        // syllable 11 (6.6 s) is the -15 dBFS one, syllable 12 (7.2 s) a -6 dBFS one
        const auto q0 = (size_t) (6.6 * sr), q1 = (size_t) (6.95 * sr), l0 = (size_t) (7.2 * sr), l1 = (size_t) (7.55 * sr);
        const auto inRange  = peakDb (in, l0, l1) - peakDb (in, q0, q1);
        const auto outRange = peakDb (out, l0 + lat, l1 + lat) - peakDb (out, q0 + lat, q1 + lat);
        check (outRange < inRange - 4.0, "loud-to-quiet syllable range (was 9 dB)", outRange, "dB");
        std::printf ("      (input range %.1f dB)\n", inRange);
    }

    // ---------------------------------------------------------------------------
    std::puts ("4. De-esser");
    {
        auto vowel = make (0); allOff (*vowel);
        setParam (*vowel, ids::deess, 50.0f);
        float grVowel = 0.0f;
        run (*vowel, syllables (3.0), &grVowel, VocalMultitoolProcessor::mDeEss, 4800);
        check (grVowel < 1.0f, "reduction on vowels (should stay ~0)", grVowel, "dB");

        // an "s": noise band-passed to 5-9 kHz, mixed under a vowel at -20 dB
        auto ess = make (0); allOff (*ess);
        setParam (*ess, ids::deess, 50.0f);
        vmt::Biquad bp; bp.setBandpass (7000.0, 1.5, sr);
        auto in = syllables (3.0, false);
        for (auto& v : in) { v *= 0.1f; v += 2.0f * bp.process (0.1f * gauss (rng), 0); }
        float grEss = 0.0f;
        const auto out = run (*ess, in, &grEss, VocalMultitoolProcessor::mDeEss, 9600);
        check (grEss > 4.0f, "reduction on a sustained \"s\" at 50 %", grEss, "dB");
        // the sibilance band in the output is lower
        vmt::Biquad m1, m2; m1.setBandpass (7000.0, 1.5, sr); m2.setBandpass (7000.0, 1.5, sr);
        std::vector<float> bi (in.size()), bo (in.size());
        for (size_t i = 0; i < in.size(); ++i) { bi[i] = m1.process (in[i], 0); bo[i] = m2.process (out[i], 0); }
        const auto lat = (size_t) ess->getLatencySamples();
        const auto cut = rmsDb (bi, 48000, 120000) - rmsDb (bo, 48000 + lat, 120000 + lat);
        check (cut > 4.0, "7 kHz band reduction at 50 %", cut, "dB");
    }

    // ---------------------------------------------------------------------------
    std::puts ("5. EQ (defaults: mud -3 @200 wide, nasal -3 @2.6k narrow, air +3 shelf @11k)");
    {
        const auto& d = presets::factory[0];
        for (auto [f, expect, tol] : { std::tuple { 200.0, (double) d.mud, 0.3 }, std::tuple { 2600.0, (double) d.nasal, 0.3 },
                                       std::tuple { 1000.0, 0.0, 0.5 }, std::tuple { 16000.0, (double) d.air, 0.6 } })
        {
            auto p = make (0); allOff (*p);
            setParam (*p, ids::mud, d.mud); setParam (*p, ids::nasal, d.nasal); setParam (*p, ids::air, d.air);
            const auto in = sine (f, 0.05, 1.0);
            const auto out = run (*p, in);
            const auto g = rmsDb (out, 30000, 46000) - rmsDb (in, 30000, 46000);
            char what[80]; std::snprintf (what, sizeof what, "gain at %.0f Hz (expect %+.1f)", f, expect);
            check (std::abs (g - expect) < tol, what, g, "dB");
        }
    }

    // ---------------------------------------------------------------------------
    std::puts ("6. Glue (30 ms / 30 ms, threshold -60, ~-5 dB, makeup to match)");
    {
        auto p = make (0); allOff (*p);
        setParam (*p, ids::comp2, 100.0f);
        const auto in = syllables (12.0);
        float gr = 0.0f;
        const auto out = run (*p, in, &gr, VocalMultitoolProcessor::mGlue, (size_t) (4.0 * sr));
        check (gr > 4.0f && gr < 5.5f, "max gain reduction", gr, "dB");
        const auto lat = (size_t) p->getLatencySamples();
        const auto a = (size_t) (6.0 * sr), b = (size_t) (11.0 * sr);
        const auto lvl = peakDb (out, a + lat, b + lat) - peakDb (in, a, b);
        check (std::abs (lvl) < 1.5, "loudest peak level in -> out", lvl, "dB");
    }

    // ---------------------------------------------------------------------------
    std::puts ("Section switches");
    {
        // default preset with every section switched off (high-pass too) = clean, delayed signal
        auto p = make (0);
        for (const auto* id : { &ids::denoiseOn, &ids::hpf, &ids::comp1On, &ids::deessOn, &ids::eqOn, &ids::comp2On })
            setParam (*p, *id, 0.0f);
        std::vector<float> in (96000);
        for (auto& v : in) v = 0.1f * gauss (rng);
        const auto out = run (*p, in);
        const auto lat = (size_t) p->getLatencySamples();
        double err = 0.0, ref = 0.0;
        for (size_t i = 24000; i < in.size(); ++i)
        {
            const auto e = out[i] - in[i - lat];
            err += (double) e * e; ref += (double) in[i - lat] * in[i - lat];
        }
        check (10.0 * std::log10 (err / ref) < -90.0, "all switches off (amounts at 100 %): output = input (error)", 10.0 * std::log10 (err / ref), "dB");

        // switching the compressor off mid-signal fades rather than jumps:
        // the biggest sample-to-sample step is no bigger than in the same signal left alone
        auto maxStepAround = [&] (bool doSwitch)
        {
            auto q = make (0); allOff (*q);
            setParam (*q, ids::comp1, 100.0f);
            const auto syl = syllables (4.0, false);
            juce::AudioBuffer<float> buf (2, block);
            juce::MidiBuffer midi;
            float prev = 0.0f, maxStep = 0.0f;
            for (size_t s0 = 0; s0 + block <= syl.size(); s0 += block)
            {
                if (doSwitch && s0 == 96000) setParam (*q, ids::comp1On, 0.0f);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < block; ++i) buf.setSample (ch, i, syl[s0 + (size_t) i]);
                q->processBlock (buf, midi);
                for (int i = 0; i < block; ++i)
                {
                    if (s0 >= 96000 + 2048 && s0 < 96000 + 2048 + 4800) maxStep = std::max (maxStep, std::abs (buf.getSample (0, i) - prev));
                    prev = buf.getSample (0, i);
                }
            }
            return maxStep;
        };
        const auto still = maxStepAround (false), switched = maxStepAround (true);
        const auto maxStep = switched / still;
        check (maxStep < 1.25f, "biggest step while switching off vs not switching", maxStep, "x");
    }

    std::puts ("Whole chain (default preset)");
    {
        auto p = make (0);
        auto in = syllables (20.0);
        for (auto& v : in) v += 0.000316f * gauss (rng);
        bool finite = true;
        const auto t0 = std::chrono::steady_clock::now();
        const auto out = run (*p, in);
        const auto secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        for (auto v : out) finite = finite && std::isfinite (v) && std::abs (v) < 4.0f;
        check (finite, "output finite and bounded", peakDb (out, 0, out.size()), "dBFS peak");
        check (secs / 20.0 * 100.0 < 5.0, "CPU, stereo 48 kHz", secs / 20.0 * 100.0, "% of one core");
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASS" : "FAILURES", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
