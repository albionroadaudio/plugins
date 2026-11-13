//! Host-independent pattern sequencer. A plugin feeds it the transport state
//! for each audio block and gets back sample-accurate note events. Keeping it
//! free of any plugin framework means its timing can be unit-tested.
//!
//! `process` never allocates as long as the caller's event buffer has spare
//! capacity (events beyond capacity are dropped).

use crate::Library;

pub const MAX_PENDING: usize = 512;
/// Note length in beats; drum samplers only care about the note-on.
pub const GATE_BEATS: f64 = 1.0 / 32.0;

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Event {
    /// Sample offset inside the block.
    pub timing: u32,
    pub on: bool,
    pub channel: u8,
    pub note: u8,
    /// 0.0-1.0
    pub vel: f32,
}

/// Transport state for one audio block.
#[derive(Debug, Clone, Copy)]
pub struct Block {
    pub playing: bool,
    pub pos_beats: Option<f64>,
    pub tempo: f64,
    pub sample_rate: f64,
    pub samples: usize,
}

#[derive(Debug, Clone)]
pub struct Settings<'a> {
    /// Index into `Library::patterns` of the groove to play.
    pub groove: usize,
    /// Play a fill every N bars (0 = never).
    pub fill_every: i64,
    /// Crash notes for the downbeat after a fill, used in turn (empty = none).
    pub crash_notes: &'a [u8],
    pub crash_velocity: u8,
    /// Added to the pattern's own swing, 0.0-1.0.
    pub extra_swing: f64,
    pub humanize_ms: f64,
    /// 0.0-1.0
    pub humanize_vel: f64,
    pub vel_scale: f64,
    pub channel: u8,
}

/// What is sounding right now, for drawing a playhead.
#[derive(Debug, Clone, Copy, Default, PartialEq)]
pub struct Playhead {
    pub playing: bool,
    /// Pattern index currently sounding (a groove, or a fill on fill bars).
    pub pattern: usize,
    pub bar_in_pattern: u32,
    pub beat_in_bar: f64,
}

#[derive(Clone, Copy)]
struct Pending {
    off_beat: f64,
    channel: u8,
    note: u8,
}

pub struct Sequencer {
    current: Option<usize>,
    last_bar: Option<i64>,
    last_end: Option<f64>,
    pending: Vec<Pending>,
}

impl Default for Sequencer {
    fn default() -> Self {
        Sequencer { current: None, last_bar: None, last_end: None, pending: Vec::with_capacity(MAX_PENDING) }
    }
}

/// Deterministic noise in [-1, 1] so humanizing is stable for a given bar/hit.
fn noise(seed: u64) -> f64 {
    let mut z = seed.wrapping_add(0x9E37_79B9_7F4A_7C15);
    z = (z ^ (z >> 30)).wrapping_mul(0xBF58_476D_1CE4_E5B9);
    z = (z ^ (z >> 27)).wrapping_mul(0x94D0_49BB_1331_11EB);
    z ^= z >> 31;
    (z >> 11) as f64 / (1u64 << 52) as f64 - 1.0
}

fn push(out: &mut Vec<Event>, e: Event) {
    if out.len() < out.capacity() {
        out.push(e);
    }
}

impl Sequencer {
    pub fn reset(&mut self) {
        self.current = None;
        self.last_bar = None;
        self.last_end = None;
        self.pending.clear();
    }

    fn flush(&mut self, out: &mut Vec<Event>, timing: u32) {
        for p in self.pending.iter() {
            push(out, Event { timing, on: false, channel: p.channel, note: p.note, vel: 0.0 });
        }
        self.pending.clear();
    }

    fn add_note(&mut self, out: &mut Vec<Event>, timing: u32, beat: f64, channel: u8, note: u8, vel: f32) {
        push(out, Event { timing, on: true, channel, note, vel });
        if self.pending.len() < MAX_PENDING {
            self.pending.push(Pending { off_beat: beat + GATE_BEATS, channel, note });
        }
    }

