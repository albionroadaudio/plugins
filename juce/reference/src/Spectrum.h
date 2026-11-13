#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <vector>

// Everything is expressed on one fixed log-frequency grid: 120 bands at
// 1/12 octave from 20 Hz (~19.4 kHz at the top). Spectra are in dB of
// average power per FFT bin (a PSD shape: white noise is flat, pink falls 3 dB/oct).
namespace spectrum
{
    constexpr int   numBands = 120;
    constexpr float firstHz  = 20.0f;
    constexpr int   fftOrder = 13;              // 8192 points
    constexpr int   fftSize  = 1 << fftOrder;

    using Curve = std::array<float, numBands>;  // dB per band

    inline float bandHz (int band) noexcept     { return firstHz * std::exp2 ((float) band / 12.0f); }

    // Maps FFT bin powers to band powers for a given sample rate.
    class BandMapper
    {
    public:
        void prepare (double sampleRate);
        void bandsFromBins (const float* binPower, float* bandPower) const noexcept;   // linear power
        bool isPrepared() const noexcept { return rate > 0.0; }

    private:
        struct Band { int first = 0, last = -1; float interpBin = 0.0f; bool interpolate = true; };
        std::array<Band, numBands> bands {};
        double rate = 0.0;
    };

    // Mean dB over 100 Hz - 8 kHz: the "loudness" we remove before comparing tone.
    float midMean (const Curve&) noexcept;
    Curve normalised (const Curve&) noexcept;               // midMean == 0
    Curve smoothed (const Curve&, float octaves) noexcept;  // gaussian in log-frequency

    juce::String toString (const Curve&);
    bool fromString (const juce::String&, Curve&);
}
