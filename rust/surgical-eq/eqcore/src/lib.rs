//! Albion Road Audio surgical EQ series.
//!
//! Every plugin in the series shares this crate: a stepped low cut (high-pass), a set of
//! instrument-specific ±6 dB bands, and a stepped high cut (low-pass), each with an on/off
//! switch. A plugin crate only supplies an [`InstrumentDef`] via the [`Instrument`] trait and
//! calls [`export_eq_plugin!`].

#[cfg(target_os = "linux")]
#[doc(hidden)]
pub mod compat;
pub mod analyzer;
pub mod dsp;
mod editor;
pub mod theme;

use dsp::{chunk_coef, Coefs, Smooth, State, BUTTER_Q2, BUTTER_Q4};
use nih_plug::prelude::*;
use nih_plug_egui::EguiState;
use std::marker::PhantomData;
use std::sync::atomic::Ordering;
use std::sync::{Arc, RwLock};

pub use analyzer::Shared;
pub use nih_plug;

/// Low cut knob positions in Hz; index 0 is "off".
pub const HP_STEPS: [f32; 16] = [0.0, 10.0, 20.0, 30.0, 40.0, 50.0, 60.0, 70.0, 80.0, 90.0, 100.0, 110.0, 120.0, 130.0, 140.0, 150.0];
/// High cut knob positions in kHz; index 0 is "off".
pub const LP_STEPS_KHZ: [f32; 12] = [0.0, 12.0, 13.0, 14.0, 15.0, 16.0, 17.0, 18.0, 19.0, 20.0, 21.0, 22.0];
/// Band gain range, ± dB.
pub const GAIN_RANGE: f32 = 6.0;
/// Low cut slope (two Butterworth sections).
pub const HP_SLOPE: &str = "24 dB/oct";
/// High cut slope (one Butterworth section).
pub const LP_SLOPE: &str = "12 dB/oct";

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum BandKind {
    Peak,
    LowShelf,
    HighShelf,
}

/// What the band is normally used for; shown on the GUI as a hint only.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Move {
    Boost,
    Cut,
    Either,
}

#[derive(Clone, Copy, Debug)]
pub struct BandDef {
    pub name: &'static str,
    pub hint: Move,
    pub kind: BandKind,
    /// Default frequency, Hz.
    pub freq: f32,
    /// Frequency range the band can be swept over, Hz.
    pub min: f32,
    pub max: f32,
    pub q: f32,
}

#[derive(Clone, Copy, Debug)]
pub struct InstrumentDef {
    /// Short name, e.g. "Kick".
    pub name: &'static str,
    /// Default low cut position, index into [`HP_STEPS`].
    pub hp_default: usize,
    /// Default high cut position, index into [`LP_STEPS_KHZ`].
    pub lp_default: usize,
    pub bands: &'static [BandDef],
}

/// Implemented by each plugin crate.
pub trait Instrument: 'static + Send + Sync {
    const NAME: &'static str;
    const CLAP_ID: &'static str;
    const VST3_ID: [u8; 16];
    const DESCRIPTION: &'static str;
    const DEF: &'static InstrumentDef;
}

#[derive(Params)]
pub struct BandParams {
    #[id = "gain"]
    pub gain: FloatParam,
    #[id = "freq"]
    pub freq: FloatParam,
    #[id = "on"]
    pub on: BoolParam,
}

impl BandParams {
    fn new(b: &BandDef) -> Self {
        Self {
            gain: FloatParam::new(format!("{} gain", b.name), 0.0, FloatRange::Linear { min: -GAIN_RANGE, max: GAIN_RANGE })
                .with_step_size(0.1)
                .with_value_to_string(Arc::new(|v| fmt_db(v)))
                .with_string_to_value(Arc::new(|s| s.trim().trim_end_matches("dB").trim().parse().ok())),
            freq: FloatParam::new(
                format!("{} freq", b.name),
                b.freq,
                FloatRange::Skewed { min: b.min, max: b.max, factor: FloatRange::skew_factor(-1.0) },
            )
            .with_value_to_string(Arc::new(|v| fmt_hz(v)))
            .with_string_to_value(formatters::s2v_f32_hz_then_khz()),
            on: BoolParam::new(format!("{} on", b.name), true),
        }
    }
}

#[derive(Params)]
pub struct EqParams {
    #[persist = "editor-state"]
    pub editor_state: Arc<EguiState>,
    /// Spectrum analyser shown behind the curve.
    #[persist = "spectrum"]
    pub spectrum: RwLock<bool>,