    /// Generate this block's events into `out` (cleared first), sorted by
    /// timing with note-offs before note-ons at the same sample.
    pub fn process(&mut self, lib: &Library, block: Block, s: &Settings, out: &mut Vec<Event>) -> Playhead {
        out.clear();
        let n = block.samples;
        let start = match (block.playing, block.pos_beats, n) {
            (true, Some(p), 1..) => p,
            _ => {
                self.flush(out, 0);
                self.last_bar = None;
                self.last_end = None;
                return Playhead { playing: false, pattern: self.current.unwrap_or(s.groove), ..Default::default() };
            }
        };
        let tempo = block.tempo.max(1.0);
        let beats_per_sample = tempo / 60.0 / block.sample_rate.max(1.0);
        let end = start + n as f64 * beats_per_sample;

        // Transport jumped (loop, seek): release anything still sounding.
        if let Some(le) = self.last_end {
            if (start - le).abs() > 1e-4 {
                self.flush(out, 0);
                self.last_bar = None;
            }
        }
        self.last_end = Some(end);

        let wanted = s.groove.min(lib.patterns.len() - 1);
        // Pattern changes take effect at the next bar line.
        let cur = *self.current.get_or_insert(wanted);
        let bar_now = (start / lib.patterns[cur].beats_per_bar()).floor() as i64;
        let cur = if self.last_bar != Some(bar_now) { wanted } else { cur };
        self.current = Some(cur);
        self.last_bar = Some(bar_now);
        let bpb = lib.patterns[cur].beats_per_bar();
        let bar_now = (start / bpb).floor() as i64;

        let to_timing = |beat: f64| (((beat - start) / beats_per_sample + 1e-6).floor() as i64).clamp(0, n as i64 - 1) as u32;

        // Note-offs due in this block.
        let mut i = 0;
        while i < self.pending.len() {
            let p = self.pending[i];
            if p.off_beat < end {
                push(out, Event { timing: to_timing(p.off_beat), on: false, channel: p.channel, note: p.note, vel: 0.0 });
                self.pending.swap_remove(i);
            } else {
                i += 1;
            }
        }

        let jitter_beats = s.humanize_ms / 1000.0 * tempo / 60.0;
        let fill_n = s.fill_every.max(0);
        let source_for = |b: i64| -> (usize, u32, bool) {
            let groove = if b > bar_now { wanted } else { cur };
            let fills = &lib.fills_for[groove];
            if fill_n > 0 && !fills.is_empty() && b % fill_n == fill_n - 1 {
                (fills[((b / fill_n) as usize) % fills.len()], 0, true)
            } else {
                let g = &lib.patterns[groove];
                (groove, (b as u64 % g.bars as u64) as u32, false)
            }
        };

        let first_bar = (bar_now - 1).max(0);
        let last_bar = (end / bpb).floor() as i64 + 1;
        for b in first_bar..=last_bar {
            let (src_idx, sbar, _) = source_for(b);
            let src = &lib.patterns[src_idx];
            let swing = (src.swing as f64 + s.extra_swing).min(1.0);
            let bar_start = b as f64 * bpb;

            // Crash on the downbeat after a fill.
            if !s.crash_notes.is_empty() && fill_n > 0 && b > 0 && b % fill_n == 0 && bar_start >= start && bar_start < end {
                let groove = if b > bar_now { wanted } else { cur };
                if !lib.fills_for[groove].is_empty() {
                    let note = s.crash_notes[((b / fill_n - 1) as usize) % s.crash_notes.len()];
                    let vel = (s.crash_velocity as f64 * s.vel_scale).clamp(1.0, 127.0) / 127.0;
                    self.add_note(out, to_timing(bar_start), bar_start, s.channel, note, vel as f32);
                }
            }

            let sbar_start = sbar as f64 * src.beats_per_bar();
            for (hi, h) in src.bar_hits(sbar).iter().enumerate() {
                let seed = ((b as u64) << 20) ^ ((hi as u64) << 8) ^ h.note as u64;
                let mut beat = bar_start + (h.beat - sbar_start) + swing * h.swing_unit;
                if jitter_beats > 0.0 {
                    beat += noise(seed) * jitter_beats;
                }
                if beat < start || beat >= end {
                    continue;
                }
                let mut v = h.vel as f64 * s.vel_scale;
                if s.humanize_vel > 0.0 {
                    v *= 1.0 + noise(seed ^ 0xABCDEF) * s.humanize_vel;
                }
                let vel = (v.clamp(1.0, 127.0) / 127.0) as f32;
                self.add_note(out, to_timing(beat), beat, s.channel, h.note, vel);
            }
        }

        out.sort_unstable_by(|a, b| a.timing.cmp(&b.timing).then(a.on.cmp(&b.on)));

        let (pidx, sbar, _) = source_for(bar_now);
        Playhead { playing: true, pattern: pidx, bar_in_pattern: sbar, beat_in_bar: start - bar_now as f64 * bpb }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const SRC: &str = "genre: T\n\
        groove: A\n CH x-x- x-x- x-x- x-x-\n SN ---- X--- ---- X---\n BD x--- ---- x--- ----\n\
        groove: B\n BD x--- x--- x--- x---\n\
        fill: F\n SN xxxx xxxx xxxx xxxx\n";

    fn lib() -> Library {
        Library::from_sources(&[("t", SRC)]).unwrap()
    }

    fn settings(groove: usize, fill_every: i64) -> Settings<'static> {
        Settings {
            groove,
            fill_every,
            crash_notes: &[],
            crash_velocity: 110,
            extra_swing: 0.0,
            humanize_ms: 0.0,
            humanize_vel: 0.0,
            vel_scale: 1.0,
            channel: 9,
        }
    }

