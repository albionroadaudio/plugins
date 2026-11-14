// Dev tool: renders an impulse response for every voice (100% wet) and reports
// the measured RT60 (T20 from the Schroeder curve) against the Decay setting,
// the reverb's energy, a stability check at maximum decay, and CPU use.
//   AlbionGuitarReverbIRTest [out_dir]     (writes <voice>.wav when out_dir is given)
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../src/PluginProcessor.h"
#include <chrono>
#include <cmath>

namespace
{
    void setParam (juce::AudioProcessorValueTreeState& s, const juce::ParameterID& id, float v)
    {
        auto* p = s.getParameter (id.getParamID());
        p->setValueNotifyingHost (p->convertTo0to1 (v));
    }

    // returns T20-based RT60 in seconds, or -1
    double rt60 (const std::vector<double>& e, double sr, int startSample)
    {
        std::vector<double> edc (e.size());
        double acc = 0.0;
        for (size_t i = e.size(); i-- > 0;)
            edc[i] = (acc += e[i]);
        const auto total = edc[(size_t) startSample];
        auto t = [&] (double db) -> double
        {
            const auto target = total * std::pow (10.0, db / 10.0);
            for (size_t i = (size_t) startSample; i < edc.size(); ++i)
                if (edc[i] <= target)
                    return (double) i / sr;
            return -1.0;
        };
        const auto a = t (-5.0), b = t (-25.0);
        return (a < 0 || b < 0) ? -1.0 : (b - a) * 3.0;
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const double sr = 48000.0;
    const int block = 256;
    const juce::File outDir = argc > 1 ? juce::File (juce::String (argv[1])) : juce::File();
    int failures = 0;

    std::printf ("%-30s %8s %8s %8s %10s %8s\n", "voice", "decay", "RT60", "ratio", "energy dB", "peak");
    for (int v = 0; v < voices::count; ++v)
    {
        for (float decay : { voices::table[(size_t) v].decay.lo, voices::table[(size_t) v].decay.def, voices::table[(size_t) v].decay.hi })
        {
            AlbionGuitarReverbAudioProcessor proc;
            proc.setPlayConfigDetails (2, 2, sr, block);
            proc.selectVoice (v);
            setParam (proc.apvts, ids::decay, decay);
            setParam (proc.apvts, ids::predelay, 0.0f);
            setParam (proc.apvts, ids::lowcut, 20.0f);
            setParam (proc.apvts, ids::highcut, 12000.0f);
            setParam (proc.apvts, ids::mix, 100.0f);
            proc.prepareToPlay (sr, block);

            const int len = (int) (sr * 5.0);
            juce::AudioBuffer<float> ir (2, len);
            ir.clear();
            juce::MidiBuffer midi;
            juce::AudioBuffer<float> buf (2, block);
            for (int pos = 0; pos < len; pos += block)
            {
                buf.clear();
                if (pos == 0) { buf.setSample (0, 0, 1.0f); buf.setSample (1, 0, 1.0f); }
                proc.processBlock (buf, midi);
                for (int ch = 0; ch < 2; ++ch)
                    ir.copyFrom (ch, pos, buf, ch, 0, juce::jmin (block, len - pos));
            }

            std::vector<double> e ((size_t) len);
            double energy = 0.0, peak = 0.0;
            int first = -1;
            for (int i = 0; i < len; ++i)
            {
                const double l = ir.getSample (0, i), r = ir.getSample (1, i);
                e[(size_t) i] = l * l + r * r;
                energy += e[(size_t) i];
                peak = std::max (peak, std::max (std::abs (l), std::abs (r)));
                if (first < 0 && e[(size_t) i] > 1e-8)
                    first = i;
            }
            const auto measured = rt60 (e, sr, std::max (0, first));
            std::printf ("%-30s %8.2f %8.2f %8.2f %10.1f %8.3f\n", voices::table[(size_t) v].name, decay, measured,
                         measured / decay, 10.0 * std::log10 (energy / 2.0 + 1e-20), peak);
            if (! std::isfinite (energy)) ++failures;

            if (outDir != juce::File() && std::abs (decay - voices::table[(size_t) v].decay.def) < 1e-6f)
            {
                outDir.createDirectory();
                auto f = outDir.getChildFile (juce::String (voices::table[(size_t) v].name).replaceCharacters (" ()", "___") + ".wav");
                f.deleteFile();
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), sr, 2, 24, {}, 0));
                if (w != nullptr)
                    w->writeFromAudioSampleBuffer (ir, 0, len);
            }
        }
    }

    // --- stability: 20 s of loud noise at maximum decay, then silence must decay -----
    std::printf ("\nstability (3.0 s decay, 20 s of 0 dBFS noise, then 10 s of silence):\n");
    juce::Random rng (1);
    for (int v = 0; v < voices::count; ++v)
    {
        AlbionGuitarReverbAudioProcessor proc;
        proc.setPlayConfigDetails (2, 2, sr, block);
        proc.selectVoice (v);
        setParam (proc.apvts, ids::decay, 3.0f);
        setParam (proc.apvts, ids::lowcut, 20.0f);
        setParam (proc.apvts, ids::highcut, 12000.0f);
        setParam (proc.apvts, ids::mix, 100.0f);
        proc.prepareToPlay (sr, block);
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        double peak = 0.0, tailPeak = 0.0;
        double cpu = 0.0;
        const int loud = (int) (20.0 * sr / block), quiet = (int) (10.0 * sr / block);
        for (int b = 0; b < loud + quiet; ++b)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                    buf.setSample (ch, i, b < loud ? rng.nextFloat() * 2.0f - 1.0f : 0.0f);
            const auto t0 = std::chrono::steady_clock::now();
            proc.processBlock (buf, midi);
            cpu += std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
            const auto m = (double) buf.getMagnitude (0, block);
            peak = std::max (peak, m);
            if (b >= loud + quiet - 10) tailPeak = std::max (tailPeak, m);
        }
        const auto ok = std::isfinite (peak) && peak < 8.0 && tailPeak < 1e-3;
        if (! ok) ++failures;
        std::printf ("  %-30s peak %6.3f  tail %.2e  cpu %.2f%% of one core  %s\n", voices::table[(size_t) v].name,
                     peak, tailPeak, 100.0 * cpu / 30.0, ok ? "ok" : "FAIL");
    }

    std::printf ("\n%s\n", failures == 0 ? "all checks passed" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
