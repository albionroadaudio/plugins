#pragma once

#include <array>

struct Preset
{
    const char* name;
    float timeMs;
    float feedbackPct;
    int   mode;          // 0 = Tape, 1 = BBD, 2 = Digital
    float toneHz;        // high cut
    float lowCutHz;
    float drivePct;
    float wowPct;
    float spreadPct;
    float mixPct;
};

namespace presets
{
    extern const std::array<Preset, 22> factory;
}
