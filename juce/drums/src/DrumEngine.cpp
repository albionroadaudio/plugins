#include "DrumEngine.h"
#include <cmath>

namespace
{
    // DrumGizmo's sample-selection weights (settings.h defaults), with the same
    // scaling its selector applies.
    constexpr float fClose   = 4.0f * 0.85f;
    constexpr float fDiverse = 0.5f * 0.16f;
    constexpr float fRandom  = (1.0f / 3.0f) * 0.07f;

    constexpr float groupChokeMs   = 68.0f;    // hihat-style group choke
    constexpr float aftertouchMs   = 450.0f;   // cymbal grab via aftertouch
    constexpr float voiceLimitMs   = 30.0f;
    constexpr float staminaFalloff = 0.5f;     // seconds to recover fully
    constexpr float staminaWeight  = 0.25f;
    constexpr float velocityStddev = 0.45f / 30.0f;

    using Fmt = MappedWav::Format;

    template <Fmt F>
    void addStreamDirect (const MappedWav& w, int channel, std::int64_t start,
                          int n, const float* env, float gain, float* out) noexcept
    {
        const auto stride = w.bytesPerFrame;
        const auto count  = (int) juce::jlimit<std::int64_t> (0, n, w.numFrames - start);
        const auto* p = w.frames + channel * w.bytesPerSample + start * stride;
        if (env == nullptr)
            for (int k = 0; k < count; ++k, p += stride)
                out[k] += wav::decode<F> (p) * gain;
        else
            for (int k = 0; k < count; ++k, p += stride)
                out[k] += wav::decode<F> (p) * gain * env[k];
    }

    // 4-point Hermite read. rel[k]/frac[k] (frame offset from `start`, fraction)
    // are computed once per voice and shared by all of that voice's mics.
    template <Fmt F>
    void addStreamInterp (const MappedWav& w, int channel, std::int64_t start,
                          const std::int32_t* rel, const float* frac,
                          int n, const float* env, float gain, float* out,
                          float* scratch, int scratchSize) noexcept
    {
        const auto* base   = w.frames + channel * w.bytesPerSample;
        const auto  stride = (std::int64_t) w.bytesPerFrame;
        const auto  frames = w.numFrames;

        auto at = [&] (std::int64_t i) noexcept
        {
            return (i >= 0 && i < frames) ? wav::decode<F> (base + i * stride) : 0.0f;
        };
        auto hermite = [] (float xm1, float x0, float x1, float x2, float f) noexcept
        {
            const auto c1 = 0.5f * (x1 - xm1);
            const auto c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
            const auto c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            return ((c3 * f + c2) * f + c1) * f + x0;
        };

        // fast region: every tap inside the file, no bounds checks
        int kBegin = 0;
        while (kBegin < n && start + rel[kBegin] < 1)
            ++kBegin;
        int kEnd = n;
        while (kEnd > kBegin && start + rel[kEnd - 1] + 2 >= frames)
            --kEnd;

        for (int k = 0; k < kBegin; ++k)
        {
            const auto i = start + rel[k];
            out[k] += hermite (at (i - 1), at (i), at (i + 1), at (i + 2), frac[k]) * gain * (env != nullptr ? env[k] : 1.0f);
        }

        if (kEnd > kBegin)
        {
            // Decode each needed file frame exactly once into `scratch`, then
            // interpolate from cache-resident floats (1 decode per frame instead
            // of 4 per output sample).
            const auto first = start + rel[kBegin] - 1;
            const auto count = (int) (rel[kEnd - 1] - rel[kBegin]) + 4;
            if (count <= scratchSize)
            {
                const auto* p = base + first * stride;
                for (int j = 0; j < count; ++j, p += stride)
                    scratch[j] = wav::decode<F> (p);

                const auto relBase = rel[kBegin];
                if (env == nullptr)
                    for (int k = kBegin; k < kEnd; ++k)
                    {
                        const auto* x = scratch + (rel[k] - relBase);
                        out[k] += hermite (x[0], x[1], x[2], x[3], frac[k]) * gain;
                    }
                else
                    for (int k = kBegin; k < kEnd; ++k)
                    {
                        const auto* x = scratch + (rel[k] - relBase);
                        out[k] += hermite (x[0], x[1], x[2], x[3], frac[k]) * gain * env[k];
                    }
            }
            else
            {
                const auto* p0 = base + (start - 1) * stride;
                for (int k = kBegin; k < kEnd; ++k)
                {
                    const auto* q = p0 + rel[k] * stride;
                    out[k] += hermite (wav::decode<F> (q), wav::decode<F> (q + stride),
                                       wav::decode<F> (q + 2 * stride), wav::decode<F> (q + 3 * stride), frac[k])
                              * gain * (env != nullptr ? env[k] : 1.0f);
                }
            }
        }

        for (int k = kEnd; k < n; ++k)
        {
            const auto i = start + rel[k];
            if (i >= frames)
                break;
            out[k] += hermite (at (i - 1), at (i), at (i + 1), at (i + 2), frac[k]) * gain * (env != nullptr ? env[k] : 1.0f);
        }
    }
}

