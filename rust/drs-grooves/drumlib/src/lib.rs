//! Drum pattern library: a small text format for drum grooves, a parser, the
//! built-in genre library and a Standard MIDI File writer.
//!
//! # Pattern format
//!
//! ```text
//! genre: Rock
//! defaults: bpm=110 time=4/4
//!
//! groove: Basic 8ths
//!   CH  x-x- x-x- x-x- x-x-
//!   SN  ---- X--- ---- X---
//!   BD  x--- ---- x--- ----
//!
//! fill: Snare 16ths  bars=1
//!   ...
//! ```
//!
//! * Each instrument row lists steps for the whole pattern. Spaces and `|`
//!   are ignored. A row's length divided by `bars` is its steps per bar, so
//!   rows can mix resolutions (16ths on the snare, triplets on the hats).
//! * Step characters: `X` accent, `x` normal, `o` soft, `g` ghost,
//!   `-` or `.` rest. `r`/`t`/`q` play 2/3/4 quick hits inside the step
//!   (rolls); upper case `R`/`T`/`Q` are accented rolls.
//! * Options: `bpm=`, `time=N/D`, `bars=`, `swing=` (0-100, where 100 is a
//!   full triplet feel; applies to off-beat 8ths/16ths).

pub mod sequencer;
pub mod smf;

use std::sync::OnceLock;

/// General MIDI drum notes for the instrument codes used in pattern files.
pub const INSTRUMENTS: &[(&str, u8, &str)] = &[
    ("BD", 36, "Kick"),
    ("BD2", 35, "Acoustic kick"),
    ("SN", 38, "Snare"),
    ("SN2", 40, "Electric snare"),
    ("SS", 37, "Side stick"),
    ("CL", 39, "Clap"),
    ("CH", 42, "Closed hi-hat"),
    ("PH", 44, "Pedal hi-hat"),
    ("OH", 46, "Open hi-hat"),
    ("FT", 41, "Low floor tom"),
    ("FT2", 43, "High floor tom"),
    ("LT", 45, "Low tom"),
    ("MT", 47, "Low-mid tom"),
    ("MT2", 48, "High-mid tom"),
    ("HT", 50, "High tom"),
    ("CR", 49, "Crash"),
    ("CR2", 57, "Crash 2"),
    ("RD", 51, "Ride"),
    ("RB", 53, "Ride bell"),
    ("CN", 52, "China"),
    ("SP", 55, "Splash"),
    ("TB", 54, "Tambourine"),
    ("CB", 56, "Cowbell"),
    ("HB", 60, "High bongo"),
    ("LB", 61, "Low bongo"),
    ("MC", 62, "Muted high conga"),
    ("HC", 63, "Open high conga"),
    ("LC", 64, "Low conga"),
    ("HTI", 65, "High timbale"),
    ("LTI", 66, "Low timbale"),
    ("HA", 67, "High agogo"),
    ("LA", 68, "Low agogo"),
    ("CA", 69, "Cabasa"),
    ("SH", 70, "Shaker (maracas)"),
    ("GU", 73, "Guiro"),
    ("CV", 75, "Claves"),
    ("HW", 76, "High wood block"),
    ("LW", 77, "Low wood block"),
    ("TR", 81, "Triangle"),
];

pub const CRASH_NOTE: u8 = 49;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Kind {
    Groove,
    Fill,
}

/// One drum hit, positioned in beats (quarter notes) from the pattern start.
#[derive(Debug, Clone, Copy)]
pub struct Hit {
    pub beat: f64,
    /// How far (in beats) this hit moves at 100% swing. Zero for on-beat hits.
    pub swing_unit: f64,
    pub note: u8,
    pub vel: u8,
}

#[derive(Debug, Clone)]
pub struct Pattern {
    pub genre: String,
    pub name: String,
    pub kind: Kind,
    pub bpm: f32,
    /// 0.0 = straight, 1.0 = full triplet swing.
    pub swing: f32,
    pub time_sig: (u8, u8),
    pub bars: u32,
    /// Hits sorted by beat.
    pub hits: Vec<Hit>,
    /// Index range into `hits` for each bar.
    pub bar_ranges: Vec<(usize, usize)>,
}

impl Pattern {
    /// Length of one bar in quarter-note beats.
    pub fn beats_per_bar(&self) -> f64 {
        self.time_sig.0 as f64 * 4.0 / self.time_sig.1 as f64
    }
    pub fn length_beats(&self) -> f64 {
        self.beats_per_bar() * self.bars as f64
    }
    pub fn bar_hits(&self, bar: u32) -> &[Hit] {
        let (a, b) = self.bar_ranges[(bar % self.bars) as usize];
        &self.hits[a..b]
    }
    /// "Rock › Basic 8ths"
    pub fn label(&self) -> String {
        format!("{} › {}", self.genre, self.name)
    }
}

