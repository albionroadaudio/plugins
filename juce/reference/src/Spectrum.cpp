#include "Spectrum.h"
#include <cmath>

namespace spectrum
{
    void BandMapper::prepare (double sampleRate)
    {
        rate = sampleRate;
        const auto binHz   = (float) (sampleRate / fftSize);
        const auto maxBin  = fftSize / 2;

        for (int b = 0; b < numBands; ++b)
        {
            const auto fc = bandHz (b);
            const auto lo = fc * std::exp2 (-1.0f / 24.0f) / binHz;
            const auto hi = fc * std::exp2 ( 1.0f / 24.0f) / binHz;
            auto& band = bands[(size_t) b];
            band.first = (int) std::ceil (lo);
            band.last  = juce::jmin (maxBin, (int) std::floor (hi));
            band.interpBin = juce::jlimit (0.0f, (float) maxBin, fc / binHz);
            // narrow low bands (fewer than 2 whole bins) are read by interpolation
            band.interpolate = band.last - band.first < 1;
        }
    }

    void BandMapper::bandsFromBins (const float* p, float* out) const noexcept
    {
        constexpr int maxBin = fftSize / 2;
        for (int b = 0; b < numBands; ++b)
        {
            const auto& band = bands[(size_t) b];
            if (band.interpolate)
            {
                const auto i = juce::jmin (maxBin - 1, (int) band.interpBin);
                const auto f = band.interpBin - (float) i;
                out[b] = p[i] + f * (p[i + 1] - p[i]);
            }
            else
            {
                float sum = 0.0f;
                for (int k = band.first; k <= band.last; ++k)
                    sum += p[k];
                out[b] = sum / (float) (band.last - band.first + 1);
            }
        }
    }

    float midMean (const Curve& c) noexcept
    {
        float sum = 0.0f;
        int n = 0;
        for (int b = 0; b < numBands; ++b)
            if (bandHz (b) >= 100.0f && bandHz (b) <= 8000.0f)
            {
                sum += c[(size_t) b];
                ++n;
            }
        return n > 0 ? sum / (float) n : 0.0f;
    }

    Curve normalised (const Curve& c) noexcept
    {
        auto out = c;
        const auto m = midMean (c);
        for (auto& v : out)
            v -= m;
        return out;
    }

    Curve smoothed (const Curve& c, float octaves) noexcept
    {
        if (octaves <= 0.01f)
            return c;
        // gaussian whose ±1 sigma spans `octaves` (12 bands per octave)
        const auto sigma = juce::jmax (0.5f, octaves * 12.0f * 0.5f);
        const auto reach = (int) std::ceil (3.0f * sigma);
        Curve out {};
        for (int b = 0; b < numBands; ++b)
        {
            float sum = 0.0f, wsum = 0.0f;
            for (int k = juce::jmax (0, b - reach); k <= juce::jmin (numBands - 1, b + reach); ++k)
            {
                const auto d = (float) (k - b) / sigma;
                const auto w = std::exp (-0.5f * d * d);
                sum += w * c[(size_t) k];
                wsum += w;
            }
            out[(size_t) b] = sum / wsum;
        }
        return out;
    }

    juce::String toString (const Curve& c)
    {
        juce::StringArray parts;
        for (auto v : c)
            parts.add (juce::String (v, 2));
        return parts.joinIntoString (" ");
    }

    bool fromString (const juce::String& s, Curve& c)
    {
        juce::StringArray parts;
        parts.addTokens (s, " ", {});
        parts.removeEmptyStrings();
        if (parts.size() != numBands)
            return false;
        for (int b = 0; b < numBands; ++b)
            c[(size_t) b] = parts[b].getFloatValue();
        return true;
    }
}
