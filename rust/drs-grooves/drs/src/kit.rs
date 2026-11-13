//! DrumGizmo DRS Kit (DRSKit 2.1) note map.
//!
//! Source: the "MIDI note labels" file published with the kit
//! (https://drumgizmo.org/kits/DRSKit/DRSKit-Reaper_notes.txt). The kit's
//! midimap XML was not checked; if a drum lands on the wrong articulation,
//! fix the note number here.

use drumlib::InstrumentTable;

pub const DRS_KIT: &InstrumentTable = &[
    // code   note  name
    ("KD", 36, "Kick (with contact)"),
    ("KDS", 35, "Kick (without contact)"),
    ("SN", 38, "Snare"),
    ("XS", 37, "Snare rim"),
    ("SR", 40, "Snare rest"),
    ("T1", 47, "Tom 1 (rack)"),
    ("T2", 43, "Tom 2 (floor)"),
    ("T3", 41, "Tom 3 (low floor)"),
    ("HH", 56, "Hi-hat closed"),
    ("HS", 42, "Hi-hat closed shank"),
    ("HSO", 46, "Hi-hat semi-open"),
    ("HO", 58, "Hi-hat open"),
    ("HOT", 61, "Hi-hat open tip"),
    ("HF", 44, "Hi-hat foot"),
    ("RD", 51, "Ride tip"),
    ("RB", 53, "Ride bell (shank)"),
    ("RTB", 68, "Ride tip bell"),
    ("RS", 66, "Ride shank"),
    ("RR", 63, "Ride rest"),
    ("CL", 49, "Crash left (shank)"),
    ("CLT", 54, "Crash left tip"),
    ("CR", 57, "Crash right (shank)"),
    ("CRT", 55, "Crash right tip"),
];

/// Crashes used, in turn, on the downbeat after a fill.
pub const CRASH_AFTER_FILL: &[u8] = &[49, 57];

/// Display order for the step grid, top to bottom.
pub const GRID_ORDER: &[u8] = &[49, 54, 57, 55, 51, 53, 68, 66, 63, 58, 61, 46, 42, 56, 44, 47, 43, 41, 38, 37, 40, 36, 35];

pub fn name_of(note: u8) -> &'static str {
    DRS_KIT.iter().find(|(_, n, _)| *n == note).map(|(_, _, name)| *name).unwrap_or("?")
}

pub fn short_of(note: u8) -> &'static str {
    match note {
        36 => "Kick",
        35 => "Kick ·",
        38 => "Snare",
        37 => "Rim",
        40 => "Sn rest",
        47 => "Tom 1",
        43 => "Tom 2",
        41 => "Tom 3",
        56 => "HH",
        42 => "HH shank",
        46 => "HH semi",
        58 => "HH open",
        61 => "HH op tip",
        44 => "HH foot",
        51 => "Ride",
        53 => "Ride bell",
        68 => "R tip bell",
        66 => "R shank",
        63 => "Ride rest",
        49 => "Crash L",
        54 => "Crash L tip",
        57 => "Crash R",
        55 => "Crash R tip",
        _ => "?",
    }
}