#[derive(Debug, Default)]
pub struct Library {
    pub patterns: Vec<Pattern>,
    pub genres: Vec<String>,
    /// Indices into `patterns` of every groove, in library order.
    pub grooves: Vec<usize>,
    /// For each pattern index: compatible fills (same genre and meter).
    pub fills_for: Vec<Vec<usize>>,
}

#[derive(Debug)]
pub struct ParseError {
    pub file: String,
    pub line: usize,
    pub msg: String,
}

impl std::fmt::Display for ParseError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}:{}: {}", self.file, self.line, self.msg)
    }
}
impl std::error::Error for ParseError {}

/// The pattern files compiled into the library, in display order.
pub const SOURCES: &[(&str, &str)] = &[
    ("rock.txt", include_str!("../patterns/rock.txt")),
    ("punk.txt", include_str!("../patterns/punk.txt")),
    ("metal.txt", include_str!("../patterns/metal.txt")),
    ("pop.txt", include_str!("../patterns/pop.txt")),
    ("funk.txt", include_str!("../patterns/funk.txt")),
    ("soul_rnb.txt", include_str!("../patterns/soul_rnb.txt")),
    ("disco.txt", include_str!("../patterns/disco.txt")),
    ("hiphop.txt", include_str!("../patterns/hiphop.txt")),
    ("trap.txt", include_str!("../patterns/trap.txt")),
    ("house.txt", include_str!("../patterns/house.txt")),
    ("techno.txt", include_str!("../patterns/techno.txt")),
    ("trance.txt", include_str!("../patterns/trance.txt")),
    ("dnb.txt", include_str!("../patterns/dnb.txt")),
    ("dubstep.txt", include_str!("../patterns/dubstep.txt")),
    ("ukg.txt", include_str!("../patterns/ukg.txt")),
    ("breakbeat.txt", include_str!("../patterns/breakbeat.txt")),
    ("synthwave.txt", include_str!("../patterns/synthwave.txt")),
    ("reggae.txt", include_str!("../patterns/reggae.txt")),
    ("reggaeton.txt", include_str!("../patterns/reggaeton.txt")),
    ("afro.txt", include_str!("../patterns/afro.txt")),
    ("latin.txt", include_str!("../patterns/latin.txt")),
    ("blues.txt", include_str!("../patterns/blues.txt")),
    ("jazz.txt", include_str!("../patterns/jazz.txt")),
    ("country.txt", include_str!("../patterns/country.txt")),
    ("world.txt", include_str!("../patterns/world.txt")),
    ("odd.txt", include_str!("../patterns/odd.txt")),
];

/// The built-in library, parsed once on first use.
pub fn library() -> &'static Library {
    static LIB: OnceLock<Library> = OnceLock::new();
    LIB.get_or_init(|| {
        Library::from_sources(SOURCES).unwrap_or_else(|e| panic!("built-in pattern error: {e}"))
    })
}

/// An instrument table: (code used in pattern files, MIDI note, description).
pub type InstrumentTable = [(&'static str, u8, &'static str)];

fn note_for(table: &InstrumentTable, code: &str) -> Option<u8> {
    table
        .iter()
        .find(|(c, _, _)| c.eq_ignore_ascii_case(code))
        .map(|(_, n, _)| *n)
}

#[derive(Clone, Copy)]
struct Opts {
    bpm: f32,
    time: (u8, u8),
    bars: u32,
    swing: f32,
}

impl Default for Opts {
    fn default() -> Self {
        Opts { bpm: 120.0, time: (4, 4), bars: 1, swing: 0.0 }
    }
}

fn apply_opt(o: &mut Opts, tok: &str) -> Result<(), String> {
    let (k, v) = tok.split_once('=').ok_or_else(|| format!("bad option `{tok}`"))?;
    match k {
        "bpm" => o.bpm = v.parse().map_err(|_| format!("bad bpm `{v}`"))?,
        "bars" => {
            o.bars = v.parse().map_err(|_| format!("bad bars `{v}`"))?;
            if o.bars == 0 {
                return Err("bars must be at least 1".into());
            }
        }
        "swing" => {
            let s: f32 = v.parse().map_err(|_| format!("bad swing `{v}`"))?;
            o.swing = (s / 100.0).clamp(0.0, 1.0);
        }
        "time" => {
            let (n, d) = v.split_once('/').ok_or_else(|| format!("bad time `{v}`"))?;
            let n: u8 = n.parse().map_err(|_| format!("bad time `{v}`"))?;
            let d: u8 = d.parse().map_err(|_| format!("bad time `{v}`"))?;
            if n == 0 || ![2, 4, 8, 16].contains(&d) {
                return Err(format!("unsupported time signature `{v}`"));
            }
            o.time = (n, d);
        }
        _ => return Err(format!("unknown option `{k}`")),
    }
    Ok(())
}