    /// Run the transport from beat 0 for `beats` beats; return (absolute sample, note) of every note-on.
    fn run(seq: &mut Sequencer, lib: &Library, s: &Settings, beats: f64, block: usize) -> Vec<(u64, u8)> {
        let (sr, tempo) = (48_000.0, 120.0);
        let total = (beats * 60.0 / tempo * sr) as u64;
        let mut out = Vec::with_capacity(1024);
        let mut ons = Vec::new();
        let mut pos = 0u64;
        while pos < total {
            let blk = Block { playing: true, pos_beats: Some(pos as f64 / sr * tempo / 60.0), tempo, sample_rate: sr, samples: block };
            seq.process(lib, blk, s, &mut out);
            ons.extend(out.iter().filter(|e| e.on).map(|e| (pos + e.timing as u64, e.note)).filter(|(t, _)| *t < total));
            pos += block as u64;
        }
        ons
    }

    #[test]
    fn plays_every_hit_once_at_the_right_sample() {
        let lib = lib();
        for block in [64, 333, 512, 4096] {
            let ons = run(&mut Sequencer::default(), &lib, &settings(lib.grooves[0], 0), 8.0, block);
            assert_eq!(ons.len(), 2 * 12, "block size {block}");
            // snare on beat 2 = 0.5 s = sample 24000
            assert!(ons.contains(&(24_000, 38)), "block size {block}: {ons:?}");
        }
    }

    #[test]
    fn fill_replaces_last_bar() {
        let lib = lib();
        let ons = run(&mut Sequencer::default(), &lib, &settings(lib.grooves[0], 2), 8.0, 512);
        // bar 0: groove (12 hits), bar 1: fill (16 snare hits)
        assert_eq!(ons.len(), 12 + 16);
        assert!(ons.iter().filter(|(s, _)| *s >= 96_000).all(|(_, n)| *n == 38));
    }

    #[test]
    fn crash_after_fill() {
        let lib = lib();
        let mut s = settings(lib.grooves[0], 2);
        s.crash_notes = &[49, 57];
        let ons = run(&mut Sequencer::default(), &lib, &s, 12.0, 512);
        assert!(ons.contains(&(192_000, 49)), "crash on bar 2 downbeat");
    }

    #[test]
    fn pattern_change_waits_for_bar_line() {
        let lib = lib();
        let mut seq = Sequencer::default();
        let (sr, tempo, block) = (48_000.0, 120.0, 480usize);
        let mut out = Vec::with_capacity(1024);
        let mut ons = Vec::new();
        for k in 0..400u64 {
            let pos = k * block as u64;
            let groove = if pos >= 48_000 { lib.grooves[1] } else { lib.grooves[0] }; // switch at beat 2
            let blk = Block { playing: true, pos_beats: Some(pos as f64 / sr * tempo / 60.0), tempo, sample_rate: sr, samples: block };
            seq.process(&lib, blk, &settings(groove, 0), &mut out);
            ons.extend(out.iter().filter(|e| e.on).map(|e| (pos + e.timing as u64, e.note)));
        }
        // beat 3 of bar 0 still plays groove A's hat; bar 1 is groove B (kicks only)
        assert!(ons.contains(&(48_000, 42)));
        assert!(ons.iter().filter(|(s, _)| *s >= 96_000).all(|(_, n)| *n == 36));
    }

    #[test]
    fn stop_releases_notes() {
        let lib = lib();
        let mut seq = Sequencer::default();
        let mut out = Vec::with_capacity(64);
        let s = settings(lib.grooves[0], 0);
        seq.process(&lib, Block { playing: true, pos_beats: Some(0.0), tempo: 120.0, sample_rate: 48_000.0, samples: 64 }, &s, &mut out);
        assert!(out.iter().any(|e| e.on));
        seq.process(&lib, Block { playing: false, pos_beats: Some(0.0), tempo: 120.0, sample_rate: 48_000.0, samples: 64 }, &s, &mut out);
        assert!(!out.is_empty() && out.iter().all(|e| !e.on));
    }
}