    #[id = "hp"]
    pub hp: IntParam,
    #[id = "hp_on"]
    pub hp_on: BoolParam,
    #[nested(array, group = "Band")]
    pub bands: Vec<BandParams>,
    #[id = "lp"]
    pub lp: IntParam,
    #[id = "lp_on"]
    pub lp_on: BoolParam,

    pub def: &'static InstrumentDef,
}

impl EqParams {
    pub fn new(def: &'static InstrumentDef) -> Self {
        let (w, h) = editor::size(def);
        Self {
            editor_state: EguiState::from_size(w, h),
            spectrum: RwLock::new(true),
            hp: IntParam::new("Low cut", def.hp_default as i32, IntRange::Linear { min: 0, max: HP_STEPS.len() as i32 - 1 })
                .with_value_to_string(Arc::new(|v| hp_label(v as usize)))
                .with_string_to_value(Arc::new(|s| parse_step(s, &HP_STEPS))),
            hp_on: BoolParam::new("Low cut on", true),
            bands: def.bands.iter().map(BandParams::new).collect(),
            lp: IntParam::new("High cut", def.lp_default as i32, IntRange::Linear { min: 0, max: LP_STEPS_KHZ.len() as i32 - 1 })
                .with_value_to_string(Arc::new(|v| lp_label(v as usize)))
                .with_string_to_value(Arc::new(|s| parse_step(s, &LP_STEPS_KHZ))),
            lp_on: BoolParam::new("High cut on", true),
            def,
        }
    }

    /// Low cut frequency in Hz, if active.
    pub fn hp_hz(&self) -> Option<f64> {
        let i = (self.hp.value().max(0) as usize).min(HP_STEPS.len() - 1);
        (self.hp_on.value() && i > 0).then(|| HP_STEPS[i] as f64)
    }

    /// High cut frequency in Hz, if active.
    pub fn lp_hz(&self) -> Option<f64> {
        let i = (self.lp.value().max(0) as usize).min(LP_STEPS_KHZ.len() - 1);
        (self.lp_on.value() && i > 0).then(|| LP_STEPS_KHZ[i] as f64 * 1000.0)
    }

    /// Effective (gain dB, freq Hz) of band `i`.
    pub fn band(&self, i: usize) -> (f64, f64) {
        let b = &self.bands[i];
        (if b.on.value() { b.gain.value() as f64 } else { 0.0 }, b.freq.value() as f64)
    }
}

pub fn band_coefs(def: &BandDef, fs: f64, freq: f64, db: f64) -> Coefs {
    let q = def.q as f64;
    match def.kind {
        BandKind::Peak => Coefs::peak(fs, freq, q, db),
        BandKind::LowShelf => Coefs::low_shelf(fs, freq, q, db),
        BandKind::HighShelf => Coefs::high_shelf(fs, freq, q, db),
    }
}

/// Highest usable cutoff for the high cut at this sample rate.
pub fn lp_limit(fs: f64) -> f64 {
    fs * 0.45
}

pub fn fmt_db(v: f32) -> String {
    if v.abs() < 0.05 {
        "0.0 dB".into()
    } else {
        format!("{v:+.1} dB")
    }
}

pub fn fmt_hz(v: f32) -> String {
    if v >= 1000.0 {
        let k = v / 1000.0;
        if (k - k.round()).abs() < 0.005 { format!("{k:.0} kHz") } else { format!("{k:.2} kHz") }
    } else {
        format!("{v:.0} Hz")
    }
}

pub fn hp_label(i: usize) -> String {
    match HP_STEPS.get(i) {
        Some(&f) if f > 0.0 => format!("{f:.0} Hz"),
        _ => "Off".into(),
    }
}

pub fn lp_label(i: usize) -> String {
    match LP_STEPS_KHZ.get(i) {
        Some(&f) if f > 0.0 => format!("{f:.0} kHz"),
        _ => "Off".into(),
    }
}

fn parse_step(s: &str, steps: &[f32]) -> Option<i32> {
    let s = s.trim().to_lowercase();
    if s == "off" {
        return Some(0);
    }
    let num: f32 = s.trim_end_matches("khz").trim_end_matches("hz").trim().parse().ok()?;
    steps
        .iter()
        .enumerate()
        .skip(1)
        .min_by(|a, b| (a.1 - num).abs().total_cmp(&(b.1 - num).abs()))
        .map(|(i, _)| i as i32)
}

