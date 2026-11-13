#include "MatchEq.h"
#include <cmath>
#include <complex>

using namespace spectrum;

namespace matcheq
{
    namespace
    {
        // RBJ peaking EQ, normalised so a0 == 1
        void peaking (double fs, double f0, double q, double gainDb,
                      double& b0, double& b1, double& b2, double& a1, double& a2) noexcept
        {
            const auto A  = std::pow (10.0, gainDb / 40.0);
            const auto w0 = juce::MathConstants<double>::twoPi * juce::jmin (f0, fs * 0.45) / fs;
            const auto alpha = std::sin (w0) / (2.0 * q);
            const auto cw = std::cos (w0);
            const auto a0 = 1.0 + alpha / A;
            b0 = (1.0 + alpha * A) / a0;
            b1 = (-2.0 * cw) / a0;
            b2 = (1.0 - alpha * A) / a0;
            a1 = (-2.0 * cw) / a0;
            a2 = (1.0 - alpha / A) / a0;
        }

        double magnitudeDb (double fs, double f0, double gainDb, double f) noexcept
        {
            double b0, b1, b2, a1, a2;
            peaking (fs, f0, bandQ, gainDb, b0, b1, b2, a1, a2);
            const auto w = juce::MathConstants<double>::twoPi * f / fs;
            const std::complex<double> z1 = std::polar (1.0, -w), z2 = std::polar (1.0, -2.0 * w);
            const auto h = (b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2);
            return 20.0 * std::log10 (std::abs (h));
        }

        constexpr double designRate = 48000.0;   // fitting is done at a nominal rate

        // Weight of each analysis band in the fit: the useful range only.
        float weight (int b) noexcept
        {
            const auto f = spectrum::bandHz (b);
            return (f >= 25.0f && f <= 16000.0f) ? 1.0f : 0.1f;
        }

        // solve (A) x = y for a small dense system (Gaussian elimination, partial pivoting)
        template <size_t N>
        std::array<double, N> solve (std::array<std::array<double, N>, N> A, std::array<double, N> y)
        {
            constexpr int n = (int) N;
            for (int c = 0; c < n; ++c)
            {
                int p = c;
                for (int r = c + 1; r < n; ++r)
                    if (std::abs (A[(size_t) r][(size_t) c]) > std::abs (A[(size_t) p][(size_t) c]))
                        p = r;
                std::swap (A[(size_t) c], A[(size_t) p]);
                std::swap (y[(size_t) c], y[(size_t) p]);
                for (int r = c + 1; r < n; ++r)
                {
                    const auto f = A[(size_t) r][(size_t) c] / A[(size_t) c][(size_t) c];
                    for (int k = c; k < n; ++k)
                        A[(size_t) r][(size_t) k] -= f * A[(size_t) c][(size_t) k];
                    y[(size_t) r] -= f * y[(size_t) c];
                }
            }
            std::array<double, N> x {};
            for (int r = n - 1; r >= 0; --r)
            {
                auto s = y[(size_t) r];
                for (int k = r + 1; k < n; ++k)
                    s -= A[(size_t) r][(size_t) k] * x[(size_t) k];
                x[(size_t) r] = s / A[(size_t) r][(size_t) r];
            }
            return x;
        }
    }

    Curve targetCurve (const Curve& reference, const Curve& mix, const Settings& s)
    {
        const auto ref = normalised (reference);
        const auto m   = normalised (mix);
        Curve diff {};
        for (int b = 0; b < spectrum::numBands; ++b)
            diff[(size_t) b] = ref[(size_t) b] - m[(size_t) b];

        auto t = smoothed (diff, s.smoothingOct);
        for (int b = 0; b < spectrum::numBands; ++b)
        {
            const auto f = spectrum::bandHz (b);
            // fade the correction out at the extremes (sub rumble, air)
            float fade = 1.0f;
            if (f < 30.0f)    fade = juce::jlimit (0.0f, 1.0f, std::log2 (f / 20.0f) / std::log2 (1.5f));
            if (f > 16000.0f) fade = juce::jlimit (0.0f, 1.0f, 1.0f - std::log2 (f / 16000.0f) / std::log2 (1.25f));
            t[(size_t) b] = juce::jlimit (-s.maxDb, s.maxDb, t[(size_t) b]) * fade;
        }
        return t;
    }

