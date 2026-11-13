#pragma once

#include <array>

// Factory presets. Levels assume tracks peaking around -10..-6 dBFS
// (i.e. averaging roughly -18 dBFS), which is typical for tracked audio.
struct Preset
{
    const char* name;
    float gainDb;
    float thresholdDb;
    int   ratioIndex;    // 0=4:1 1=8:1 2=12:1 3=20:1 4=All
    float kneeDb;
    float attackMs;
    float releaseMs;
    float makeupDb;
    float mixPct;
};

namespace presets
{
    extern const std::array<Preset, 20> factory;
}