const MAX_CH: usize = 2;
const MAX_BANDS: usize = 8;
const CHUNK: usize = 32;
/// Parameter smoothing time constant, seconds.
const TAU: f64 = 0.015;

#[derive(Default, Clone, Copy)]
struct BandState {
    gain: Smooth,
    log_freq: Smooth,
    coefs: Coefs,
    st: [State; MAX_CH],
}

#[derive(Default)]
struct CutState {
    mix: Smooth,
    log_freq: Smooth,
    coefs: [Coefs; 2],
    st: [[State; 2]; MAX_CH],
}

pub struct EqPlugin<I: Instrument> {
    params: Arc<EqParams>,
    shared: Arc<Shared>,
    fs: f64,
    primed: bool,
    hp: CutState,
    lp: CutState,
    bands: [BandState; MAX_BANDS],
    _inst: PhantomData<fn() -> I>,
}

impl<I: Instrument> Default for EqPlugin<I> {
    fn default() -> Self {
        assert!(I::DEF.bands.len() <= MAX_BANDS);
        Self {
            params: Arc::new(EqParams::new(I::DEF)),
            shared: Arc::new(Shared::default()),
            fs: 48000.0,
            primed: false,
            hp: CutState::default(),
            lp: CutState::default(),
            bands: [BandState::default(); MAX_BANDS],
            _inst: PhantomData,
        }
    }
}

impl<I: Instrument> EqPlugin<I> {
    /// Move every smoother one chunk towards its parameter and refresh coefficients.
    fn update(&mut self, len: usize) {
        let p = &self.params;
        let fs = self.fs;
        let k = if self.primed { chunk_coef(len, fs, TAU) } else { 1.0 };

        // Low cut: crossfade in/out so switching it never clicks; keep the last frequency while off.
        let hp = p.hp_hz();
        let hp_f = hp.unwrap_or_else(|| self.hp.log_freq.value.exp().max(HP_STEPS[1] as f64));
        let f = self.hp.log_freq.step(hp_f.ln(), k).exp();
        self.hp.mix.step(hp.is_some() as u8 as f64, k);
        self.hp.coefs = [Coefs::highpass(fs, f, BUTTER_Q4[0]), Coefs::highpass(fs, f, BUTTER_Q4[1])];

        let lp = p.lp_hz().map(|f| f.min(lp_limit(fs)));
        let lp_f = lp.unwrap_or_else(|| self.lp.log_freq.value.exp().clamp(1000.0, lp_limit(fs)));
        let f = self.lp.log_freq.step(lp_f.ln(), k).exp();
        self.lp.mix.step(lp.is_some() as u8 as f64, k);
        self.lp.coefs[0] = Coefs::lowpass(fs, f, BUTTER_Q2);

        for (i, def) in p.def.bands.iter().enumerate() {
            let (db, freq) = p.band(i);
            let b = &mut self.bands[i];
            let g = b.gain.step(db, k);
            let f = b.log_freq.step(freq.ln(), k).exp();
            b.coefs = band_coefs(def, fs, f, g);
        }
        self.primed = true;
    }
}

impl<I: Instrument> Plugin for EqPlugin<I> {
    const NAME: &'static str = I::NAME;
    const VENDOR: &'static str = "Albion Road Audio";
    const URL: &'static str = "https://www.albionroadaudio.co.uk";
    const EMAIL: &'static str = "";
    const VERSION: &'static str = env!("CARGO_PKG_VERSION");

