//! Albion Road Audio — Acoustic Guitar EQ.
//!
//! Bands follow musicguymixing.com/eq-acoustic-guitar plus two narrow notches: a sharp
//! 48 dB/oct high-pass at 70 Hz, body/mud at 250 Hz (boost for warmth, cut if boomy), a
//! notch near 300 Hz, a wide clarity boost at 3.5 kHz, a notch near 4 kHz, string
//! percussiveness at 10 kHz, and a 48 dB/oct low-pass at 20 kHz. The bells use the analog
//! shape so the 10 kHz boost isn't squeezed near the top of the spectrum.

use eqcore::{BandDef, BandKind, EqPlugin, Instrument, InstrumentDef, Move};

pub const ACOUSTIC: InstrumentDef = InstrumentDef {
    name: "Acoustic",
    hp_default: 7, // 70 Hz
    lp_default: 9, // 20 kHz
    bands: &[
        BandDef { name: "Body / mud", hint: Move::Either, kind: BandKind::AnalogPeak, freq: 250.0, min: 200.0, max: 300.0, q: 1.0 },
        BandDef { name: "Notch", hint: Move::Cut, kind: BandKind::Notch, freq: 300.0, min: 250.0, max: 350.0, q: 16.0 },
        BandDef { name: "Clarity", hint: Move::Boost, kind: BandKind::AnalogPeak, freq: 3500.0, min: 2500.0, max: 4500.0, q: 1.2 },
        BandDef { name: "Notch", hint: Move::Cut, kind: BandKind::Notch, freq: 4000.0, min: 3950.0, max: 4050.0, q: 16.0 },
        BandDef { name: "Percussion", hint: Move::Boost, kind: BandKind::AnalogPeak, freq: 10000.0, min: 8000.0, max: 12000.0, q: 0.9 },
    ],
    hp_sections: 4, // 48 dB/oct
    lp_sections: 4, // 48 dB/oct
    ..InstrumentDef::SERIES
};

pub struct Acoustic;

impl Instrument for Acoustic {
    const NAME: &'static str = "ARA Acoustic EQ";
    const CLAP_ID: &'static str = "com.albionroadaudio.acoustic-eq";
    const VST3_ID: [u8; 16] = *b"AlbionRdAcouEQ01";
    const DESCRIPTION: &'static str = "Surgical acoustic guitar EQ: 48 dB/oct low cut, body/mud, notch, clarity, notch, percussion, 48 dB/oct high cut";
    const DEF: &'static InstrumentDef = &ACOUSTIC;
}

pub type AcousticEq = EqPlugin<Acoustic>;

eqcore::export_eq_plugin!(AcousticEq);

#[cfg(test)]
mod tests {
    use super::*;
    use eqcore::{band_coefs, dsp::{butter_q, Coefs}};

    const FS: f64 = 48000.0;

    /// Response (dB) of band `i` set to `db` at its default frequency.
    fn band_db(i: usize, db: f64, f: f64) -> f64 {
        let b = &ACOUSTIC.bands[i];
        band_coefs(b, FS, b.freq as f64, db).db(FS, f)
    }

    #[test]
    fn notches_are_narrow() {
        // fully cut, the 300 Hz notch is -12 dB at 300 Hz but barely touches 250/350 Hz
        assert!((band_db(1, -12.0, 300.0) + 12.0).abs() < 0.01);
        assert!(band_db(1, -12.0, 250.0) > -1.5 && band_db(1, -12.0, 350.0) > -1.5);
        // and the 4 kHz one leaves 3.5 kHz and 4.5 kHz nearly alone
        assert!((band_db(3, -12.0, 4000.0) + 12.0).abs() < 0.01);
        assert!(band_db(3, -12.0, 3500.0) > -1.5 && band_db(3, -12.0, 4500.0) > -1.5);
    }

    #[test]
    fn cuts_are_48_db_per_octave() {
        let hp = |f: f64| -> f64 { (0..4).map(|k| Coefs::highpass(FS, 70.0, butter_q(4, k)).db(FS, f)).sum() };
        assert!((hp(70.0) + 3.01).abs() < 0.05);
        assert!((hp(17.5) - hp(35.0) + 48.0).abs() < 1.0);
        assert!(hp(140.0) > -0.1, "flat an octave above: {}", hp(140.0));
    }
}