/// Split "Name with words  bpm=90 bars=2" into name and option tokens.
fn split_header(rest: &str) -> (String, Vec<&str>) {
    let toks: Vec<&str> = rest.split_whitespace().collect();
    let first_opt = toks.iter().position(|t| t.contains('=')).unwrap_or(toks.len());
    (toks[..first_opt].join(" "), toks[first_opt..].to_vec())
}

struct Draft {
    name: String,
    kind: Kind,
    opts: Opts,
    rows: Vec<(u8, String, usize)>, // note, steps, line
    line: usize,
}

impl Library {
    /// Parse pattern files using the General MIDI drum map.
    pub fn from_sources(sources: &[(&str, &str)]) -> Result<Library, ParseError> {
        Self::from_sources_with(sources, INSTRUMENTS)
    }

    /// Parse pattern files using a custom instrument table (e.g. a specific kit's midimap).
    pub fn from_sources_with(sources: &[(&str, &str)], table: &InstrumentTable) -> Result<Library, ParseError> {
        let mut lib = Library::default();
        for (file, text) in sources {
            parse_into(&mut lib, file, text, table)?;
        }
        lib.index();
        Ok(lib)
    }

    fn index(&mut self) {
        self.grooves = (0..self.patterns.len())
            .filter(|&i| self.patterns[i].kind == Kind::Groove)
            .collect();
        self.fills_for = self
            .patterns
            .iter()
            .map(|p| {
                (0..self.patterns.len())
                    .filter(|&j| {
                        let f = &self.patterns[j];
                        f.kind == Kind::Fill && f.genre == p.genre && f.time_sig == p.time_sig
                    })
                    .collect()
            })
            .collect();
    }
}

fn parse_into(lib: &mut Library, file: &str, text: &str, table: &InstrumentTable) -> Result<(), ParseError> {
    let err = |line: usize, msg: String| ParseError { file: file.to_string(), line, msg };
    let mut genre: Option<String> = None;
    let mut defaults = Opts::default();
    let mut draft: Option<Draft> = None;

    let finish = |lib: &mut Library, d: Option<Draft>, genre: &Option<String>| -> Result<(), ParseError> {
        if let Some(d) = d {
            let p = build(d, genre.clone().unwrap_or_default()).map_err(|(l, m)| err(l, m))?;
            lib.patterns.push(p);
        }
        Ok(())
    };

    for (i, raw) in text.lines().enumerate() {
        let ln = i + 1;
        let line = raw.split('#').next().unwrap_or("").trim();
        if line.is_empty() {
            continue;
        }
        if let Some(rest) = line.strip_prefix("genre:") {
            finish(lib, draft.take(), &genre)?;
            let g = rest.trim().to_string();
            if !lib.genres.contains(&g) {
                lib.genres.push(g.clone());
            }
            genre = Some(g);
            defaults = Opts::default();
        } else if let Some(rest) = line.strip_prefix("defaults:") {
            for t in rest.split_whitespace() {
                apply_opt(&mut defaults, t).map_err(|m| err(ln, m))?;
            }
        } else if let Some((kind, rest)) = line
            .strip_prefix("groove:")
            .map(|r| (Kind::Groove, r))
            .or_else(|| line.strip_prefix("fill:").map(|r| (Kind::Fill, r)))
        {
            finish(lib, draft.take(), &genre)?;
            if genre.is_none() {
                return Err(err(ln, "pattern before any `genre:` line".into()));
            }
            let (name, toks) = split_header(rest);
            if name.is_empty() {
                return Err(err(ln, "pattern needs a name".into()));
            }
            let mut opts = defaults;
            opts.bars = 1;
            for t in toks {
                apply_opt(&mut opts, t).map_err(|m| err(ln, m))?;
            }
            draft = Some(Draft { name, kind, opts, rows: Vec::new(), line: ln });
        } else {
            let d = draft.as_mut().ok_or_else(|| err(ln, format!("row outside a pattern: `{line}`")))?;
            let mut it = line.splitn(2, char::is_whitespace);
            let code = it.next().unwrap_or("");
            let steps: String = it.next().unwrap_or("").chars().filter(|c| !c.is_whitespace() && *c != '|').collect();
            let note = note_for(table, code).ok_or_else(|| err(ln, format!("unknown instrument `{code}`")))?;
            if steps.is_empty() {
                return Err(err(ln, format!("row `{code}` has no steps")));
            }
            d.rows.push((note, steps, ln));
        }
    }
    finish(lib, draft.take(), &genre)
}

