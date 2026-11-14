//! Albion Road Audio — Bass EQ.
//!
//! Bands follow musicguymixing.com/bass-guitar-eq: high-pass the rumble at 20–40 Hz, cut a
//! small space for the kick at 40–80 Hz, boost the fundamental around 100 Hz, a wide cut
//! for boxiness around 300 Hz, boost the string attack at 4–6 kHz, and roll off the top
//! around 7 kHz. The high cut has its own steps (4–20 kHz) so it can reach that low.

use eqcore::{BandDef, BandKind, EqPlugin, Instrument, InstrumentDef, Move};

/// High cut positions in kHz; index 0 is "off".
pub const BASS_LP_STEPS_KHZ: [f32; 12] = [0.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0, 12.0, 14.0, 16.0, 20.0];

pub const BASS: InstrumentDef = InstrumentDef {
    name: "Bass",
    hp_default: 3, // 30 Hz
    lp_default: 4, // 7 kHz
    bands: &[
        BandDef { name: "Room for kick", hint: Move::Cut, kind: BandKind::Peak, freq: 60.0, min: 40.0, max: 80.0, q: 2.0 },
        BandDef { name: "Fundamental", hint: Move::Boost, kind: BandKind::Peak, freq: 100.0, min: 80.0, max: 140.0, q: 1.2 },
        BandDef { name: "Boxy", hint: Move::Cut, kind: BandKind::Peak, freq: 300.0, min: 250.0, max: 500.0, q: 0.9 },
        BandDef { name: "Attack", hint: Move::Boost, kind: BandKind::Peak, freq: 5000.0, min: 4000.0, max: 6000.0, q: 1.0 },
    ],
    lp_steps_khz: &BASS_LP_STEPS_KHZ,
    ..InstrumentDef::SERIES
};

pub struct Bass;

impl Instrument for Bass {
    const NAME: &'static str = "ARA Bass EQ";
    const CLAP_ID: &'static str = "com.albionroadaudio.bass-eq";
    const VST3_ID: [u8; 16] = *b"AlbionRdBassEQ01";
    const DESCRIPTION: &'static str = "Surgical bass guitar EQ: low cut, room for kick, fundamental, boxy, attack, high cut";
    const DEF: &'static InstrumentDef = &BASS;
}

pub type BassEq = EqPlugin<Bass>;

eqcore::export_eq_plugin!(BassEq);