//==============================================================================
void DrumEngine::prepare (double hostSampleRate)
{
    hostRate = hostSampleRate;
    clock = 0;
    for (auto& v : voices)
        v.active = false;
    resetPerformanceState();
}

// The humanizer's stamina and the sample selector's "recently played" memory are
// timestamps on `clock`. When prepare() restarts the clock (hosts do this before a
// bounce), those timestamps must restart too, or every drum looks as if it was hit
// minutes in the future: stamina goes far below zero and hits play near-silent
// until it recovers.
void DrumEngine::resetPerformanceState() noexcept
{
    if (kit == nullptr)
        return;
    for (auto& inst : kit->instruments)
    {
        inst.stamina = 1.0f;
        inst.lastHit = 0;
        std::fill (inst.lastPlayed.begin(), inst.lastPlayed.end(), (std::int64_t) 0);
    }
}

void DrumEngine::setKit (Kit* newKit) noexcept
{
    for (auto& v : voices)
        v.active = false;
    kit = newKit;
}

int DrumEngine::activeVoices() const noexcept
{
    int n = 0;
    for (const auto& v : voices)
        n += v.active ? 1 : 0;
    return n;
}

float DrumEngine::random01() noexcept
{
    // xorshift64*
    rngState ^= rngState >> 12;
    rngState ^= rngState << 25;
    rngState ^= rngState >> 27;
    return (float) ((rngState * 0x2545F4914F6CDD1Dull) >> 40) * (1.0f / 16777216.0f);
}

float DrumEngine::gaussian (float mean, float stddev) noexcept
{
    const auto u1 = juce::jmax (1.0e-7f, random01());
    const auto u2 = random01();
    return mean + stddev * std::sqrt (-2.0f * std::log (u1)) * std::cos (juce::MathConstants<float>::twoPi * u2);
}

//==============================================================================
void DrumEngine::noteOn (int note, float velocity01) noexcept
{
    if (kit == nullptr || ! juce::isPositiveAndBelow (note, 128))
        return;
    for (auto instrument : kit->noteToInstruments[(size_t) note])
        trigger (instrument, velocity01);
}

void DrumEngine::chokeNote (int note) noexcept
{
    if (kit == nullptr || ! juce::isPositiveAndBelow (note, 128))
        return;
    for (auto instrument : kit->noteToInstruments[(size_t) note])
        for (auto& v : voices)
            if (v.active && ! v.ramping && v.instrument == instrument)
                choke (v, aftertouchMs);
}

void DrumEngine::allNotesOff() noexcept
{
    for (auto& v : voices)
        if (v.active && ! v.ramping)
            choke (v, 20.0f);
}

void DrumEngine::choke (Voice& v, float ms) noexcept
{
    v.ramping  = true;
    v.rampStep = -v.ramp / juce::jmax (1.0f, ms * 0.001f * (float) hostRate);
}

