//! Albion Road Audio — Kick EQ.
//!
//! Bands follow the usual kick moves: high-pass the sub rumble, boost the body,
//! cut to leave room for the bass, cut the boxiness, boost the beater click, and
//! roll off the top for headroom.

use eqcore::{BandDef, BandKind, EqPlugin, Instrument, InstrumentDef, Move};

pub const KICK: InstrumentDef = InstrumentDef {
    name: "Kick",
    hp_default: 2, // 20 Hz
    lp_default: 4, // 15 kHz
    bands: &[
        BandDef { name: "Body", hint: Move::Boost, kind: BandKind::Peak, freq: 60.0, min: 50.0, max: 80.0, q: 1.0 },
        BandDef { name: "Room for bass", hint: Move::Cut, kind: BandKind::Peak, freq: 150.0, min: 120.0, max: 200.0, q: 1.4 },
        BandDef { name: "Boxy", hint: Move::Cut, kind: BandKind::Peak, freq: 400.0, min: 300.0, max: 550.0, q: 1.8 },
        BandDef { name: "Click", hint: Move::Boost, kind: BandKind::Peak, freq: 3500.0, min: 2500.0, max: 5000.0, q: 1.2 },
    ],
};

pub struct Kick;

impl Instrument for Kick {
    const NAME: &'static str = "ARA Kick EQ";
    const CLAP_ID: &'static str = "com.albionroadaudio.kick-eq";
    const VST3_ID: [u8; 16] = *b"AlbionRdKickEQ01";
    const DESCRIPTION: &'static str = "Surgical kick drum EQ: low cut, body, room for bass, boxy, click, high cut";
    const DEF: &'static InstrumentDef = &KICK;
}

pub type KickEq = EqPlugin<Kick>;

eqcore::export_eq_plugin!(KickEq);
