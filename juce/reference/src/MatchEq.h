#pragma once

#include "Spectrum.h"
#include <array>
#include <atomic>

// Gentle match EQ: 15 broad peaking bands (2/3-octave spacing, 25 Hz - 16 kHz)
// fitted by least squares to a smoothed, range-limited target curve.
// Minimum-phase biquads: zero latency, ~30 biquads per stereo sample.
namespace matcheq
{
    constexpr int numBands = 15;
    constexpr std::array<float, numBands> bandHz { 25.0f, 40.0f, 63.0f, 100.0f, 160.0f, 250.0f, 400.0f, 630.0f,
                                                  1000.0f, 1600.0f, 2500.0f, 4000.0f, 6300.0f, 10000.0f, 16000.0f };
    constexpr float bandQ = 1.6f;

    using Gains = std::array<float, numBands>;   // dB

    struct Settings
    {
        float maxDb = 3.0f;          // per-frequency limit on the target
        float smoothingOct = 1.0f;   // smoothing of the difference curve
    };

    // Target = smoothed (reference - mix), both loudness-normalised, limited
    // to +-maxDb and faded out below 30 Hz / above 16 kHz.
    spectrum::Curve targetCurve (const spectrum::Curve& reference, const spectrum::Curve& mix, const Settings&);

    // Least-squares band gains that reproduce `target` as closely as possible.
    Gains fit (const spectrum::Curve& target, float maxDb);

    // Response (dB) of the band set at the analysis band centres, for drawing.
    spectrum::Curve response (const Gains&, double sampleRate = 48000.0);

    // --- audio side --------------------------------------------------------------
    class Processor
    {
    public:
        void prepare (double sampleRate, int numChannels);
        void reset();
        // gains: target dB per band (already scaled by amount); slews smoothly
        void process (float* const* channels, int numChannels, int numSamples, const Gains& target) noexcept;
        bool isFlat() const noexcept { return flat; }

    private:
        struct Coeffs { float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
        struct State  { float z1 = 0, z2 = 0; };
        void design (int band, float gainDb) noexcept;

        double rate = 48000.0;
        Gains current {};
        std::array<Coeffs, numBands> coeffs {};
        std::array<std::array<State, numBands>, 2> state {};
        bool flat = true;
    };
}
