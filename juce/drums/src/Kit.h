#pragma once

#include "MappedWav.h"
#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

// In-memory model of a DrumGizmo kit (drumkit xml + instrument xmls + midimap).
//
// A Kit is built on the loader thread, then handed to the audio thread and is
// immutable from then on, except for the fields marked "audio-thread state"
// and the atomics that carry mixer gains / meters between threads.
struct Kit
{
    static constexpr int maxChannels = 64;

    struct Stream            // one mic channel of one sample
    {
        const MappedWav* wav = nullptr;
        int  fileChannel = 0;     // 0-based channel inside the WAV
        int  kitChannel  = -1;    // output channel index, -1 = unmapped
        bool main        = true;  // false = bleed (attenuated by Bleed)
    };

    struct Sample
    {
        juce::String name;
        float power = 0.0f;
        bool normalized = false;
        std::int64_t length = 0;          // frames (longest stream)
        std::vector<Stream> streams;
    };

    struct VelocityRange { float lower, upper; int sample; };   // v1.0 instruments
    struct Choke { int instrument; float ms; };

    struct Instrument
    {
        juce::String name;
        int group = -1;                   // choke group id, -1 = none
        bool powerBased = true;           // v2.0: select by power; v1.0: velocity ranges
        std::vector<Sample> samples;      // v2.0: sorted by power
        std::vector<VelocityRange> ranges;
        std::vector<Choke> chokes;
        float minPower = 0.0f, maxPower = 1.0f;

        // audio-thread state
        std::vector<std::int64_t> lastPlayed;   // per sample, for the "diverse" term
        float stamina = 1.0f;
        std::int64_t lastHit = 0;
    };

    struct MidiMapEntry { int note; int instrument; };

    // --- metadata ---------------------------------------------------------------
    juce::File   kitFile, midimapFile;
    juce::String name, description;
    double sampleRate = 44100.0;
    juce::File   imageFile;
    bool hasBleedControl = false;

    std::vector<juce::String> channelNames;
    std::vector<Instrument> instruments;
    std::vector<MidiMapEntry> midimap;               // in file order (for the UI)
    std::array<std::vector<int>, 128> noteToInstruments;
    std::vector<juce::String> groupNames;

    std::vector<std::unique_ptr<MappedWav>> files;
    std::uint64_t totalBytes = 0;

    // --- cross-thread data (message thread writes gains, audio writes meters) ---
    std::array<std::atomic<float>, maxChannels> gainL {}, gainR {}, gainDirect {};
    std::array<std::atomic<float>, maxChannels> meter {};      // peak, consumed by UI
    std::vector<std::unique_ptr<std::atomic<float>>> hit;      // per instrument, last velocity
    std::vector<std::unique_ptr<std::atomic<float>>> instrumentGain;   // DRUMS mixer (message -> audio)
    std::vector<std::unique_ptr<std::atomic<float>>> instrumentGate;   // gate threshold vs. hit peak, 0 = off
    std::vector<std::unique_ptr<std::atomic<float>>> instrumentMeter;  // hit level after gain (audio -> UI)

    int numChannels() const noexcept { return (int) channelNames.size(); }
};

namespace kitfiles
{
    // XML files in a kit folder whose root element is <drumkit> / <midimap>.
    juce::Array<juce::File> findKitVariants (const juce::File& folder);
    juce::Array<juce::File> findMidimaps (const juce::File& folder);
}

class KitLoader
{
public:
    // Progress is 0..1; return false from shouldContinue to abort.
    std::unique_ptr<Kit> load (const juce::File& kitFile, juce::File midimapFile,
                               std::atomic<float>& progress,
                               const std::function<bool()>& shouldContinue,
                               juce::String& error);
};
