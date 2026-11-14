#pragma once

#include <array>

// The seven reverb voices: one per row of the guitar/bass reverb cheat-sheet.
// Each voice picks an engine, sets that engine's colour, and carries the
// recommended zones that the editor draws as brass arcs on the knobs.
// Clicking a voice in the editor also moves Decay, Pre-delay, Low Cut and
// High Cut to the voice's defaults.
enum class EngineKind { TightRoom, StudioRoom, Chamber, Plate, Spring };

struct Zone
{
    float lo, hi, def;
};

struct VoiceDef
{
    const char* name;          // host-facing choice name
    const char* instrument;    // "BASS", "RHYTHM", "LEAD"
    const char* button;        // button caption
    const char* blurb;         // one-line description under the buttons
    EngineKind  engine;

    Zone decay;                // seconds (RT60)
    Zone lowCut;               // Hz, steep high-pass before the reverb
    Zone highCut;              // Hz, low-pass before the reverb
    Zone preDelay;             // ms

    // engine colour
    float hfRatio;             // rooms/chamber: high-frequency RT60 / low-frequency RT60
    float bandwidthHz;         // plate: input bandwidth low-pass
    float dampHz;              // plate/spring: in-loop damping low-pass
    float mod;                 // 0..1 modulation (rooms/plate: chorus on the tail; spring: wobble)
};

namespace voices
{
    constexpr int count = 7;
    constexpr int defaultVoice = 2;   // Rhythm · Studio Room

    //                                                                                           decay              low cut            high cut                 pre-delay
    inline constexpr std::array<VoiceDef, count> table {{
        { "Bass - Tight Room",     "BASS",   "TIGHT ROOM",  "Tight room ambience. Keeps the low end dry and focused.",
          EngineKind::TightRoom,  { 0.3f, 0.7f, 0.45f }, { 250.f, 400.f, 300.f }, { 3000.f, 5000.f, 4000.f }, {  0.f, 15.f,  5.f },
          0.55f, 0.0f, 0.0f, 0.25f },
        { "Bass - Plate",          "BASS",   "PLATE",       "Short, smooth plate. Adds sheen to the upper harmonics only.",
          EngineKind::Plate,      { 0.5f, 1.0f, 0.70f }, { 250.f, 400.f, 300.f }, { 3000.f, 5000.f, 4000.f }, {  0.f, 20.f, 10.f },
          0.0f, 9000.0f, 7500.0f, 0.30f },
        { "Rhythm - Studio Room",  "RHYTHM", "STUDIO ROOM", "Natural live-room depth for distorted rhythm parts.",
          EngineKind::StudioRoom, { 0.4f, 0.9f, 0.60f }, { 150.f, 250.f, 200.f }, { 4000.f, 6000.f, 5000.f }, { 20.f, 50.f, 20.f },
          0.50f, 0.0f, 0.0f, 0.25f },
        { "Rhythm - Chamber",      "RHYTHM", "CHAMBER",     "Echo chamber: denser and smoother than the room.",
          EngineKind::Chamber,    { 0.4f, 0.9f, 0.70f }, { 150.f, 250.f, 200.f }, { 4000.f, 6000.f, 5000.f }, { 20.f, 50.f, 25.f },
          0.65f, 0.0f, 0.0f, 0.30f },
        { "Rhythm - Dark Plate",   "RHYTHM", "DARK PLATE",  "Dark plate that sits behind a wall of rhythm guitars.",
          EngineKind::Plate,      { 0.8f, 1.5f, 1.10f }, { 150.f, 250.f, 200.f }, { 4000.f, 6000.f, 5000.f }, { 20.f, 50.f, 30.f },
          0.0f, 5000.0f, 3200.0f, 0.20f },
        { "Rhythm - Spring",       "RHYTHM", "SPRING",      "Amp-style spring tank, with pre-delay to keep the pick attack clear.",
          EngineKind::Spring,     { 0.8f, 1.5f, 1.10f }, { 150.f, 250.f, 200.f }, { 4000.f, 6000.f, 5000.f }, { 20.f, 50.f, 30.f },
          0.0f, 0.0f, 4500.0f, 0.50f },
        { "Lead - Dark Plate (EMT 140)", "LEAD", "DARK PLATE  \xc2\xb7  EMT 140", "Lush dark plate for solos. 30 ms pre-delay keeps the notes in front.",
          EngineKind::Plate,      { 1.0f, 1.8f, 1.40f }, { 200.f, 300.f, 250.f }, { 5000.f, 7000.f, 6000.f }, { 20.f, 40.f, 30.f },
          0.0f, 6500.0f, 3800.0f, 0.60f },
    }};
}