const Kit::Sample* DrumEngine::selectSample (Kit::Instrument& inst, float velocity) noexcept
{
    if (inst.samples.empty())
        return nullptr;

    if (inst.powerBased)
    {
        // DrumGizmo v2 selection: closest power, penalise recently played
        // samples ("diverse"), plus a little randomness.
        const auto level = inst.minPower + velocity * (inst.maxPower - inst.minPower);
        auto range = inst.maxPower - inst.minPower;
        if (range <= 0.0f)
            range = 1.0f;

        size_t best = 0;
        auto bestValue = std::numeric_limits<float>::max();
        for (size_t i = 0; i < inst.samples.size(); ++i)
        {
            const auto close   = (inst.samples[i].power - level) / range;
            const auto since   = (float) juce::jmax<std::int64_t> (0, clock - inst.lastPlayed[i]);
            const auto diverse = 1.0f / (1.0f + since / (float) hostRate);
            const auto value   = fClose * close * close + fDiverse * diverse + fRandom * random01();
            if (value < bestValue)
            {
                bestValue = value;
                best = i;
            }
        }
        inst.lastPlayed[best] = clock;
        return &inst.samples[best];
    }

    // v1: fixed velocity groups; pick randomly among the samples whose range
    // contains the velocity, else from the nearest range.
    int candidates[64];
    int count = 0;
    auto nearest = -1;
    auto nearestDist = std::numeric_limits<float>::max();
    for (const auto& r : inst.ranges)
    {
        if (velocity >= r.lower && velocity <= r.upper)
        {
            if (count < 64)
                candidates[count++] = r.sample;
        }
        else
        {
            const auto d = juce::jmin (std::abs (velocity - r.lower), std::abs (velocity - r.upper));
            if (d < nearestDist)
            {
                nearestDist = d;
                nearest = r.sample;
            }
        }
    }

    if (count > 0)
        return &inst.samples[(size_t) candidates[juce::jmin (count - 1, (int) (random01() * (float) count))]];
    if (nearest >= 0)
        return &inst.samples[(size_t) nearest];
    return &inst.samples[(size_t) juce::jmin ((int) inst.samples.size() - 1, (int) (random01() * (float) inst.samples.size()))];
}

void DrumEngine::trigger (int instrumentIndex, float velocity) noexcept
{
    auto& inst = kit->instruments[(size_t) instrumentIndex];

    // --- humanizer (DrumGizmo's stamina + velocity spread, scaled by amount) ---
    if (humanize > 0.0f)
    {
        const auto since = (float) juce::jmax<std::int64_t> (0, clock - inst.lastHit);
        auto mod = juce::jlimit (0.0f, 1.0f, inst.stamina + since / ((float) hostRate * staminaFalloff));
        velocity *= mod;
        inst.stamina = mod * (1.0f - humanize * (1.0f - staminaWeight));
        inst.lastHit = clock;
        velocity = gaussian (velocity, velocityStddev * humanize);
    }
    velocity = juce::jlimit (0.0f, 1.0f, velocity);

    // --- chokes -------------------------------------------------------------------
    for (auto& v : voices)
    {
        if (! v.active || v.ramping)
            continue;
        if (inst.group >= 0 && v.group == inst.group && v.instrument != instrumentIndex)
            choke (v, groupChokeMs);
        else
            for (const auto& c : inst.chokes)
                if (v.instrument == c.instrument)
                {
                    choke (v, c.ms);
                    break;
                }
    }

    // --- per-instrument voice limit: fade out the oldest -------------------------
    int playing = 0;
    Voice* oldest = nullptr;
    for (auto& v : voices)
        if (v.active && ! v.ramping && v.instrument == instrumentIndex)
        {
            ++playing;
            if (oldest == nullptr || v.started < oldest->started)
                oldest = &v;
        }
    if (playing >= maxVoicesPerInstrument && oldest != nullptr)
        choke (*oldest, voiceLimitMs);

    const auto* sample = selectSample (inst, velocity);
    if (sample == nullptr)
        return;

    // --- allocate a voice (steal the oldest if the pool is full) ---------------------
    Voice* slot = nullptr;
    for (auto& v : voices)
        if (! v.active) { slot = &v; break; }
    if (slot == nullptr)
    {
        slot = &voices[0];
        for (auto& v : voices)
            if (v.started < slot->started)
                slot = &v;
    }

    auto gain = sample->normalized ? velocity : 1.0f;
    gain *= 1.0f - dynamics + dynamics * velocity;

    const auto instGain = kit->instrumentGain[(size_t) instrumentIndex]->load (std::memory_order_relaxed);

    *slot = {};
    slot->instrumentGain = instGain;
    slot->sample     = sample;
    slot->instrument = instrumentIndex;
    slot->group      = inst.group;
    slot->gain       = gain;
    slot->active     = true;
    slot->started    = clock;

    kit->hit[(size_t) instrumentIndex]->store (juce::jmax (0.05f, velocity), std::memory_order_relaxed);
    auto& meter = *kit->instrumentMeter[(size_t) instrumentIndex];
    meter.store (juce::jmax (meter.load (std::memory_order_relaxed), velocity * gain * instGain), std::memory_order_relaxed);

    const auto scope = prefetchFifo.write (1);
    if (scope.blockSize1 > 0)
        prefetchQueue[(size_t) scope.startIndex1] = sample;
}

