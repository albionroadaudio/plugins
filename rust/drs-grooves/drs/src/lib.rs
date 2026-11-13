//! Albion Road Audio — DRS Grooves.
//! Rock, indie, grunge, punk and metal grooves written for the DrumGizmo DRS Kit.

#[cfg(target_os = "linux")]
mod compat;
mod editor;
mod theme;
pub mod kit;

use drumlib::sequencer::{Block, Event, Sequencer, Settings};
use drumlib::Library;
use nih_plug::prelude::*;
use nih_plug_egui::EguiState;
use std::sync::atomic::{AtomicBool, AtomicU64, AtomicUsize, Ordering};
use std::sync::{Arc, OnceLock, RwLock};

pub const SOURCES: &[(&str, &str)] = &[
    ("rock.txt", include_str!("../patterns/rock.txt")),
    ("indie.txt", include_str!("../patterns/indie.txt")),
    ("grunge.txt", include_str!("../patterns/grunge.txt")),
    ("punk.txt", include_str!("../patterns/punk.txt")),
    ("metal.txt", include_str!("../patterns/metal.txt")),
];

/// The DRS pattern library, parsed once.
pub fn library() -> &'static Library {
    static LIB: OnceLock<Library> = OnceLock::new();
    LIB.get_or_init(|| {
        Library::from_sources_with(SOURCES, kit::DRS_KIT).unwrap_or_else(|e| panic!("DRS pattern error: {e}"))
    })
}

#[derive(Enum, Debug, PartialEq, Eq, Clone, Copy)]
pub enum FillEvery {
    #[name = "Off"]
    Off,
    #[name = "Every 2 bars"]
    Two,
    #[name = "Every 4 bars"]
    Four,
    #[name = "Every 8 bars"]
    Eight,
    #[name = "Every 16 bars"]
    Sixteen,
}

impl FillEvery {
    pub fn bars(self) -> i64 {
        match self {
            FillEvery::Off => 0,
            FillEvery::Two => 2,
            FillEvery::Four => 4,
            FillEvery::Eight => 8,
            FillEvery::Sixteen => 16,
        }
    }
}

#[derive(Params)]
pub struct DrsParams {
    #[persist = "editor-state"]
    pub editor_state: Arc<EguiState>,
    /// MIDI export length in bars (1, 2, 4 or 8).
    #[persist = "export-bars"]
    pub export_bars: RwLock<u32>,
    /// MIDI export: replace the last bar with a fill.
    #[persist = "export-fill"]
    pub export_fill: RwLock<bool>,

    #[id = "pattern"]
    pub pattern: IntParam,
    #[id = "fill"]
    pub fill_every: EnumParam<FillEvery>,
    #[id = "crash"]
    pub crash_after_fill: BoolParam,
    #[id = "swing"]
    pub swing: FloatParam,
    #[id = "htime"]
    pub humanize_time: FloatParam,
    #[id = "hvel"]
    pub humanize_vel: FloatParam,
    #[id = "vel"]
    pub velocity: FloatParam,
    #[id = "chan"]
    pub channel: IntParam,
}

impl Default for DrsParams {
    fn default() -> Self {
        let lib: &'static Library = library();
        let n = lib.grooves.len() as i32;
        Self {
            editor_state: EguiState::from_size(editor::WIDTH, editor::HEIGHT),
            export_bars: RwLock::new(4),
            export_fill: RwLock::new(true),
            pattern: IntParam::new("Pattern", 0, IntRange::Linear { min: 0, max: n - 1 })
                .with_value_to_string(Arc::new(move |v| {
                    let p = &lib.patterns[lib.grooves[v.clamp(0, n - 1) as usize]];
                    format!("{} ({} BPM)", p.label(), p.bpm)
                }))
                .with_string_to_value(Arc::new(move |s| {
                    let s = s.trim().to_lowercase();
                    if let Ok(num) = s.parse::<i32>() {
                        return Some(num.clamp(0, n - 1));
                    }
                    lib.grooves
                        .iter()
                        .position(|&i| lib.patterns[i].label().to_lowercase().contains(&s))
                        .map(|p| p as i32)
                })),
            fill_every: EnumParam::new("Fill", FillEvery::Four),
            crash_after_fill: BoolParam::new("Crash after fill", true),
            swing: FloatParam::new("Extra swing", 0.0, FloatRange::Linear { min: 0.0, max: 100.0 })
                .with_unit(" %")
                .with_step_size(1.0),
            humanize_time: FloatParam::new("Humanize timing", 4.0, FloatRange::Linear { min: 0.0, max: 30.0 })
                .with_unit(" ms")
                .with_step_size(0.5),
            humanize_vel: FloatParam::new("Humanize velocity", 8.0, FloatRange::Linear { min: 0.0, max: 50.0 })
                .with_unit(" %")
                .with_step_size(1.0),
            velocity: FloatParam::new("Velocity", 100.0, FloatRange::Linear { min: 10.0, max: 150.0 })
                .with_unit(" %")
                .with_step_size(1.0),
            channel: IntParam::new("MIDI channel", 10, IntRange::Linear { min: 1, max: 16 }),
        }
    }
}

/// Written by the audio thread, read by the GUI for the playhead.
#[derive(Default)]
pub struct Shared {
    pub playing: AtomicBool,
    pub pattern: AtomicUsize,
    /// f64 bits: position in beats from the start of `pattern`.
    pub beat: AtomicU64,
}

