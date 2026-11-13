#pragma once
// Shared helpers for the headless dev tools.
#include "../src/PluginProcessor.h"
#include "../src/AlbionLookAndFeel.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <random>
#include <thread>

namespace juce::detail { bool dispatchNextMessageOnSystemQueue (bool returnIfNoPendingMessages); }

inline void pumpMessages (int ms)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
    while (juce::Time::getMillisecondCounter() < end)
    {
        while (juce::detail::dispatchNextMessageOnSystemQueue (true)) {}
        std::this_thread::sleep_for (std::chrono::milliseconds (2));
    }
}

// Noise generator: white, or "music-like" (pink-ish, -4 dB/oct via a few one-poles)
struct Noise
{
    std::mt19937 rng { 1234 };
    std::normal_distribution<float> n { 0.0f, 1.0f };
    float b0 = 0, b1 = 0, b2 = 0;
    bool pinkish = false;
    float next (float gain)
    {
        const auto w = n (rng);
        if (! pinkish) return w * gain;
        // Paul Kellet's economy pink filter, then a little extra low-pass for ~-4 dB/oct
        b0 = 0.99765f * b0 + w * 0.0990460f;
        b1 = 0.96300f * b1 + w * 0.2965164f;
        b2 = 0.57000f * b2 + w * 1.0526913f;
        return (b0 + b1 + b2 + w * 0.1848f) * gain * 0.25f;
    }
};

// Feed `seconds` of noise through the processor at roughly real-time-friendly pace
// so the analysis thread keeps up (it drops audio if its FIFO overflows).
inline void feed (juce::AudioProcessor& p, Noise& noise, double seconds, float gain, double sr = 48000.0)
{
    juce::AudioBuffer<float> buf (2, 4096);
    juce::MidiBuffer midi;
    for (int b = 0; b < (int) (seconds * sr / 4096); ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 4096; ++i)
                buf.setSample (ch, i, noise.next (gain));
        p.processBlock (buf, midi);
        std::this_thread::sleep_for (std::chrono::milliseconds (6));
    }
    std::this_thread::sleep_for (std::chrono::milliseconds (150));
}

inline void setParam (juce::AudioProcessorValueTreeState& s, const juce::ParameterID& id, float value)
{
    auto* p = s.getParameter (id.getParamID());
    p->setValueNotifyingHost (p->convertTo0to1 (value));
}
