#pragma once

#include "Voices.h"
#include <array>
#include <cstdint>
#include <vector>

// The reverb engines. Each takes a mono input block and writes a stereo wet
// block. All of them are zero-latency and allocate only in prepare().
namespace rvb
{
    // Power-of-two circular delay line. write() then read (d) returns the
    // sample written d samples ago (d >= 1).
    class DelayLine
    {
    public:
        void prepare (int maxDelaySamples);
        void clear();
        void write (float x) noexcept               { buf[(size_t) pos] = x; pos = (pos + 1) & mask; }
        float read (int d) const noexcept           { return buf[(size_t) ((pos - d) & mask)]; }
        float readFrac (float d) const noexcept;    // linear interpolation
    private:
        std::vector<float> buf;
        int mask = 0, pos = 0;
    };

    // Schroeder allpass around a delay line (used as diffuser).
    struct Allpass
    {
        DelayLine line;
        int   delay = 1;
        float g = 0.5f;
        float process (float x) noexcept
        {
            const auto d = line.read (delay);
            const auto v = x + g * d;
            line.write (v);
            return d - g * v;
        }
    };

    struct OnePoleLP
    {
        float a = 0.0f, z = 0.0f;               // y = x + a (y - x)
        void setCutoff (double hz, double sr) noexcept;
        float process (float x) noexcept        { z = x + a * (z - x); return z; }
    };

    //==========================================================================
    class Engine
    {
    public:
        virtual ~Engine() = default;
        virtual void prepare (double sampleRate) = 0;
        virtual void reset() = 0;
        // rt60 in seconds; colour comes from the voice table
        virtual void setVoice (const VoiceDef& v, float rt60) = 0;
        virtual void process (const float* in, float* outL, float* outR, int n) noexcept = 0;
    };

    //==========================================================================
    // 8-line feedback delay network (Householder matrix) with Jot-style
    // frequency-dependent absorption, input diffusion and early reflections.
    // Three sizes: tight room, studio room, chamber.
    class FdnRoom final : public Engine
    {
    public:
        static constexpr int N = 8;
        struct Shape
        {
            std::array<float, N> lineMs;      // late-reverb delay lengths
            std::array<float, 4> diffuseMs;   // input allpass diffusers
            float diffusion;                  // allpass gain
            std::array<float, 8> erMs;        // early reflection taps
            std::array<float, 8> erGain;
            float erLevel, lateLevel;
            float modMs;                      // max delay modulation depth
        };

        explicit FdnRoom (const Shape& s) : shape (s) {}
        void prepare (double sampleRate) override;
        void reset() override;
        void setVoice (const VoiceDef& v, float rt60) override;
        void process (const float* in, float* outL, float* outR, int n) noexcept override;

    private:
        Shape shape;
        double sr = 48000.0;
        std::array<DelayLine, N> lines;
        std::array<float, N> lenSamples {};
        std::array<float, N> gain {}, dampP {}, dampZ {};
        std::array<float, N> modPhase {};
        std::array<Allpass, 4> diffusers;
        DelayLine erLine;
        std::array<int, 8> erTap {};
        float modDepth = 0.0f, modInc = 0.0f;
    };

    //==========================================================================
    // Dattorro / Griesinger plate (JAES 1997 "Effect Design Part 1"), with
    // input bandwidth, tank damping and modulation set per voice.
    class Plate final : public Engine
    {
    public:
        void prepare (double sampleRate) override;
        void reset() override;
        void setVoice (const VoiceDef& v, float rt60) override;
        void process (const float* in, float* outL, float* outR, int n) noexcept override;

    private:
        double sr = 48000.0;
        double scale = 1.0;                // sr / 29761
        OnePoleLP bandwidth;
        std::array<Allpass, 4> inputDiff;

        // tank halves: modulated allpass -> delay -> damping -> decay -> allpass -> delay -> decay
        DelayLine apMod[2];
        float apModLen[2] {};
        DelayLine del1[2], del2[2];
        int del1Len[2] {}, del2Len[2] {};
        Allpass ap2[2];
        OnePoleLP damp[2];
        float tankFeed[2] {};

        float decay = 0.5f, decayDiff1 = 0.70f;
        float excursion = 0.0f, lfoPhase = 0.0f, lfoInc = 0.0f;

        std::array<int, 7> tapL {}, tapR {};
    };

    //==========================================================================
    // Three-spring tank: each spring is a feedback loop holding a long cascade
    // of first-order allpasses (dispersion -> the descending "chirp"), a
    // transit delay, a damping low-pass and a DC blocker. Springs 1 and 2 feed
    // left and right, spring 3 is shared.
    class Spring final : public Engine
    {
    public:
        static constexpr int numSprings = 3;
        static constexpr int stages = 48;

        void prepare (double sampleRate) override;
        void reset() override;
        void setVoice (const VoiceDef& v, float rt60) override;
        void process (const float* in, float* outL, float* outR, int n) noexcept override;

    private:
        struct OneSpring
        {
            std::array<float, stages> apX {}, apY {};   // first-order allpass states
            DelayLine transit;
            float transitSamples = 1000.0f;
            OnePoleLP damp;
            float dcX = 0.0f, dcY = 0.0f;
            float fb = 0.0f, g = 0.5f;
            float wobble = 0.0f;
        };

        double sr = 48000.0;
        std::array<OneSpring, numSprings> springs;
        float apCoeff = -0.6f, dcCoeff = 0.995f;
        float wobbleDepth = 0.0f, wobbleCoeff = 0.999f;
        std::uint32_t rng = 0x9e3779b9u;
    };
}