    Gains fit (const Curve& target, float maxDb)
    {
        // Peaking-EQ dB responses add exactly in a cascade but aren't linear in
        // gain, so: linear least-squares with 1 dB basis responses, then refine
        // on the true response a few times.
        std::array<std::array<double, numBands>, spectrum::numBands> basis {};
        for (int b = 0; b < spectrum::numBands; ++b)
            for (int i = 0; i < numBands; ++i)
                basis[(size_t) b][(size_t) i] = magnitudeDb (designRate, bandHz[(size_t) i], 1.0, spectrum::bandHz (b));

        std::array<std::array<double, numBands>, numBands> normal {};
        for (int i = 0; i < numBands; ++i)
            for (int j = 0; j < numBands; ++j)
            {
                double s = 0.0;
                for (int b = 0; b < spectrum::numBands; ++b)
                    s += weight (b) * basis[(size_t) b][(size_t) i] * basis[(size_t) b][(size_t) j];
                normal[(size_t) i][(size_t) j] = s + (i == j ? 0.05 : 0.0);   // light regularisation
            }

        Gains g {};
        Curve residual = target;
        for (int iteration = 0; iteration < 4; ++iteration)
        {
            std::array<double, numBands> rhs {};
            for (int i = 0; i < numBands; ++i)
                for (int b = 0; b < spectrum::numBands; ++b)
                    rhs[(size_t) i] += weight (b) * basis[(size_t) b][(size_t) i] * residual[(size_t) b];

            const auto step = solve<(size_t) numBands> (normal, rhs);
            for (int i = 0; i < numBands; ++i)
                g[(size_t) i] = juce::jlimit (-(maxDb + 3.0f), maxDb + 3.0f, g[(size_t) i] + (float) step[(size_t) i]);

            const auto r = response (g, designRate);
            for (int b = 0; b < spectrum::numBands; ++b)
                residual[(size_t) b] = target[(size_t) b] - r[(size_t) b];
        }
        return g;
    }

    Curve response (const Gains& g, double fs)
    {
        Curve r {};
        for (int b = 0; b < spectrum::numBands; ++b)
        {
            double s = 0.0;
            for (int i = 0; i < numBands; ++i)
                if (std::abs (g[(size_t) i]) > 1.0e-4f)
                    s += magnitudeDb (fs, bandHz[(size_t) i], g[(size_t) i], spectrum::bandHz (b));
            r[(size_t) b] = (float) s;
        }
        return r;
    }

    //==============================================================================
    void Processor::prepare (double sampleRate, int)
    {
        rate = sampleRate;
        current.fill (0.0f);
        for (int i = 0; i < numBands; ++i)
            design (i, 0.0f);
        reset();
        flat = true;
    }

    void Processor::reset()
    {
        for (auto& ch : state)
            for (auto& s : ch)
                s = {};
    }

    void Processor::design (int band, float gainDb) noexcept
    {
        double b0, b1, b2, a1, a2;
        peaking (rate, bandHz[(size_t) band], bandQ, gainDb, b0, b1, b2, a1, a2);
        coeffs[(size_t) band] = { (float) b0, (float) b1, (float) b2, (float) a1, (float) a2 };
    }

    void Processor::process (float* const* channels, int numChannels, int numSamples, const Gains& target) noexcept
    {
        // slew each band towards its target (12 dB/s) and redesign only bands that moved
        const auto maxStep = 12.0f * (float) numSamples / (float) rate;
        bool allFlat = true;
        for (int i = 0; i < numBands; ++i)
        {
            auto& c = current[(size_t) i];
            const auto t = target[(size_t) i];
            if (std::abs (t - c) > 1.0e-4f)
            {
                c += juce::jlimit (-maxStep, maxStep, t - c);
                design (i, c);
            }
            allFlat &= std::abs (c) < 1.0e-3f;
        }

        if (allFlat)
        {
            if (! flat)
                reset();
            flat = true;
            return;   // bit-exact pass-through, no CPU
        }
        flat = false;

        for (int ch = 0; ch < juce::jmin (numChannels, 2); ++ch)
        {
            auto* x = channels[ch];
            for (int i = 0; i < numBands; ++i)
            {
                if (std::abs (current[(size_t) i]) < 1.0e-3f)
                    continue;
                const auto& k = coeffs[(size_t) i];
                auto& s = state[(size_t) ch][(size_t) i];
                auto z1 = s.z1, z2 = s.z2;
                for (int n = 0; n < numSamples; ++n)
                {
                    const auto in  = x[n];
                    const auto out = k.b0 * in + z1;
                    z1 = k.b1 * in - k.a1 * out + z2;
                    z2 = k.b2 * in - k.a2 * out;
                    x[n] = out;
                }
                s.z1 = z1;
                s.z2 = z2;
            }
        }
    }
}