//==============================================================================
void DrumEngine::render (float* const* channels, int n) noexcept
{
    if (kit == nullptr || n <= 0)
    {
        clock += n;
        return;
    }

    jassert (n <= (int) envelope.size());
    n = juce::jmin (n, (int) envelope.size());

    const auto rateRatio = kit->sampleRate / hostRate;
    const auto numCh     = kit->numChannels();

    for (auto& v : voices)
    {
        if (! v.active)
            continue;

        // pitch = kit Tune + this drum's Tune (DRUMS mixer)
        const auto drumTune  = kit->instrumentTune[(size_t) v.instrument]->load (std::memory_order_relaxed);
        const auto semis     = juce::jlimit (-24.0f, 24.0f, tuneSemitones + drumTune);
        const auto step      = rateRatio * std::exp2 ((double) semis / 12.0);
        const auto integral  = std::abs (step - 1.0) < 1.0e-12;
        const auto drumBleed = bleed * kit->instrumentBleed[(size_t) v.instrument]->load (std::memory_order_relaxed);

        // Envelope = choke ramp x DRUMS-fader ramp. When neither is moving we
        // skip the envelope entirely and fold the fader into a scalar gain.
        const auto target = kit->instrumentGain[(size_t) v.instrument]->load (std::memory_order_relaxed);
        const bool faderMoving = std::abs (target - v.instrumentGain) > 1.0e-6f;
        const float* env = nullptr;
        auto voiceGain = v.gain;

        if (v.ramping || faderMoving)
        {
            auto r = v.ramp;
            auto g = v.instrumentGain;
            const auto gStep = (target - g) / (float) n;
            for (int k = 0; k < n; ++k)
            {
                envelope[(size_t) k] = r * g;
                if (v.ramping)
                    r = juce::jmax (0.0f, r + v.rampStep);
                g += gStep;
            }
            v.ramp = r;
            v.instrumentGain = target;
            env = envelope.data();
        }
        else
        {
            voiceGain *= v.instrumentGain;
        }

        const auto start = (std::int64_t) v.pos;
        if (! integral)
        {
            const auto base = (double) start;
            for (int k = 0; k < n; ++k)
            {
                const auto ph = v.pos + (double) k * step - base;
                const auto r  = (std::int32_t) ph;
                interpRel[(size_t) k]  = r;
                interpFrac[(size_t) k] = (float) (ph - (double) r);
            }
        }

        for (const auto& s : v.sample->streams)
        {
            if (s.kitChannel < 0 || s.kitChannel >= numCh)
                continue;
            const auto g = voiceGain * (s.main ? 1.0f : drumBleed);
            if (g <= 0.0f)
                continue;

            auto* out = channels[s.kitChannel];
            const auto* rel  = interpRel.data();
            const auto* frac = interpFrac.data();
            switch (s.wav->format)
            {
                case Fmt::Int16:   integral ? addStreamDirect<Fmt::Int16>   (*s.wav, s.fileChannel, start, n, env, g, out)
                                            : addStreamInterp<Fmt::Int16>   (*s.wav, s.fileChannel, start, rel, frac, n, env, g, out, decodeScratch.data(), (int) decodeScratch.size()); break;
                case Fmt::Int24:   integral ? addStreamDirect<Fmt::Int24>   (*s.wav, s.fileChannel, start, n, env, g, out)
                                            : addStreamInterp<Fmt::Int24>   (*s.wav, s.fileChannel, start, rel, frac, n, env, g, out, decodeScratch.data(), (int) decodeScratch.size()); break;
                case Fmt::Int32:   integral ? addStreamDirect<Fmt::Int32>   (*s.wav, s.fileChannel, start, n, env, g, out)
                                            : addStreamInterp<Fmt::Int32>   (*s.wav, s.fileChannel, start, rel, frac, n, env, g, out, decodeScratch.data(), (int) decodeScratch.size()); break;
                case Fmt::Float32: integral ? addStreamDirect<Fmt::Float32> (*s.wav, s.fileChannel, start, n, env, g, out)
                                            : addStreamInterp<Fmt::Float32> (*s.wav, s.fileChannel, start, rel, frac, n, env, g, out, decodeScratch.data(), (int) decodeScratch.size()); break;
            }
        }

        v.pos += (double) n * step;
        if ((v.ramping && v.ramp <= 0.0f) || v.pos >= (double) v.sample->length)
            v.active = false;
    }

    clock += n;
}
