//! Albion Road Audio — Crash EQ.
//!
//! Low cut everything below the cymbal, shape the body, cut drum bleed and harshness, add sizzle.

use eqcore::{BandDef, BandKind, EqPlugin, Instrument, InstrumentDef, Move};

pub const CRASH: InstrumentDef = InstrumentDef {
    name: "Crash",
    hp_default: 15, // 150 Hz
    lp_default: 11, // 22 kHz
    bands: &[
        BandDef { name: "Body", hint: Move::Either, kind: BandKind::Peak, freq: 340.0, min: 280.0, max: 400.0, q: 1.2 },
        BandDef { name: "Bleed", hint: Move::Cut, kind: BandKind::Peak, freq: 500.0, min: 400.0, max: 650.0, q: 1.6 },
        BandDef { name: "Harsh", hint: Move::Cut, kind: BandKind::Peak, freq: 4000.0, min: 3000.0, max: 5500.0, q: 1.8 },
        BandDef { name: "Sizzle", hint: Move::Boost, kind: BandKind::Peak, freq: 10000.0, min: 8000.0, max: 14000.0, q: 0.8 },
    ],
    ..InstrumentDef::SERIES
};

pub struct Crash;

impl Instrument for Crash {
    const NAME: &'static str = "ARA Crash EQ";
    const CLAP_ID: &'static str = "com.albionroadaudio.crash-eq";
    const VST3_ID: [u8; 16] = *b"AlbionRdCrashEQ1";
    const DESCRIPTION: &'static str = "Surgical crash cymbal EQ: low cut, body, bleed, harsh, sizzle, high cut";
    const DEF: &'static InstrumentDef = &CRASH;
}

pub type CrashEq = EqPlugin<Crash>;

eqcore::export_eq_plugin!(CrashEq);
