#pragma once

#include "Kit.h"
#include <array>

// Real-time playback of a Kit. Everything here runs on the audio thread:
// no allocation, no locks, no file I/O after prepare().
class DrumEngine
{
public:
    static constexpr int maxVoices            = 256;
    static constexpr int maxVoicesPerInstrument = 16;
    static constexpr float gateReleaseMs = 20.0f;   // gate close time (fade-out)

    void prepare (double hostSampleRate);
    void setKit (Kit* newKit) noexcept;     // kills all voices
    Kit* getKit() const noexcept { return kit; }

    // Playing parameters (block rate)
    float tuneSemitones = 0.0f;
    float bleed         = 1.0f;   // 0..1 gain on non-main (bleed) channels
    float humanize      = 0.0f;   // 0..1, 1 = DrumGizmo's humanizer defaults
    float dynamics      = 0.0f;   // 0..1 extra velocity -> level scaling

    void noteOn (int note, float velocity01) noexcept;
    void chokeNote (int note) noexcept;     // polyphonic aftertouch choke
    void allNotesOff() noexcept;

    // Adds n samples of every kit channel into channels[0..kit->numChannels()).
    void render (float* const* channels, int n) noexcept;

    int activeVoices() const noexcept;

    // Lock-free queue of samples just triggered, drained by the loader thread
    // to madvise() them (keeps tails in the page cache under memory pressure).
    static constexpr int prefetchCapacity = 256;
    juce::AbstractFifo prefetchFifo { prefetchCapacity };
    std::array<const Kit::Sample*, prefetchCapacity> prefetchQueue {};

private:
    struct Voice
    {
        const Kit::Sample* sample = nullptr;
        int    instrument = -1;
        int    group = -1;
        double pos = 0.0;
        float  gain = 1.0f;          // velocity / dynamics
        float  instrumentGain = 1.0f; // DRUMS mixer fader, ramped per block
        float  detector = 0.0f;       // gate: peak follower on the main mics
        float  detectorPeak = 0.0f;   // gate: loudest point of this hit so far
        float  ramp = 1.0f, rampStep = 0.0f;
        bool   ramping = false;
        bool   active = false;
        std::int64_t started = 0;
    };

    void trigger (int instrument, float velocity) noexcept;
    void choke (Voice&, float ms) noexcept;
    const Kit::Sample* selectSample (Kit::Instrument&, float velocity) noexcept;
    float random01() noexcept;
    float gaussian (float mean, float stddev) noexcept;

    Kit* kit = nullptr;
    double hostRate = 44100.0;
    std::int64_t clock = 0;        // samples rendered since prepare, host rate
    std::uint64_t rngState = 0x9E3779B97F4A7C15ull;

    std::array<Voice, maxVoices> voices {};
    std::array<float, 4096> envelope {};
    std::array<std::int32_t, 4096> interpRel {};
    std::array<float, 4096> interpFrac {};
    std::array<float, 4096 * 5 + 8> decodeScratch {};   // up to ~4.4x read speed (96k kit, +12 st); faster reads
                                                         // (kit + drum tune above that) use the uncached path
};
