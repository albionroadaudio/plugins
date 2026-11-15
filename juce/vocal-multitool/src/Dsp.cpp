#include "Dsp.h"
#include <algorithm>
#include <complex>

namespace vmt
{
    //==========================================================================
    // Biquad (Robert Bristow-Johnson's cookbook)
    namespace
    {
        struct Trig { double c, s; };
        Trig trig (double f, double sr) noexcept
        {
            const auto w = 2.0 * juce::MathConstants<double>::pi * juce::jlimit (5.0, sr * 0.49, f) / sr;
            return { std::cos (w), std::sin (w) };
        }
    }

    void Biquad::setLowpass (double f, double q, double sr) noexcept
    {
        const auto [c, s] = trig (f, sr);
        const auto alpha = s / (2.0 * q);
        b0 = (1.0 - c) * 0.5; b1 = 1.0 - c; b2 = b0;
        a1 = -2.0 * c; a2 = 1.0 - alpha;
        normalise (1.0 + alpha);
    }

    void Biquad::setHighpass (double f, double q, double sr) noexcept
    {
        const auto [c, s] = trig (f, sr);
        const auto alpha = s / (2.0 * q);
        b0 = (1.0 + c) * 0.5; b1 = -(1.0 + c); b2 = b0;
        a1 = -2.0 * c; a2 = 1.0 - alpha;
        normalise (1.0 + alpha);
    }

    void Biquad::setBandpass (double f, double q, double sr) noexcept
    {
        const auto [c, s] = trig (f, sr);
        const auto alpha = s / (2.0 * q);
        b0 = alpha; b1 = 0.0; b2 = -alpha;
        a1 = -2.0 * c; a2 = 1.0 - alpha;
        normalise (1.0 + alpha);
    }

    void Biquad::setPeak (double f, double q, double gainDb, double sr) noexcept
    {
        const auto [c, s] = trig (f, sr);
        const auto A = std::pow (10.0, gainDb / 40.0);
        const auto alpha = s / (2.0 * q);
        b0 = 1.0 + alpha * A; b1 = -2.0 * c; b2 = 1.0 - alpha * A;
        a1 = -2.0 * c; a2 = 1.0 - alpha / A;
        normalise (1.0 + alpha / A);
    }

