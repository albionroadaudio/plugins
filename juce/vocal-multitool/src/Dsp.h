#pragma once

// DSP building blocks for the Vocal Multitool. Everything here is allocation-free
// once prepare() has run, and stereo-linked (one gain for both channels).

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>
#include <vector>

namespace vmt
{
    constexpr int maxChannels = 2;

    inline double dbToGain (double db) noexcept { return std::pow (10.0, db / 20.0); }
    inline double gainToDb (double g)  noexcept { return 20.0 * std::log10 (g + 1.0e-30); }
    inline double timeCoeff (double seconds, double rate) noexcept { return 1.0 - std::exp (-1.0 / (seconds * rate)); }

    //==========================================================================
    // RBJ biquad, transposed direct form II, coefficients and state in double.
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        std::array<double, maxChannels> z1 {}, z2 {};

        void reset() noexcept { z1 = {}; z2 = {}; }

        float process (float in, int ch) noexcept
        {
            const double x = in;
            const double y = b0 * x + z1[(size_t) ch];
            z1[(size_t) ch] = b1 * x - a1 * y + z2[(size_t) ch];
            z2[(size_t) ch] = b2 * x - a2 * y;
            return (float) y;
        }

        void setLowpass  (double f, double q, double sr) noexcept;
        void setHighpass (double f, double q, double sr) noexcept;
        void setBandpass (double f, double q, double sr) noexcept;
        void setPeak     (double f, double q, double gainDb, double sr) noexcept;
        void setHighShelf (double f, double q, double gainDb, double sr) noexcept;

        // Magnitude response in dB (for tests and the editor's curve).
        double magnitudeDb (double f, double sr) const noexcept;

    private:
        void normalise (double a0) noexcept { b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0; }
    };

    //==========================================================================
    // 1. Spectral denoiser: an STFT downward expander per frequency bin.
    //    Bins whose level sits below the threshold are pulled down by up to
    //    `depth` dB; the vocal itself (far above the floor) passes untouched.
    //    Latency = FFT size (2048 at 44.1/48 kHz, scaled with the sample rate).
    class Denoiser
    {
    public:
        static constexpr double thresholdDb = -60.0;   // white-noise level (dBFS RMS) that sits on the threshold
        static constexpr double kneeDb      = 12.0;    // fully open at -54, fully closed at -66
        static constexpr double maxDepthDb  = 24.0;    // reduction at 100 %

        void prepare (double sampleRate, int numChannels);
        void reset();
        int latency() const noexcept { return fftSize; }

        // amount 0..1. Always runs (also at 0), so the latency never changes.
        void process (juce::AudioBuffer<float>&, int numChannels, float amount) noexcept;

        // Average reduction across bins in the last frame (positive dB, for a meter).
        float lastReductionDb() const noexcept { return meterDb; }

    private:
        void processFrame (int numChannels) noexcept;

        int fftOrder = 11, fftSize = 2048, hop = 512, numBins = 1025;
        std::unique_ptr<juce::dsp::FFT> fft;
        std::vector<float> window;
        std::array<std::vector<float>, maxChannels> inRing, outRing;
        std::array<std::vector<float>, maxChannels> spectrum;   // 2 * fftSize, JUCE layout
        std::vector<double> power, smoothPower, gainDb, gainTmp;
        int pos = 0, hopCounter = 0;
        double powerNorm = 1.0, powerAttack = 0.5, powerRelease = 0.5, gainRelease = 0.1, olaScale = 0.5;
        float amountNow = 1.0f, meterDb = 0.0f;
    };

    //==========================================================================
    // 3 / 6. A one-knob "auto" compressor that does the engineer's
    // "turn it until the meter reads N dB on the loudest bits" step itself.
    // A slow peak tracker remembers how loud the loudest moments are; the
    // threshold (Clamp) or ratio (Glue) is then placed so that those moments
    // get exactly `targetDb` of reduction, and `targetDb` of makeup restores level.
    class AutoComp
    {
    public:
        enum class Style { Clamp, Glue };

        struct Settings
        {
            Style  style;
            double attackMs, releaseMs;
            bool   rmsDetector;
            double ratio;          // Clamp: fixed ratio
            double thresholdDb;    // Glue: fixed threshold ("all the way down")
            double maxTargetDb;    // reduction on the loudest moments at amount = 100 %
            double kneeDb;
            double trackerFloorDb; // the tracker never drops below this
        };

        explicit AutoComp (Settings s) : set (s) {}

        void prepare (double sampleRate);
        void reset();

        // `in` = this sample's detector input (max |x| over channels).
        // Returns the gain to apply (linear, makeup included).
        float process (float in, float amount) noexcept;

        float grDb() const noexcept { return (float) gr; }

    private:
        double staticGr (double levelDb, double amount) const noexcept;

        Settings set;
        double att = 0, rel = 0, env = 0, tracker = -60.0, hold = 0, holdSamples = 0, decayPerSample = 0;
        double gr = 0;
    };

    //==========================================================================
    // 4. De-esser: compares the sibilance band (5-10 kHz) with the whole signal,
    //    and turns the top end (above ~4.5 kHz) down while "s" sounds stick out.
    class DeEsser
    {
    public:
        void prepare (double sampleRate);
        void reset();

        // One stereo frame; amount 0..1.
        void process (float* frame, int numChannels, float amount) noexcept;
        float grDb() const noexcept { return (float) gr; }

    private:
        Biquad scHp, scLp, split;
        double att = 0, rel = 0, sibMs = 0, fullMs = 0, gr = 0, grRel = 0;
    };
}
