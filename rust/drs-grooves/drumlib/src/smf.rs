//! Minimal Standard MIDI File (type 0) writer for drum patterns.

use crate::{Library, Pattern, CRASH_NOTE};

pub const PPQ: u16 = 480;

#[derive(Debug, Clone)]
pub struct ExportOptions {
    /// Total bars to write; the pattern loops to fill them. 0 = pattern length.
    pub bars: u32,
    /// Replace the last bar with one of the genre's fills.
    pub fill: bool,
    /// Extra swing on top of the pattern's own, 0.0-1.0.
    pub extra_swing: f32,
    /// MIDI channel, 0-based (9 = GM drums).
    pub channel: u8,
    /// Note length in ticks.
    pub gate: u32,
    /// Velocity scale (1.0 = as written); results are clamped to 1-127.
    pub vel_scale: f32,
}

impl Default for ExportOptions {
    fn default() -> Self {
        ExportOptions { bars: 0, fill: false, extra_swing: 0.0, channel: 9, gate: PPQ as u32 / 8, vel_scale: 1.0 }
    }
}

fn vlq(mut v: u32, out: &mut Vec<u8>) {
    let mut buf = [0u8; 5];
    let mut i = 4;
    buf[i] = (v & 0x7f) as u8;
    v >>= 7;
    while v > 0 {
        i -= 1;
        buf[i] = (v & 0x7f) as u8 | 0x80;
        v >>= 7;
    }
    out.extend_from_slice(&buf[i..]);
}

/// Render one pattern (optionally looped and with a fill) to SMF bytes.
pub fn render(lib: &Library, idx: usize, opts: &ExportOptions) -> Vec<u8> {
    let p = &lib.patterns[idx];
    let bpb = p.beats_per_bar();
    let bars = if opts.bars == 0 { p.bars } else { opts.bars };
    let fill = if opts.fill { lib.fills_for[idx].first().map(|&f| &lib.patterns[f]) } else { None };
    let swing = (p.swing + opts.extra_swing).clamp(0.0, 1.0) as f64;

    // (tick, order, status, note, vel); order puts note-offs before note-ons at equal ticks
    let mut ev: Vec<(u32, u8, u8, u8, u8)> = Vec::new();
    let tick = |beats: f64| (beats * PPQ as f64).round() as u32;
    let on = 0x90 | (opts.channel & 0x0f);
    let off = 0x80 | (opts.channel & 0x0f);
    let push = |beats: f64, note: u8, vel: u8, ev: &mut Vec<_>| {
        let t = tick(beats);
        ev.push((t, 1, on, note, vel));
        ev.push((t + opts.gate, 0, off, note, 0));
    };
    for b in 0..bars {
        let (src, sbar, sw): (&Pattern, u32, f64) = match fill {
            Some(f) if b == bars - 1 && bars > 1 => (f, 0, (f.swing + opts.extra_swing).clamp(0.0, 1.0) as f64),
            Some(f) if bars == 1 => (f, 0, (f.swing + opts.extra_swing).clamp(0.0, 1.0) as f64),
            _ => (p, b % p.bars, swing),
        };
        for h in src.bar_hits(sbar) {
            let beat = b as f64 * bpb + (h.beat - sbar as f64 * bpb) + sw * h.swing_unit;
            let vel = (h.vel as f32 * opts.vel_scale).round().clamp(1.0, 127.0) as u8;
            push(beat, h.note, vel, &mut ev);
        }
    }
    let _ = CRASH_NOTE;
    ev.sort_by_key(|e| (e.0, e.1));

    let mut trk = Vec::new();
    // track name
    let name = format!("{} - {}", p.genre, p.name);
    trk.extend_from_slice(&[0x00, 0xff, 0x03]);
    vlq(name.len() as u32, &mut trk);
    trk.extend_from_slice(name.as_bytes());
    // tempo
    let us = (60_000_000.0 / p.bpm as f64).round() as u32;
    trk.extend_from_slice(&[0x00, 0xff, 0x51, 0x03, (us >> 16) as u8, (us >> 8) as u8, us as u8]);
    // time signature
    let dd = (p.time_sig.1 as f32).log2() as u8;
    trk.extend_from_slice(&[0x00, 0xff, 0x58, 0x04, p.time_sig.0, dd, 24, 8]);
    let mut last = 0u32;
    for (t, _, st, n, v) in &ev {
        vlq(t - last, &mut trk);
        trk.extend_from_slice(&[*st, *n, *v]);
        last = *t;
    }
    // end of track at the end of the last bar
    let end = tick(bars as f64 * bpb).max(last);
    vlq(end - last, &mut trk);
    trk.extend_from_slice(&[0xff, 0x2f, 0x00]);

    let mut out = Vec::with_capacity(trk.len() + 22);
    out.extend_from_slice(b"MThd");
    out.extend_from_slice(&6u32.to_be_bytes());
    out.extend_from_slice(&0u16.to_be_bytes()); // format 0
    out.extend_from_slice(&1u16.to_be_bytes()); // one track
    out.extend_from_slice(&PPQ.to_be_bytes());
    out.extend_from_slice(b"MTrk");
    out.extend_from_slice(&(trk.len() as u32).to_be_bytes());
    out.extend_from_slice(&trk);
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn vlq_encoding() {
        let mut v = Vec::new();
        vlq(0, &mut v);
        vlq(0x7f, &mut v);
        vlq(0x80, &mut v);
        vlq(0x3fff, &mut v);
        assert_eq!(v, vec![0x00, 0x7f, 0x81, 0x00, 0xff, 0x7f]);
    }

    #[test]
    fn renders_valid_header() {
        let lib = crate::library();
        let bytes = render(lib, lib.grooves[0], &ExportOptions { bars: 4, fill: true, ..Default::default() });
        assert_eq!(&bytes[0..4], b"MThd");
        assert_eq!(&bytes[14..18], b"MTrk");
        let len = u32::from_be_bytes(bytes[18..22].try_into().unwrap()) as usize;
        assert_eq!(bytes.len(), 22 + len);
        assert_eq!(&bytes[bytes.len() - 3..], &[0xff, 0x2f, 0x00]);
    }
}
