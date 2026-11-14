//! Albion Road Audio — Toms EQ.
//!
//! One plugin for rack and floor toms: the fundamental zone is wide enough for both.

use eqcore::{BandDef, BandKind, EqPlugin, Instrument, InstrumentDef, Move};

pub const TOMS: InstrumentDef = InstrumentDef {
    name: "Toms",
    hp_default: 5, // 50 Hz
    lp_default: 4, // 15 kHz
    bands: &[
        BandDef { name: "Fundamental", hint: Move::Boost, kind: BandKind::Peak, freq: 100.0, min: 60.0, max: 160.0, q: 1.0 },
        BandDef { name: "Body", hint: Move::Boost, kind: BandKind::Peak, freq: 320.0, min: 250.0, max: 400.0, q: 1.4 },
        BandDef { name: "Ring / boxy", hint: Move::Cut, kind: BandKind::Peak, freq: 500.0, min: 400.0, max: 700.0, q: 2.0 },
        BandDef { name: "Click", hint: Move::Boost, kind: BandKind::Peak, freq: 5000.0, min: 3500.0, max: 7000.0, q: 1.0 },
    ],
    ..InstrumentDef::SERIES
};

pub struct Toms;

impl Instrument for Toms {
    const NAME: &'static str = "ARA Toms EQ";
    const CLAP_ID: &'static str = "com.albionroadaudio.toms-eq";
    const VST3_ID: [u8; 16] = *b"AlbionRdTomsEQ01";
    const DESCRIPTION: &'static str = "Surgical tom EQ: low cut, fundamental, body, ring/boxy, click, high cut";
    const DEF: &'static InstrumentDef = &TOMS;
}

pub type TomsEq = EqPlugin<Toms>;

eqcore::export_eq_plugin!(TomsEq);
