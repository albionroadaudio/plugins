#include "Presets.h"

namespace presets
{
    // Mix: 50% = dry and wet both at unity; below that the dry stays at unity
    // and the echo is turned down, which is how slapback is normally used.
    //                                      time   fb  mode   tone  lowcut drive  wow spread  mix
    const std::array<Preset, 22> factory {{
        { "Default",                        120.0f, 10.0f, 0,  6000.0f, 120.0f, 30.0f, 30.0f,   0.0f, 30.0f },

        // --- tape ------------------------------------------------------------
        { "Tape Slapback - Classic 50s",    110.0f,  8.0f, 0,  4500.0f, 150.0f, 45.0f, 35.0f,   0.0f, 35.0f },
        { "Tape Slapback - Rockabilly",      95.0f, 15.0f, 0,  5000.0f, 120.0f, 55.0f, 30.0f,   0.0f, 40.0f },
        { "Tape Slapback - Worn Machine",   130.0f, 12.0f, 0,  3200.0f, 200.0f, 60.0f, 80.0f,  10.0f, 35.0f },
        { "Tape Slapback - Hot Saturated",  105.0f, 20.0f, 0,  4000.0f, 160.0f, 85.0f, 35.0f,   0.0f, 35.0f },

        // --- analog / bucket-brigade -----------------------------------------
        { "Analog BBD - Warm",              100.0f, 15.0f, 1,  3500.0f, 120.0f, 30.0f, 25.0f,  15.0f, 30.0f },
        { "Analog BBD - Dark Pedal",        140.0f, 25.0f, 1,  2400.0f, 150.0f, 40.0f, 35.0f,   0.0f, 35.0f },
        { "Analog BBD - Chorused Slap",     115.0f, 12.0f, 1,  3000.0f, 140.0f, 25.0f, 70.0f,  25.0f, 35.0f },

        // --- digital ---------------------------------------------------------
        { "Digital - Clean",                100.0f,  0.0f, 2, 16000.0f,  60.0f,  0.0f,  0.0f,   0.0f, 30.0f },
        { "Digital - Wide Double",           70.0f,  0.0f, 2, 12000.0f, 150.0f,  0.0f, 10.0f,  70.0f, 30.0f },
        { "Digital - Haas Widener",          30.0f,  0.0f, 2, 14000.0f, 100.0f,  0.0f, 15.0f, 100.0f, 35.0f },

        // --- guitar ----------------------------------------------------------
        { "Guitar - Rockabilly Lead",       105.0f, 18.0f, 0,  4500.0f, 180.0f, 50.0f, 30.0f,   0.0f, 40.0f },
        { "Guitar - Country Twang",          85.0f, 10.0f, 0,  5500.0f, 200.0f, 35.0f, 25.0f,   0.0f, 30.0f },
        { "Guitar - Surf",                  120.0f, 30.0f, 1,  3500.0f, 150.0f, 35.0f, 40.0f,  20.0f, 40.0f },
        { "Guitar - Blues Room",            140.0f, 15.0f, 1,  3000.0f, 120.0f, 30.0f, 20.0f,  10.0f, 25.0f },

        // --- vocals ----------------------------------------------------------
        { "Vocal - 50s Slap",               120.0f,  5.0f, 0,  4000.0f, 250.0f, 35.0f, 25.0f,   0.0f, 25.0f },
        { "Vocal - Modern Thickener",        80.0f,  0.0f, 2,  8000.0f, 300.0f, 10.0f, 15.0f,  50.0f, 18.0f },
        { "Vocal - Lo-Fi Telephone Slap",   130.0f, 10.0f, 1,  2500.0f, 600.0f, 60.0f, 30.0f,   0.0f, 30.0f },

        // --- drums -----------------------------------------------------------
        { "Snare - Slap Room",               75.0f,  0.0f, 0,  6000.0f, 300.0f, 40.0f, 15.0f,  30.0f, 25.0f },
        { "Snare - Rockabilly Smack",        95.0f,  5.0f, 0,  5000.0f, 250.0f, 50.0f, 20.0f,   0.0f, 30.0f },
        { "Drums - Short Ambience",          50.0f, 10.0f, 2,  9000.0f, 200.0f, 10.0f,  5.0f,  60.0f, 20.0f },

        // --- other -----------------------------------------------------------
        { "Keys & Horns - Vintage Slap",    115.0f, 12.0f, 0,  4000.0f, 200.0f, 45.0f, 30.0f,   0.0f, 30.0f },
    }};
}
