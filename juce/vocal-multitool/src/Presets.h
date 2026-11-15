#pragma once

#include <array>

struct Preset
{
    const char* name;
    float denoise;   // %
    bool  hpf;
    float comp1;     // %
    float deess;     // %
    float mud;       // dB (cut)
    float nasal;     // dB (cut)
    float air;       // dB (boost)
    float comp2;     // %
    float output;    // dB
    bool  sectionsOn = true;   // the on/off switches of denoise, compressor, de-esser, EQ and glue
};

namespace presets
{
    extern const std::array<Preset, 7> factory;
}
