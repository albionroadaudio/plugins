#pragma once

#include <juce_core/juce_core.h>
#include <cstdint>
#include <cstring>

// A WAV file mapped straight into memory (POSIX mmap; RAM copy elsewhere).
//
// Kits are often several GB of multichannel WAVs, so we never decode them up
// front. The OS pages audio in on demand; the loader pre-touches every file's
// attack so the first hit never waits on the disk. The file descriptor is
// closed right after mapping so thousand-file kits don't hit fd limits.
class MappedWav
{
public:
    enum class Format { Int16, Int24, Int32, Float32 };

    MappedWav() = default;
    ~MappedWav();
    MappedWav (const MappedWav&) = delete;
    MappedWav& operator= (const MappedWav&) = delete;

    bool open (const juce::File& file, juce::String& error);

    // Ask the OS to start reading the whole file into the page cache (async).
    void adviseWillNeed() const noexcept;
    // Synchronously fault in the first `bytes` of audio so attacks are resident.
    void touchHead (size_t bytes) const noexcept;

    const std::uint8_t* frames = nullptr;   // first byte of the data chunk
    std::int64_t numFrames      = 0;
    int    numChannels   = 0;
    int    bytesPerSample = 0;
    int    bytesPerFrame = 0;
    Format format        = Format::Int16;
    double sampleRate    = 44100.0;

    size_t mappedBytes() const noexcept { return size; }

private:
    bool parse (juce::String& error);

    void*  base = nullptr;
    size_t size = 0;
    bool   isMapped = false;
    juce::HeapBlock<std::uint8_t> heapCopy;   // fallback when mmap is unavailable
};

// --- sample decoding (inlined into the render loops) ---------------------------
namespace wav
{
    template <MappedWav::Format F>
    inline float decode (const std::uint8_t* p) noexcept
    {
        if constexpr (F == MappedWav::Format::Int16)
        {
            std::int16_t v; std::memcpy (&v, p, 2);
            return (float) v * (1.0f / 32768.0f);
        }
        else if constexpr (F == MappedWav::Format::Int24)
        {
            const auto u = (std::uint32_t) p[0] | ((std::uint32_t) p[1] << 8) | ((std::uint32_t) p[2] << 16);
            const auto v = (std::int32_t) (u << 8) >> 8;   // sign-extend 24 -> 32 bit
            return (float) v * (1.0f / 8388608.0f);
        }
        else if constexpr (F == MappedWav::Format::Int32)
        {
            std::int32_t v; std::memcpy (&v, p, 4);
            return (float) v * (1.0f / 2147483648.0f);
        }
        else
        {
            float v; std::memcpy (&v, p, 4);
            return v;
        }
    }
}
