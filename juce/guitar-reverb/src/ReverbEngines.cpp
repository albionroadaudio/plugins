#include "ReverbEngines.h"
#include <algorithm>
#include <cmath>

namespace rvb
{
namespace
{
    // Parabolic sine on phase [0, 1): ~0.1% error, no transcendental calls.
    inline float fastSin (float phase) noexcept
    {
        const auto x = 2.0f * phase - 1.0f;
        auto y = 4.0f * (x - x * std::abs (x));
        return 0.225f * (y * std::abs (y) - y) + y;
    }

    inline float wrap (float p) noexcept { return p >= 1.0f ? p - 1.0f : p; }

    int nextPow2 (int v)
    {
        int p = 1;
        while (p < v)
            p <<= 1;
        return p;
    }

    // gain per pass for a loop of `seconds` so the loop decays 60 dB in rt60
    double loopGain (double seconds, double rt60)
    {
        return std::pow (10.0, -3.0 * seconds / std::max (0.05, rt60));
    }
}

//==============================================================================
void DelayLine::prepare (int maxDelaySamples)
{
    const auto size = nextPow2 (std::max (4, maxDelaySamples + 2));
    buf.assign ((size_t) size, 0.0f);
    mask = size - 1;
    pos = 0;
}

void DelayLine::clear()
{
    std::fill (buf.begin(), buf.end(), 0.0f);
    pos = 0;
}

float DelayLine::readFrac (float d) const noexcept
{
    const auto whole = (int) d;
    const auto frac  = d - (float) whole;
    const auto a = buf[(size_t) ((pos - whole) & mask)];
    const auto b = buf[(size_t) ((pos - whole - 1) & mask)];
    return a + frac * (b - a);
}

void OnePoleLP::setCutoff (double hz, double sr) noexcept
{
    a = (float) std::exp (-2.0 * 3.141592653589793 * std::clamp (hz, 10.0, sr * 0.45) / sr);
}

//==============================================================================
// FDN room
//==============================================================================
void FdnRoom::prepare (double sampleRate)
{
    sr = sampleRate;
    const auto msToSm = sr * 0.001;
    for (int i = 0; i < N; ++i)
    {
        lenSamples[(size_t) i] = (float) (shape.lineMs[(size_t) i] * msToSm);
        lines[(size_t) i].prepare ((int) std::ceil (lenSamples[(size_t) i] + shape.modMs * msToSm) + 4);
        modPhase[(size_t) i] = (float) i / (float) N;
    }
    for (int i = 0; i < 4; ++i)
    {
        auto& d = diffusers[(size_t) i];
        d.delay = std::max (1, (int) std::lround (shape.diffuseMs[(size_t) i] * msToSm));
        d.g = shape.diffusion;
        d.line.prepare (d.delay + 1);
    }
    float maxEr = 0.0f;
    for (int i = 0; i < 8; ++i)
    {
        erTap[(size_t) i] = std::max (1, (int) std::lround (shape.erMs[(size_t) i] * msToSm));
        maxEr = std::max (maxEr, (float) erTap[(size_t) i]);
    }
    erLine.prepare ((int) maxEr + 2);
    modInc = (float) (0.37 / sr);
    reset();
}

void FdnRoom::reset()
{
    for (auto& l : lines) l.clear();
    for (auto& d : diffusers) d.line.clear();
    erLine.clear();
    dampZ.fill (0.0f);
}

void FdnRoom::setVoice (const VoiceDef& v, float rt60)
{
    rt60 *= 1.17f;   // calibrated: HF damping shortens the broadband T20 (tools/IRTest)
    for (int i = 0; i < N; ++i)
    {
        const auto secs = (double) lenSamples[(size_t) i] / sr;
        const auto gDc  = loopGain (secs, rt60);
        const auto gHf  = loopGain (secs, rt60 * std::max (0.1f, v.hfRatio));
        const auto p    = (gDc - gHf) / (gDc + gHf);
        gain[(size_t) i]  = (float) (gDc * (1.0 - p));
        dampP[(size_t) i] = (float) p;
    }
    modDepth = (float) (shape.modMs * 0.001 * sr * v.mod);
}

void FdnRoom::process (const float* in, float* outL, float* outR, int n) noexcept
{
    static constexpr float inSign[N]  = { 1, -1, 1, -1, 1, -1, 1, -1 };
    static constexpr float outSL[N]   = { 1, 1, -1, -1, 1, 1, -1, -1 };
    static constexpr float outSR[N]   = { 1, -1, 1, -1, -1, 1, -1, 1 };
    constexpr float householder = 2.0f / (float) N;
    const auto lateScale = shape.lateLevel * 0.35f;

    for (int s = 0; s < n; ++s)
    {
        const auto x = in[s];

        // early reflections from a tapped line
        const auto erIn = erLine.read (erTap[0]) * shape.erGain[0] + erLine.read (erTap[2]) * shape.erGain[2]
                        + erLine.read (erTap[4]) * shape.erGain[4] + erLine.read (erTap[6]) * shape.erGain[6];
        const auto erInR = erLine.read (erTap[1]) * shape.erGain[1] + erLine.read (erTap[3]) * shape.erGain[3]
                         + erLine.read (erTap[5]) * shape.erGain[5] + erLine.read (erTap[7]) * shape.erGain[7];
        erLine.write (x);

        auto d = x;
        for (auto& ap : diffusers)
            d = ap.process (d);
        d *= 0.5f;

        float o[N], f[N];
        float sum = 0.0f;
        for (int i = 0; i < N; ++i)
        {
            auto& ph = modPhase[(size_t) i];
            ph = wrap (ph + modInc * (1.0f + 0.13f * (float) i));
            const auto dl = lenSamples[(size_t) i] - modDepth * 0.5f * (1.0f + fastSin (ph));
            o[i] = lines[(size_t) i].readFrac (std::max (1.0f, dl));
            f[i] = gain[(size_t) i] * o[i] + dampP[(size_t) i] * dampZ[(size_t) i];
            dampZ[(size_t) i] = f[i];
            sum += f[i];
        }
        sum *= householder;

        float l = 0.0f, r = 0.0f;
        for (int i = 0; i < N; ++i)
        {
            lines[(size_t) i].write (f[i] - sum + d * inSign[i]);
            l += o[i] * outSL[i];
            r += o[i] * outSR[i];
        }

        outL[s] = shape.erLevel * erIn  + lateScale * l;
        outR[s] = shape.erLevel * erInR + lateScale * r;
    }
}

//==============================================================================
// Dattorro plate
//==============================================================================
namespace
{
    constexpr double plateRate = 29761.0;
    constexpr int inDiffLen[4]  = { 142, 107, 379, 277 };
    constexpr float inDiffG[4]  = { 0.75f, 0.75f, 0.625f, 0.625f };
    constexpr int apModBase[2]  = { 672, 908 };
    constexpr int del1Base[2]   = { 4453, 4217 };
    constexpr int ap2Base[2]    = { 1800, 2656 };
    constexpr int del2Base[2]   = { 3720, 3163 };
    // output taps, Dattorro fig. 1 / table 2:
    // L: +R.del1[266] +R.del1[2974] -R.ap2[1913] +R.del2[1996] -L.del1[1990] -L.ap2[187] -L.del2[1066]
    // R: +L.del1[353] +L.del1[3627] -L.ap2[1228] +L.del2[2673] -R.del1[2111] -R.ap2[335] -R.del2[121]
    constexpr int tapLBase[7] = { 266, 2974, 1913, 1996, 1990, 187, 1066 };
    constexpr int tapRBase[7] = { 353, 3627, 1228, 2673, 2111, 335, 121 };
    constexpr float plateLoopSeconds = (float) ((672 + 4453 + 1800 + 3720 + 908 + 4217 + 2656 + 3163) * 0.5 / plateRate);
}

void Plate::prepare (double sampleRate)
{
    sr = sampleRate;
    scale = sr / plateRate;
    auto sc = [this] (int v) { return std::max (1, (int) std::lround (v * scale)); };

    for (int i = 0; i < 4; ++i)
    {
        inputDiff[(size_t) i].delay = sc (inDiffLen[i]);
        inputDiff[(size_t) i].g = inDiffG[i];
        inputDiff[(size_t) i].line.prepare (inputDiff[(size_t) i].delay + 1);
    }
    const auto maxExc = (float) (16.0 * scale) + 2.0f;
    for (int h = 0; h < 2; ++h)
    {
        apModLen[h] = (float) (apModBase[h] * scale);
        apMod[h].prepare ((int) std::ceil (apModLen[h] + maxExc) + 2);
        del1Len[h] = sc (del1Base[h]);
        del1[h].prepare (del1Len[h] + 1);
        ap2[h].delay = sc (ap2Base[h]);
        ap2[h].line.prepare (ap2[h].delay + 1);
        del2Len[h] = sc (del2Base[h]);
        del2[h].prepare (del2Len[h] + 1);
    }
    for (int i = 0; i < 7; ++i)
    {
        tapL[(size_t) i] = sc (tapLBase[i]);
        tapR[(size_t) i] = sc (tapRBase[i]);
    }
    lfoInc = (float) (1.0 / sr);
    reset();
}

void Plate::reset()
{
    bandwidth.z = 0.0f;
    for (auto& d : inputDiff) d.line.clear();
    for (int h = 0; h < 2; ++h)
    {
        apMod[h].clear(); del1[h].clear(); del2[h].clear(); ap2[h].line.clear();
        damp[h].z = 0.0f;
        tankFeed[h] = 0.0f;
    }
}

void Plate::setVoice (const VoiceDef& v, float rt60)
{
    bandwidth.setCutoff (v.bandwidthHz, sr);
    for (auto& d : damp)
        d.setCutoff (v.dampHz, sr);
    // decay is applied twice per tank half
    decay = (float) std::min (0.97, std::sqrt (loopGain (plateLoopSeconds, rt60)));
    for (auto& a : ap2)
        a.g = std::clamp (decay + 0.15f, 0.25f, 0.5f);
    excursion = (float) (16.0 * scale * v.mod);
    lfoInc = (float) ((0.5 + 0.6 * v.mod) / sr);
}

void Plate::process (const float* in, float* outL, float* outR, int n) noexcept
{
    constexpr float outGain = 0.6f;

    for (int s = 0; s < n; ++s)
    {
        auto x = bandwidth.process (in[s]);
        for (auto& ap : inputDiff)
            x = ap.process (x);

        lfoPhase = wrap (lfoPhase + lfoInc);
        const float lfo[2] = { fastSin (lfoPhase), fastSin (wrap (lfoPhase + 0.25f)) };

        float feed[2];
        for (int h = 0; h < 2; ++h)
        {
            const auto u = x + tankFeed[1 - h];
            // modulated allpass, "decay diffusion 1"
            const auto dl = apMod[h].readFrac (apModLen[h] + excursion * lfo[h]);
            const auto v  = u + decayDiff1 * dl;
            apMod[h].write (v);
            const auto a = dl - decayDiff1 * v;

            auto t = del1[h].read (del1Len[h]);
            del1[h].write (a);
            t = damp[h].process (t) * decay;
            t = ap2[h].process (t);

            const auto w = del2[h].read (del2Len[h]);
            del2[h].write (t);
            feed[h] = w * decay;
        }
        tankFeed[0] = feed[0];
        tankFeed[1] = feed[1];

        const auto l = del1[1].read (tapL[0]) + del1[1].read (tapL[1]) - ap2[1].line.read (tapL[2]) + del2[1].read (tapL[3])
                     - del1[0].read (tapL[4]) - ap2[0].line.read (tapL[5]) - del2[0].read (tapL[6]);
        const auto r = del1[0].read (tapR[0]) + del1[0].read (tapR[1]) - ap2[0].line.read (tapR[2]) + del2[0].read (tapR[3])
                     - del1[1].read (tapR[4]) - ap2[1].line.read (tapR[5]) - del2[1].read (tapR[6]);
        outL[s] = l * outGain;
        outR[s] = r * outGain;
    }
}

//==============================================================================
// Spring tank
//==============================================================================
namespace
{
    constexpr float springTransitMs[Spring::numSprings] = { 27.3f, 31.9f, 36.1f };
}

void Spring::prepare (double sampleRate)
{
    sr = sampleRate;
    for (int i = 0; i < numSprings; ++i)
    {
        auto& sp = springs[(size_t) i];
        sp.transitSamples = (float) (springTransitMs[i] * 0.001 * sr);
        sp.transit.prepare ((int) std::ceil (sp.transitSamples + 0.001 * sr) + 4);
    }
    dcCoeff = (float) std::exp (-2.0 * 3.141592653589793 * 60.0 / sr);
    wobbleCoeff = (float) std::exp (-2.0 * 3.141592653589793 * 3.0 / sr);
    reset();
}

void Spring::reset()
{
    for (auto& sp : springs)
    {
        sp.apX.fill (0.0f);
        sp.apY.fill (0.0f);
        sp.transit.clear();
        sp.damp.z = 0.0f;
        sp.dcX = sp.dcY = sp.fb = sp.wobble = 0.0f;
    }
}

void Spring::setVoice (const VoiceDef& v, float rt60)
{
    rt60 *= 1.32f;   // calibrated: in-loop damping and DC blocker shorten the broadband T20 (tools/IRTest)
    // first-order allpass group delay at DC is (1-a)/(1+a) samples per stage
    const auto apDelay = (1.0f - apCoeff) / (1.0f + apCoeff) * (float) stages;
    for (auto& sp : springs)
    {
        sp.damp.setCutoff (v.dampHz, sr);
        sp.g = (float) std::min (0.95, loopGain ((sp.transitSamples + apDelay) / sr, rt60));
    }
    // low-passed uniform noise has RMS sqrt((1-c)/(1+c)/3); normalise it to ~1
    const auto c = (double) wobbleCoeff;
    const auto norm = 1.0 / std::sqrt ((1.0 - c) / (1.0 + c) / 3.0);
    wobbleDepth = (float) (0.0004 * sr * v.mod * norm);   // ~0.4 ms RMS of random transit wobble at full
}

void Spring::process (const float* in, float* outL, float* outR, int n) noexcept
{
    const auto a = apCoeff;

    for (int s = 0; s < n; ++s)
    {
        float y[numSprings];
        for (int k = 0; k < numSprings; ++k)
        {
            auto& sp = springs[(size_t) k];
            auto v = in[s] * 0.5f + sp.fb;

            for (int i = 0; i < stages; ++i)
            {
                const auto o = a * v + sp.apX[(size_t) i] - a * sp.apY[(size_t) i];
                sp.apX[(size_t) i] = v;
                sp.apY[(size_t) i] = o;
                v = o;
            }

            rng = rng * 1664525u + 1013904223u;
            const auto noise = (float) (rng >> 8) * (2.0f / 16777216.0f) - 1.0f;
            sp.wobble = noise + wobbleCoeff * (sp.wobble - noise);

            const auto d = std::clamp (sp.transitSamples + wobbleDepth * sp.wobble, 1.0f, sp.transitSamples + (float) (0.0009 * sr));
            const auto out = sp.transit.readFrac (d);
            sp.transit.write (v);

            const auto lp = sp.damp.process (out);
            sp.dcY = lp - sp.dcX + dcCoeff * sp.dcY;
            sp.dcX = lp;
            sp.fb = sp.g * sp.dcY;
            y[k] = out;
        }
        outL[s] = 1.2f * (y[0] + 0.5f * y[2]);
        outR[s] = 1.2f * (y[1] - 0.5f * y[2]);
    }
}
}
