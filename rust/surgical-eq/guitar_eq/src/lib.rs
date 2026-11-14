//! Albion Road Audio — Distorted Guitar EQ.
//!
//! Bands follow musicguymixing.com/distorted-guitar-eq: a 100 Hz high-pass, a gentle body
//! boost at 150–200 Hz, a mud cut at 250–350 Hz, warmth at 500 Hz, clarity around 800 Hz,
//! a wide, subtle cut at 2 kHz for pick click and grating fizz, and a high-shelf cut at
//! 4–5 kHz to tame harsh distortion (the guide prefers this to a low-pass). The guide gives
//! no Q or dB amounts; the Qs and zones here are our choices.

use eqcore::{BandDef, BandKind, EqPlugin, Instrument, InstrumentDef, Move};

pub const GUITAR: InstrumentDef = InstrumentDef {
    name: "Distorted Guitar",
    hp_default: 10, // 100 Hz
    lp_default: 9,  // 20 kHz
    bands: &[
        BandDef { name: "Body", hint: Move::Boost, kind: BandKind::AnalogPeak, freq: 175.0, min: 150.0, max: 200.0, q: 1.0 },
        BandDef { name: "Mud", hint: Move::Cut, kind: BandKind::AnalogPeak, freq: 300.0, min: 250.0, max: 350.0, q: 1.2 },
        BandDef { name: "Warmth", hint: Move::Boost, kind: BandKind::AnalogPeak, freq: 500.0, min: 420.0, max: 600.0, q: 1.2 },
        BandDef { name: "Clarity", hint: Move::Boost, kind: BandKind::AnalogPeak, freq: 800.0, min: 700.0, max: 950.0, q: 1.4 },
        BandDef { name: "Pick / fizz", hint: Move::Cut, kind: BandKind::AnalogPeak, freq: 2000.0, min: 1600.0, max: 2500.0, q: 0.7 },
        BandDef { name: "Harsh", hint: Move::Cut, kind: BandKind::HighShelf, freq: 4500.0, min: 4000.0, max: 5000.0, q: 0.8 },
    ],
    ..InstrumentDef::SERIES
};

pub struct Guitar;

impl Instrument for Guitar {
    const NAME: &'static str = "ARA Distorted Guitar EQ";
    const CLAP_ID: &'static str = "com.albionroadaudio.distorted-guitar-eq";
    const VST3_ID: [u8; 16] = *b"AlbionRdDistEQ01";
    const DESCRIPTION: &'static str = "Surgical distorted guitar EQ: low cut, body, mud, warmth, clarity, pick/fizz, harsh shelf, high cut";
    const DEF: &'static InstrumentDef = &GUITAR;
}

pub type GuitarEq = EqPlugin<Guitar>;

eqcore::export_eq_plugin!(GuitarEq);

#[cfg(test)]
mod tests {
    use super::*;
    use eqcore::band_coefs;

    const FS: f64 = 48000.0;

    fn band_db(i: usize, db: f64, f: f64) -> f64 {
        let b = &GUITAR.bands[i];
        band_coefs(b, FS, b.freq as f64, db).db(FS, f)
    }

    #[test]
    fn defaults_match_the_guide() {
        assert_eq!(eqcore::HP_STEPS[GUITAR.hp_default], 100.0);
        assert_eq!(GUITAR.lp_steps_khz[GUITAR.lp_default], 20.0);
    }

    #[test]
    fn harsh_shelf_cuts_the_top_and_spares_the_mids() {
        // -6 dB shelf at 4.5 kHz: close to full cut at 10 kHz, about half at the corner,
        // and leaves the 800 Hz clarity area alone
        assert!(band_db(5, -6.0, 10000.0) < -5.0);
        assert!((band_db(5, -6.0, 4500.0) + 3.0).abs() < 0.5);
        assert!(band_db(5, -6.0, 800.0) > -0.3);
    }

    #[test]
    fn pick_cut_is_wide() {
        // the 2 kHz cut is broad: still at least a third of its depth an octave away
        assert!(band_db(4, -6.0, 1000.0) < -2.0 && band_db(4, -6.0, 4000.0) < -2.0);
    }
}
