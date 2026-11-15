#include "Presets.h"

namespace presets
{
    // 100 % on Clamp = -10 dB on the loudest peaks (+10 dB makeup); 100 % on Glue = -5 dB (+5 dB makeup).
    //                                      denoise  hpf    clamp  de-ess  mud    nasal  air    glue   out
    const std::array<Preset, 7> factory {{
        { "Vocal Chain (default)",           100.f, true,  100.f,  50.f, -3.0f, -3.0f, 3.0f, 100.f, 0.f },
        { "Gentle",                           60.f, true,   50.f,  35.f, -2.0f, -2.0f, 2.0f,  60.f, 0.f },
        { "Upfront Rock Vocal",              100.f, true,  100.f,  60.f, -3.0f, -4.0f, 4.0f, 100.f, 0.f },
        { "Spoken Word / Podcast",           100.f, true,   80.f,  50.f, -4.0f, -2.0f, 2.0f,  80.f, 0.f },
        { "Bright Singer (tame the top)",    100.f, true,  100.f,  80.f, -3.0f, -5.0f, 1.5f, 100.f, 0.f },
        { "Dark Singer (open it up)",        100.f, true,  100.f,  30.f, -2.0f, -1.5f, 5.0f, 100.f, 0.f },
        { "Clean Slate (everything off)",      0.f, false,   0.f,   0.f,  0.0f,  0.0f, 0.0f,   0.f, 0.f, false },
    }};
}