pub struct DrsGrooves {
    params: Arc<DrsParams>,
    shared: Arc<Shared>,
    seq: Sequencer,
    events: Vec<Event>,
}

impl Default for DrsGrooves {
    fn default() -> Self {
        let _ = library();
        Self {
            params: Arc::new(DrsParams::default()),
            shared: Arc::new(Shared::default()),
            seq: Sequencer::default(),
            events: Vec::with_capacity(2048),
        }
    }
}

impl Plugin for DrsGrooves {
    const NAME: &'static str = "ARA DRS Grooves";
    const VENDOR: &'static str = "Albion Road Audio";
    const URL: &'static str = "https://www.albionroadaudio.co.uk";
    const EMAIL: &'static str = "";
    const VERSION: &'static str = env!("CARGO_PKG_VERSION");

    const AUDIO_IO_LAYOUTS: &'static [AudioIOLayout] = &[AudioIOLayout {
        main_input_channels: None,
        main_output_channels: None,
        ..AudioIOLayout::const_default()
    }];
    const MIDI_INPUT: MidiConfig = MidiConfig::None;
    const MIDI_OUTPUT: MidiConfig = MidiConfig::Basic;
    const SAMPLE_ACCURATE_AUTOMATION: bool = false;

    type SysExMessage = ();
    type BackgroundTask = ();

    fn params(&self) -> Arc<dyn Params> {
        self.params.clone()
    }

    fn editor(&mut self, _async_executor: AsyncExecutor<Self>) -> Option<Box<dyn Editor>> {
        editor::create(self.params.clone(), self.shared.clone())
    }

    fn initialize(&mut self, _: &AudioIOLayout, _: &BufferConfig, _: &mut impl InitContext<Self>) -> bool {
        true
    }

    fn reset(&mut self) {
        self.seq.reset();
        self.events.clear();
    }

    fn process(
        &mut self,
        buffer: &mut Buffer,
        _aux: &mut AuxiliaryBuffers,
        context: &mut impl ProcessContext<Self>,
    ) -> ProcessStatus {
        let lib = library();
        let t = context.transport();
        let block = Block {
            playing: t.playing,
            pos_beats: t.pos_beats(),
            tempo: t.tempo.unwrap_or(120.0),
            sample_rate: t.sample_rate as f64,
            samples: buffer.samples(),
        };
        let p = &self.params;
        let groove = lib.grooves[(p.pattern.value().max(0) as usize).min(lib.grooves.len() - 1)];
        let settings = Settings {
            groove,
            fill_every: p.fill_every.value().bars(),
            crash_notes: if p.crash_after_fill.value() { kit::CRASH_AFTER_FILL } else { &[] },
            crash_velocity: 112,
            extra_swing: p.swing.value() as f64 / 100.0,
            humanize_ms: p.humanize_time.value() as f64,
            humanize_vel: p.humanize_vel.value() as f64 / 100.0,
            vel_scale: p.velocity.value() as f64 / 100.0,
            channel: (p.channel.value() - 1).clamp(0, 15) as u8,
        };
        let ph = self.seq.process(lib, block, &settings, &mut self.events);

        for e in self.events.iter() {
            context.send_event(if e.on {
                NoteEvent::NoteOn { timing: e.timing, voice_id: None, channel: e.channel, note: e.note, velocity: e.vel }
            } else {
                NoteEvent::NoteOff { timing: e.timing, voice_id: None, channel: e.channel, note: e.note, velocity: 0.0 }
            });
        }

        let pat = &lib.patterns[ph.pattern];
        let beat = ph.bar_in_pattern as f64 * pat.beats_per_bar() + ph.beat_in_bar;
        self.shared.playing.store(ph.playing, Ordering::Relaxed);
        self.shared.pattern.store(ph.pattern, Ordering::Relaxed);
        self.shared.beat.store(beat.to_bits(), Ordering::Relaxed);
        ProcessStatus::Normal
    }
}

impl ClapPlugin for DrsGrooves {
    const CLAP_ID: &'static str = "com.albionroadaudio.drs-grooves";
    const CLAP_DESCRIPTION: Option<&'static str> = Some("Rock, indie, grunge, punk and metal grooves for the DrumGizmo DRS Kit");
    const CLAP_MANUAL_URL: Option<&'static str> = None;
    const CLAP_SUPPORT_URL: Option<&'static str> = None;
    const CLAP_FEATURES: &'static [ClapFeature] = &[ClapFeature::NoteEffect, ClapFeature::Utility];
}

impl Vst3Plugin for DrsGrooves {
    const VST3_CLASS_ID: [u8; 16] = *b"AlbionRdDRSGroov";
    const VST3_SUBCATEGORIES: &'static [Vst3SubCategory] = &[Vst3SubCategory::Instrument, Vst3SubCategory::Tools];
}

nih_export_clap!(DrsGrooves);
nih_export_vst3!(DrsGrooves);

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn drs_library_parses() {
        let lib = library();
        assert_eq!(lib.genres, ["Rock", "Indie", "Grunge", "Punk", "Metal"]);
        assert!(lib.grooves.len() >= 60, "grooves: {}", lib.grooves.len());
        for p in &lib.patterns {
            for h in &p.hits {
                assert!(kit::GRID_ORDER.contains(&h.note), "{}: note {} not in grid", p.label(), h.note);
            }
        }
        for &g in &lib.grooves {
            let p = &lib.patterns[g];
            assert!(!lib.fills_for[g].is_empty(), "{} has no compatible fill", p.label());
        }
    }
}