fn build(d: Draft, genre: String) -> Result<Pattern, (usize, String)> {
    let o = d.opts;
    let bpb = o.time.0 as f64 * 4.0 / o.time.1 as f64;
    if d.rows.is_empty() {
        return Err((d.line, format!("pattern `{}` has no rows", d.name)));
    }
    let mut hits = Vec::new();
    for (note, steps, ln) in &d.rows {
        let chars: Vec<char> = steps.chars().collect();
        if chars.len() % o.bars as usize != 0 {
            return Err((*ln, format!("row has {} steps, not divisible by {} bars", chars.len(), o.bars)));
        }
        let per_bar = chars.len() / o.bars as usize;
        let step = bpb / per_bar as f64;
        let per_beat = per_bar as f64 / bpb;
        let swingable = (per_beat - 2.0).abs() < 1e-9 || (per_beat - 4.0).abs() < 1e-9;
        for (k, c) in chars.iter().enumerate() {
            let (count, vel) = match c {
                '-' | '.' => continue,
                'X' => (1, 120),
                'x' => (1, 96),
                'o' => (1, 72),
                'g' => (1, 42),
                'r' => (2, 84),
                'R' => (2, 110),
                't' => (3, 84),
                'T' => (3, 110),
                'q' => (4, 84),
                'Q' => (4, 110),
                _ => return Err((*ln, format!("unknown step character `{c}`"))),
            };
            let base = k as f64 * step;
            let swing_unit = if swingable && k % 2 == 1 { step / 3.0 } else { 0.0 };
            for n in 0..count {
                let v = if count > 1 { vel - (count - 1 - n) as i32 * 8 } else { vel };
                hits.push(Hit {
                    beat: base + n as f64 * step / count as f64,
                    swing_unit: if n == 0 { swing_unit } else { swing_unit * (1.0 - n as f64 / count as f64) },
                    note: *note,
                    vel: v.clamp(1, 127) as u8,
                });
            }
        }
    }
    hits.sort_by(|a, b| a.beat.partial_cmp(&b.beat).unwrap().then(a.note.cmp(&b.note)));
    let mut bar_ranges = Vec::with_capacity(o.bars as usize);
    for b in 0..o.bars {
        let lo = b as f64 * bpb - 1e-9;
        let hi = (b + 1) as f64 * bpb - 1e-9;
        let a = hits.partition_point(|h| h.beat < lo);
        let z = hits.partition_point(|h| h.beat < hi);
        bar_ranges.push((a, z));
    }
    Ok(Pattern {
        genre,
        name: d.name,
        kind: d.kind,
        bpm: o.bpm,
        swing: o.swing,
        time_sig: o.time,
        bars: o.bars,
        hits,
        bar_ranges,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn library_parses() {
        let lib = library();
        assert!(lib.genres.len() >= 20, "genres: {}", lib.genres.len());
        assert!(lib.grooves.len() >= 100, "grooves: {}", lib.grooves.len());
        for p in &lib.patterns {
            assert!(!p.hits.is_empty(), "{} is empty", p.label());
            let len = p.length_beats();
            for h in &p.hits {
                assert!(h.beat >= 0.0 && h.beat < len, "{} hit outside pattern", p.label());
            }
            let total: usize = p.bar_ranges.iter().map(|(a, b)| b - a).sum();
            assert_eq!(total, p.hits.len(), "{} bar ranges", p.label());
        }
    }

    #[test]
    fn every_genre_has_a_fill() {
        let lib = library();
        for g in &lib.genres {
            assert!(
                lib.patterns.iter().any(|p| &p.genre == g && p.kind == Kind::Fill),
                "{g} has no fills"
            );
        }
    }

    #[test]
    fn mixed_resolution_rows() {
        let src = "genre: T\ngroove: Mix\n CH x-x x-x x-x x-x\n SN ---- x--- ---- x---\n";
        let lib = Library::from_sources(&[("t", src)]).unwrap();
        let p = &lib.patterns[0];
        assert_eq!(p.hits.len(), 10);
        // triplet hat: second hit of beat 1 is at 2/3 of a beat
        let hat: Vec<f64> = p.hits.iter().filter(|h| h.note == 42).map(|h| h.beat).collect();
        assert!((hat[1] - 2.0 / 3.0).abs() < 1e-9);
    }

    #[test]
    fn bad_row_reports_line() {
        let src = "genre: T\ngroove: Bad bars=2\n CH x-x\n";
        let e = Library::from_sources(&[("t", src)]).unwrap_err();
        assert_eq!(e.line, 3);
    }
}