    const AUDIO_IO_LAYOUTS: &'static [AudioIOLayout] = &[
        AudioIOLayout {
            main_input_channels: NonZeroU32::new(2),
            main_output_channels: NonZeroU32::new(2),
            ..AudioIOLayout::const_default()
        },
        AudioIOLayout {
            main_input_channels: NonZeroU32::new(1),
            main_output_channels: NonZeroU32::new(1),
            ..AudioIOLayout::const_default()
        },
    ];
    const SAMPLE_ACCURATE_AUTOMATION: bool = false;

    type SysExMessage = ();
    type BackgroundTask = ();

    fn params(&self) -> Arc<dyn Params> {
        self.params.clone()
    }

    fn editor(&mut self, _async_executor: AsyncExecutor<Self>) -> Option<Box<dyn Editor>> {
        editor::create(self.params.clone(), self.shared.clone())
    }

    fn initialize(&mut self, _: &AudioIOLayout, cfg: &BufferConfig, _: &mut impl InitContext<Self>) -> bool {
        self.fs = cfg.sample_rate as f64;
        self.shared.sample_rate.store(self.fs.to_bits(), Ordering::Relaxed);
        self.reset();
        true
    }

    fn reset(&mut self) {
        self.primed = false;
        self.hp = CutState::default();
        self.lp = CutState::default();
        self.bands = [BandState::default(); MAX_BANDS];
    }

    fn process(&mut self, buffer: &mut Buffer, _: &mut AuxiliaryBuffers, _: &mut impl ProcessContext<Self>) -> ProcessStatus {
        let n = buffer.samples();
        let nb = self.params.def.bands.len();
        let chans = buffer.as_slice();
        let mut cursor = self.shared.cursor();
        let mut start = 0;
        while start < n {
            let len = (n - start).min(CHUNK);
            self.update(len);
            let (hm, lm) = (self.hp.mix.value, self.lp.mix.value);
            let nch = chans.len().min(MAX_CH);
            let inv = 1.0 / nch.max(1) as f32;
            for i in start..start + len {
                let (mut pre, mut post) = (0.0, 0.0);
                for (ch, data) in chans.iter_mut().enumerate().take(MAX_CH) {
                    let x = data[i] as f64;
                    let h = self.hp.st[ch][0].tick(&self.hp.coefs[0], x);
                    let h = self.hp.st[ch][1].tick(&self.hp.coefs[1], h);
                    let mut y = x + hm * (h - x);
                    for b in self.bands[..nb].iter_mut() {
                        y = b.st[ch].tick(&b.coefs, y);
                    }
                    let l = self.lp.st[ch][0].tick(&self.lp.coefs[0], y);
                    y += lm * (l - y);
                    pre += data[i];
                    data[i] = y as f32;
                    post += data[i];
                }
                self.shared.write(cursor, pre * inv, post * inv);
                cursor = cursor.wrapping_add(1);
            }
            start += len;
        }
        self.shared.commit(cursor);
        ProcessStatus::Normal
    }
}

impl<I: Instrument> ClapPlugin for EqPlugin<I> {
    const CLAP_ID: &'static str = I::CLAP_ID;
    const CLAP_DESCRIPTION: Option<&'static str> = Some(I::DESCRIPTION);
    const CLAP_MANUAL_URL: Option<&'static str> = None;
    const CLAP_SUPPORT_URL: Option<&'static str> = None;
    const CLAP_FEATURES: &'static [ClapFeature] = &[ClapFeature::AudioEffect, ClapFeature::Equalizer, ClapFeature::Stereo, ClapFeature::Mono];
}

impl<I: Instrument> Vst3Plugin for EqPlugin<I> {
    const VST3_CLASS_ID: [u8; 16] = I::VST3_ID;
    const VST3_SUBCATEGORIES: &'static [Vst3SubCategory] = &[Vst3SubCategory::Fx, Vst3SubCategory::Eq];
}

/// Export a plugin type as CLAP and VST3. The calling crate also needs the
/// two-line `build.rs` that wraps acosf/atan2f (see kick_eq/build.rs).
#[macro_export]
macro_rules! export_eq_plugin {
    ($ty:ty) => {
        $crate::nih_plug::nih_export_clap!($ty);
        $crate::nih_plug::nih_export_vst3!($ty);
        #[cfg(target_os = "linux")]
        pub use $crate::compat::*;
    };
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn step_labels_and_parsing() {
        assert_eq!(hp_label(0), "Off");
        assert_eq!(hp_label(2), "20 Hz");
        assert_eq!(lp_label(4), "15 kHz");
        assert_eq!(parse_step("80 Hz", &HP_STEPS), Some(8));
        assert_eq!(parse_step("off", &LP_STEPS_KHZ), Some(0));
        assert_eq!(parse_step("21.6", &LP_STEPS_KHZ), Some(11));
    }

    #[test]
    fn formatting() {
        assert_eq!(fmt_db(0.01), "0.0 dB");
        assert_eq!(fmt_db(-3.0), "-3.0 dB");
        assert_eq!(fmt_hz(3500.0), "3.50 kHz");
        assert_eq!(fmt_hz(15000.0), "15 kHz");
        assert_eq!(fmt_hz(62.4), "62 Hz");
    }
}
