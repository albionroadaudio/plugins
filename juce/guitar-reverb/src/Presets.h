#pragma once

#include <array>

struct Preset
{
    const char* name;
    int   voice;         // index into voices::table
    float decayS;
    float preDelayMs;
    float lowCutHz;
    float highCutHz;
    float widthPct;
    float duckPct;
    float mixPct;
};

namespace presets
{
    extern const std::array<Preset, 16> factory;
}
