#include "Presets.h"

namespace presets
{
    // Voices: 0 Bass Tight Room, 1 Bass Plate, 2 Rhythm Studio Room, 3 Rhythm Chamber,
    //         4 Rhythm Dark Plate, 5 Rhythm Spring, 6 Lead Dark Plate (EMT 140).
    // Mix: 50% = dry and wet both at unity. "Send" presets are 100% wet, for an FX/aux track.
    //                                        voice decay  pre  lowcut  highcut  width duck   mix
    const std::array<Preset, 16> factory {{
        { "Default",                             2, 0.60f, 20.f, 200.f, 5000.f, 100.f,  0.f,  25.f },

        // --- bass --------------------------------------------------------------
        { "Bass - Tight Room",                   0, 0.45f,  5.f, 300.f, 4000.f,  80.f,  0.f,  20.f },
        { "Bass - Plate",                        1, 0.70f, 10.f, 300.f, 4000.f, 100.f,  0.f,  18.f },
        { "Bass - Subtle Glue",                  0, 0.35f,  0.f, 350.f, 3500.f,  60.f,  0.f,  12.f },
        { "Bass - Send (Room)",                  0, 0.50f,  5.f, 300.f, 4000.f,  80.f,  0.f, 100.f },

        // --- distorted rhythm --------------------------------------------------
        { "Rhythm - Studio Room",                2, 0.60f, 20.f, 200.f, 5000.f, 100.f,  0.f,  25.f },
        { "Rhythm - Chamber",                    3, 0.70f, 25.f, 200.f, 5000.f, 100.f,  0.f,  22.f },
        { "Rhythm - Dark Plate",                 4, 1.10f, 30.f, 200.f, 5000.f, 100.f, 20.f,  20.f },
        { "Rhythm - Spring",                     5, 1.10f, 30.f, 200.f, 5000.f, 100.f,  0.f,  25.f },
        { "Rhythm - Tight Wall",                 2, 0.45f, 20.f, 220.f, 4500.f, 100.f, 30.f,  20.f },
        { "Rhythm - Surf Spring",                5, 1.50f, 20.f, 180.f, 5500.f, 100.f,  0.f,  40.f },
        { "Rhythm - Send (Dark Plate)",          4, 1.10f, 30.f, 200.f, 5000.f, 100.f,  0.f, 100.f },

        // --- distorted lead ----------------------------------------------------
        { "Lead - Dark Plate (EMT 140)",         6, 1.40f, 30.f, 250.f, 6000.f, 100.f,  0.f,  25.f },
        { "Lead - Ducked Solo",                  6, 1.60f, 30.f, 250.f, 6000.f, 100.f, 60.f,  30.f },
        { "Lead - Long Sustain",                 6, 1.80f, 35.f, 250.f, 6500.f, 100.f, 30.f,  30.f },
        { "Lead - Send (EMT 140)",               6, 1.40f, 30.f, 250.f, 6000.f, 100.f,  0.f, 100.f },
    }};
}
