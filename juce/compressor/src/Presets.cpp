#include "Presets.h"

namespace presets
{
    //                          name                          gain   thr  ratio knee  atk ms  rel ms  makeup  mix
    const std::array<Preset, 20> factory {{
        { "Default",                                         0.0f, -20.0f, 0,  6.0f,  0.50f, 200.0f,  0.0f, 100.0f },

        // --- drums -----------------------------------------------------------
        { "Drums - Bus Glue",                                0.0f, -16.0f, 0,  8.0f, 10.00f, 150.0f,  2.5f, 100.0f },
        { "Drums - Parallel Smash",                          4.0f, -30.0f, 4,  0.0f,  0.05f,  60.0f,  6.0f,  35.0f },
        { "Drums - Kick Punch",                              0.0f, -18.0f, 0,  4.0f,  8.00f,  70.0f,  3.0f, 100.0f },
        { "Drums - Snare Crack",                             0.0f, -20.0f, 1,  3.0f,  3.00f,  90.0f,  4.0f, 100.0f },
        { "Drums - Room Mic Crush",                          6.0f, -32.0f, 4,  0.0f,  0.02f,  50.0f,  4.0f, 100.0f },
        { "Drums - Overheads Tame",                          0.0f, -14.0f, 0, 10.0f,  5.00f, 250.0f,  1.5f, 100.0f },

        // --- vocals ----------------------------------------------------------
        { "Vocal - Lead Smooth",                             0.0f, -22.0f, 0,  8.0f,  1.50f, 250.0f,  4.0f, 100.0f },
        { "Vocal - Rock Aggressive",                         3.0f, -24.0f, 1,  4.0f,  0.40f, 120.0f,  5.0f, 100.0f },
        { "Vocal - Gentle Levelling",                        0.0f, -18.0f, 0, 12.0f,  6.00f, 450.0f,  2.5f, 100.0f },
        { "Vocal - Backing Stack",                           0.0f, -26.0f, 1,  6.0f,  2.00f, 300.0f,  5.0f, 100.0f },
        { "Vocal - Rap Upfront",                             2.0f, -24.0f, 2,  3.0f,  0.30f,  80.0f,  5.0f, 100.0f },

        // --- bass ------------------------------------------------------------
        { "Bass - DI Even",                                  0.0f, -22.0f, 0,  6.0f,  3.00f, 180.0f,  4.0f, 100.0f },
        { "Bass - Pick Grit",                                4.0f, -24.0f, 1,  3.0f,  0.80f, 120.0f,  3.0f, 100.0f },
        { "Bass - Fingerstyle Warm",                         0.0f, -20.0f, 0, 10.0f, 12.00f, 300.0f,  3.0f, 100.0f },

        // --- guitars & keys -------------------------------------------------
        { "Acoustic Gtr - Strum",                            0.0f, -20.0f, 0, 10.0f,  4.00f, 220.0f,  3.0f, 100.0f },
        { "Acoustic Gtr - Fingerpicked",                     0.0f, -22.0f, 0, 12.0f,  8.00f, 350.0f,  2.5f, 100.0f },
        { "Electric Gtr - Clean Sustain",                    0.0f, -24.0f, 1,  4.0f,  1.00f, 400.0f,  5.0f, 100.0f },
        { "Piano - Natural",                                 0.0f, -16.0f, 0, 12.0f, 10.00f, 400.0f,  2.0f, 100.0f },

        // --- utility ---------------------------------------------------------
        { "Peak Limiter 20:1",                               0.0f,  -6.0f, 3,  0.0f,  0.05f, 100.0f,  4.0f, 100.0f },
    }};
}
