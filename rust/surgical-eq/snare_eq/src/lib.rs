//! Albion Road Audio — Snare EQ.
//!
//! Low cut the kick bleed, add body, cut the box, tame or keep the ring, and boost the crack.

use eqcore::{BandDef, BandKind, EqPlugin, Instrument, InstrumentDef, Move};

pub const SNARE: InstrumentDef = InstrumentDef {
    name: "Snare",
    hp_default: 7, // 70 Hz
    lp_default: 4, // 15 kHz
    bands: &[
        BandDef { name: "Body", hint: Move::Boost, kind: BandKind::Peak, freq: 180.0, min: 150.0, max: 250.0, q: 1.2 },
        BandDef { name: "Boxy", hint: Move::Cut, kind: BandKind::Peak, freq: 400.0, min: 300.0, max: 550.0, q: 1.8 },
        BandDef { name: "Ring", hint: Move::Either, kind: BandKind::Peak, freq: 800.0, min: 600.0, max: 1100.0, q: 2.0 },
        BandDef { name: "Crack", hint: Move::Boost, kind: BandKind::Peak, freq: 5000.0, min: 3500.0, max: 7000.0, q: 1.0 },
    ],
    ..InstrumentDef::SERIES
};

pub struct Snare;

impl Instrument for Snare {
    const NAME: &'static str = "ARA Snare EQ";
    const CLAP_ID: &'static str = "com.albionroadaudio.snare-eq";
    const VST3_ID: [u8; 16] = *b"AlbionRdSnareEQ1";
    const DESCRIPTION: &'static str = "Surgical snare drum EQ: low cut, body, boxy, ring, crack, high cut";
    const DEF: &'static InstrumentDef = &SNARE;
}

pub type SnareEq = EqPlugin<Snare>;

eqcore::export_eq_plugin!(SnareEq);