    void Biquad::setHighShelf (double f, double q, double gainDb, double sr) noexcept
    {
        const auto [c, s] = trig (f, sr);
        const auto A = std::pow (10.0, gainDb / 40.0);
        const auto alpha = s / (2.0 * q);
        const auto k = 2.0 * std::sqrt (A) * alpha;
        b0 =  A * ((A + 1.0) + (A - 1.0) * c + k);
        b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * c);
        b2 =  A * ((A + 1.0) + (A - 1.0) * c - k);
        a1 =  2.0 * ((A - 1.0) - (A + 1.0) * c);
        a2 =  (A + 1.0) - (A - 1.0) * c - k;
        normalise ((A + 1.0) - (A - 1.0) * c + k);
    }

    double Biquad::magnitudeDb (double f, double sr) const noexcept
    {
        const auto w = 2.0 * juce::MathConstants<double>::pi * f / sr;
        const std::complex<double> e1 = std::polar (1.0, -w), e2 = std::polar (1.0, -2.0 * w);
        const auto h = (b0 + b1 * e1 + b2 * e2) / (1.0 + a1 * e1 + a2 * e2);
        return gainToDb (std::abs (h));
    }

    //==========================================================================
    // Denoiser
    void Denoiser::prepare (double sampleRate, int numChannels)
    {
        juce::ignoreUnused (numChannels);
        fftOrder = sampleRate <= 50000.0 ? 11 : (sampleRate <= 100000.0 ? 12 : 13);
        fftSize  = 1 << fftOrder;
        hop      = fftSize / 4;
        numBins  = fftSize / 2 + 1;
        fft = std::make_unique<juce::dsp::FFT> (fftOrder);

        // sqrt-Hann for analysis and synthesis: their product (Hann) overlap-adds to 2 at 75 % overlap
        window.resize ((size_t) fftSize);
        double sumSq = 0.0;
        for (int n = 0; n < fftSize; ++n)
        {
            const auto hann = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * n / fftSize);
            window[(size_t) n] = (float) std::sqrt (hann);
            sumSq += hann;
        }
        powerNorm = sumSq;   // white noise of variance s^2 gives E|X|^2 = s^2 * sum(w^2)
        olaScale  = 0.5;

        const auto frameRate = sampleRate / hop;
        powerAttack  = std::exp (-1.0 / (0.008 * frameRate));   // level estimate: a short (~8 ms) average
        powerRelease = powerAttack;                              // (the gate release below sets the timing)
        gainRelease  = std::exp (-1.0 / (0.080 * frameRate));   // the gate closes over ~80 ms

        for (int ch = 0; ch < maxChannels; ++ch)
        {
            inRing[(size_t) ch].assign ((size_t) fftSize, 0.0f);
            outRing[(size_t) ch].assign ((size_t) fftSize, 0.0f);
            spectrum[(size_t) ch].assign ((size_t) fftSize * 2, 0.0f);
        }
        power.assign ((size_t) numBins, 0.0);
        smoothPower.assign ((size_t) numBins, 0.0);
        gainDb.assign ((size_t) numBins, 0.0);
        gainTmp.assign ((size_t) numBins, 0.0);
        reset();
    }

    void Denoiser::reset()
    {
        for (int ch = 0; ch < maxChannels; ++ch)
        {
            std::fill (inRing[(size_t) ch].begin(), inRing[(size_t) ch].end(), 0.0f);
            std::fill (outRing[(size_t) ch].begin(), outRing[(size_t) ch].end(), 0.0f);
        }
        std::fill (smoothPower.begin(), smoothPower.end(), 0.0);
        std::fill (gainDb.begin(), gainDb.end(), 0.0);
        pos = 0;
        hopCounter = 0;
        meterDb = 0.0f;
    }

    void Denoiser::process (juce::AudioBuffer<float>& buffer, int numChannels, float amount) noexcept
    {
        amountNow = amount;
        const auto n = buffer.getNumSamples();
        const auto mask = fftSize - 1;

        for (int i = 0; i < n; ++i)
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* d = buffer.getWritePointer (ch);
                inRing[(size_t) ch][(size_t) pos] = d[i];
                d[i] = outRing[(size_t) ch][(size_t) pos];
                outRing[(size_t) ch][(size_t) pos] = 0.0f;
            }
            pos = (pos + 1) & mask;

            if (++hopCounter == hop)
            {
                hopCounter = 0;
                processFrame (numChannels);
            }
        }
    }

    void Denoiser::processFrame (int numChannels) noexcept
    {
        const auto mask = fftSize - 1;

        // --- analysis --------------------------------------------------------------
        std::fill (power.begin(), power.end(), 0.0);
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& spec = spectrum[(size_t) ch];
            const auto& ring = inRing[(size_t) ch];
            for (int n = 0; n < fftSize; ++n)
                spec[(size_t) n] = ring[(size_t) ((pos + n) & mask)] * window[(size_t) n];
            std::fill (spec.begin() + fftSize, spec.end(), 0.0f);
            fft->performRealOnlyForwardTransform (spec.data(), true);

            for (int k = 0; k < numBins; ++k)
            {
                const double re = spec[(size_t) (2 * k)], im = spec[(size_t) (2 * k + 1)];
                power[(size_t) k] = std::max (power[(size_t) k], re * re + im * im);   // stereo-linked
            }
        }

        // --- per-bin gain --------------------------------------------------------------
        const double depth = maxDepthDb * juce::jlimit (0.0, 1.0, (double) amountNow);
        const double openDb = thresholdDb + kneeDb * 0.5;
        double reductionSum = 0.0;

        for (int k = 0; k < numBins; ++k)
        {
            // 3-bin frequency smoothing, then fast-rise / slow-fall time smoothing
            const auto lo = power[(size_t) std::max (0, k - 1)];
            const auto hi = power[(size_t) std::min (numBins - 1, k + 1)];
            const auto p  = 0.25 * lo + 0.5 * power[(size_t) k] + 0.25 * hi;
            auto& sp = smoothPower[(size_t) k];
            const auto c = p > sp ? powerAttack : powerRelease;
            sp = c * sp + (1.0 - c) * p;

            const auto levelDb = 10.0 * std::log10 (sp / powerNorm + 1.0e-30);
            const auto closed  = juce::jlimit (0.0, 1.0, (openDb - levelDb) / kneeDb);
            const auto target  = -depth * closed;

            auto& g = gainDb[(size_t) k];
            g = target > g ? target : gainRelease * g + (1.0 - gainRelease) * target;   // opens at once, closes gently
        }

        // smooth the gain across frequency too (fewer "musical noise" artefacts)
        for (int k = 0; k < numBins; ++k)
        {
            const auto lo = gainDb[(size_t) std::max (0, k - 1)];
            const auto hi = gainDb[(size_t) std::min (numBins - 1, k + 1)];
            gainTmp[(size_t) k] = 0.25 * lo + 0.5 * gainDb[(size_t) k] + 0.25 * hi;
            reductionSum += gainTmp[(size_t) k];
        }
        meterDb = (float) (-reductionSum / numBins);

        // --- synthesis -------------------------------------------------------------------
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& spec = spectrum[(size_t) ch];
            for (int k = 0; k < numBins; ++k)
            {
                const auto g = (float) dbToGain (gainTmp[(size_t) k]);
                spec[(size_t) (2 * k)]     *= g;
                spec[(size_t) (2 * k + 1)] *= g;
            }
            fft->performRealOnlyInverseTransform (spec.data());

            auto& out = outRing[(size_t) ch];
            for (int n = 0; n < fftSize; ++n)
                out[(size_t) ((pos + n) & mask)] += spec[(size_t) n] * window[(size_t) n] * (float) olaScale;
        }
    }

    //==========================================================================
    // AutoComp
    void AutoComp::prepare (double sampleRate)
    {
        att = timeCoeff (set.attackMs * 0.001, sampleRate);
        rel = timeCoeff (set.releaseMs * 0.001, sampleRate);
        holdSamples    = 2.0 * sampleRate;     // remember the loudest moment for 2 s ...
        decayPerSample = 0.5 / sampleRate;     // ... then let go at 0.5 dB per second
        reset();
    }

    void AutoComp::reset()
    {
        env = 0.0;
        tracker = set.trackerFloorDb;
        hold = 0.0;
        gr = 0.0;
    }

    double AutoComp::staticGr (double levelDb, double amount) const noexcept
    {
        const auto target = set.maxTargetDb * amount;
        if (target < 0.01)
            return 0.0;

        double threshold, slope;
        if (set.style == Style::Clamp)
        {
            slope = 1.0 - 1.0 / set.ratio;
            threshold = tracker - target / slope;
        }
        else
        {
            threshold = set.thresholdDb;
            const auto span = std::max (tracker - threshold, target + 1.0);
            slope = target / span;                   // = 1 - 1/ratio
        }

        const auto over = levelDb - threshold;
        const auto knee = set.kneeDb;
        if (2.0 * over <= -knee)
            return 0.0;
        if (2.0 * over < knee)
            return slope * (over + knee * 0.5) * (over + knee * 0.5) / (2.0 * knee);
        return slope * over;
    }

    float AutoComp::process (float in, float amount) noexcept
    {
        const double x = set.rmsDetector ? (double) in * in : std::abs ((double) in);
        env += (x > env ? att : rel) * (x - env);
        const auto levelDb = set.rmsDetector ? 10.0 * std::log10 (env + 1.0e-30) : gainToDb (env);

        if (levelDb > tracker)
        {
            tracker = levelDb;
            hold = holdSamples;
        }
        else if (hold > 0.0)
        {
            hold -= 1.0;
        }
        else
        {
            tracker = std::max (set.trackerFloorDb, tracker - decayPerSample);
        }

        const auto a = juce::jlimit (0.0, 1.0, (double) amount);
        gr = staticGr (levelDb, a);
        return (float) dbToGain (set.maxTargetDb * a - gr);
    }

    //==========================================================================
    // DeEsser
    void DeEsser::prepare (double sampleRate)
    {
        scHp.setHighpass (5000.0, 0.7071, sampleRate);
        scLp.setLowpass (std::min (10000.0, sampleRate * 0.45), 0.7071, sampleRate);
        split.setLowpass (4500.0, 0.7071, sampleRate);
        att   = timeCoeff (0.0005, sampleRate);
        rel   = timeCoeff (0.030,  sampleRate);
        grRel = timeCoeff (0.050,  sampleRate);
        reset();
    }

    void DeEsser::reset()
    {
        scHp.reset(); scLp.reset(); split.reset();
        sibMs = fullMs = 0.0;
        gr = 0.0;
    }

    void DeEsser::process (float* frame, int numChannels, float amount) noexcept
    {
        float mono = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            mono += frame[ch];
        mono /= (float) numChannels;

        const double s = scLp.process (scHp.process (mono, 0), 0);
        const double m = mono;
        sibMs  += (s * s > sibMs  ? att : rel) * (s * s - sibMs);
        fullMs += (m * m > fullMs ? att : rel) * (m * m - fullMs);

        double target = 0.0;
        const auto a = juce::jlimit (0.0, 1.0, (double) amount);
        if (a > 0.0)
        {
            const auto sibDb  = 10.0 * std::log10 (sibMs + 1.0e-30);
            const auto relDb  = sibDb - 10.0 * std::log10 (fullMs + 1.0e-30);
            const auto thresh = -4.0 - 10.0 * a;            // 50 %: acts once the "s" band is within 9 dB of the whole
            const auto depth  = 3.0 + 9.0 * a;              // up to 12 dB at 100 %
            if (sibDb > -60.0 && relDb > thresh)
                target = std::min (depth, (relDb - thresh) * 0.75);
        }
        gr = target > gr ? target : gr + grRel * (target - gr);

        const auto g = (float) dbToGain (-gr);
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto x = frame[ch];
            const auto low = split.process (x, ch);
            frame[ch] = low + g * (x - low);
        }
    }
}
