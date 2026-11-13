//! Albion Road Audio — Hi-Hat EQ.
//!
//! Bands follow musicguymixing.com/hi-hat-eq: low cut 150 Hz, body 300–500 Hz (boost if thin,
//! cut if muddy), cut harshness around 4–5 kHz, boost sparkle at 6–9 kHz, high cut 20 kHz.

use eqcore::{BandDef, BandKind, EqPlugin, Instrument, InstrumentDef, Move};

pub const HIHAT: InstrumentDef = InstrumentDef {
    name: "Hi-Hat",
    hp_default: 15, // 150 Hz
    lp_default: 9, // 20 kHz
    bands: &[
        BandDef { name: "Body", hint: Move::Either, kind: BandKind::Peak, freq: 400.0, min: 300.0, max: 500.0, q: 1.4 },
        BandDef { name: "Harsh", hint: Move::Cut, kind: BandKind::Peak, freq: 4500.0, min: 3500.0, max: 5500.0, q: 1.8 },
        BandDef { name: "Sparkle", hint: Move::Boost, kind: BandKind::Peak, freq: 7500.0, min: 6000.0, max: 9000.0, q: 1.0 },
    ],
};

pub struct HiHat;

impl Instrument for HiHat {
    const NAME: &'static str = "ARA Hi-Hat EQ";
    const CLAP_ID: &'static str = "com.albionroadaudio.hihat-eq";
    const VST3_ID: [u8; 16] = *b"AlbionRdHatEQ001";
    const DESCRIPTION: &'static str = "Surgical hi-hat EQ: low cut, body, harsh, sparkle, high cut";
    const DEF: &'static InstrumentDef = &HIHAT;
}

pub type HiHatEq = EqPlugin<HiHat>;

eqcore::export_eq_plugin!(HiHatEq);
