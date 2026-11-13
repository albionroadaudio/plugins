//! Write the drum pattern library out as .mid files.
//!
//! Usage: drumlib-export [OUT_DIR] [--bars N] [--fill] [--swing PCT] [--genre NAME] [--list]

use drumlib::{library, smf, Kind};
use std::fs;
use std::path::PathBuf;

fn sanitize(s: &str) -> String {
    s.chars()
        .map(|c| if c.is_alphanumeric() || " -_&().,'".contains(c) { c } else { '-' })
        .collect::<String>()
        .trim()
        .to_string()
}

fn main() {
    let mut out = PathBuf::from("drum-midi");
    let mut opts = smf::ExportOptions::default();
    let mut phrase_bars = 4u32;
    let mut genre_filter: Option<String> = None;
    let mut list = false;
    let mut args = std::env::args().skip(1);
    while let Some(a) = args.next() {
        match a.as_str() {
            "--bars" => phrase_bars = args.next().and_then(|v| v.parse().ok()).expect("--bars needs a number"),
            "--fill" => opts.fill = true,
            "--swing" => {
                let s: f32 = args.next().and_then(|v| v.parse().ok()).expect("--swing needs a percentage");
                opts.extra_swing = (s / 100.0).clamp(0.0, 1.0);
            }
            "--genre" => genre_filter = Some(args.next().expect("--genre needs a name").to_lowercase()),
            "--list" => list = true,
            "-h" | "--help" => {
                println!("drumlib-export [OUT_DIR] [--bars N] [--fill] [--swing PCT] [--genre NAME] [--list]");
                println!("  OUT_DIR      where to write files (default ./drum-midi)");
                println!("  --bars N     loop each groove to N bars (default 4; 0 = native length)");
                println!("  --fill       put a genre fill in the last bar of each groove");
                println!("  --swing PCT  extra swing, 0-100 (100 = triplet feel)");
                println!("  --genre NAME only export one genre");
                println!("  --list       print the library instead of writing files");
                return;
            }
            _ if a.starts_with('-') => {
                eprintln!("unknown option {a} (try --help)");
                std::process::exit(2);
            }
            _ => out = PathBuf::from(a),
        }
    }

    let lib = library();
    if list {
        for (gi, g) in lib.genres.iter().enumerate() {
            println!("{:02} {g}", gi + 1);
            for p in lib.patterns.iter().filter(|p| &p.genre == g) {
                let k = if p.kind == Kind::Groove { "groove" } else { "fill  " };
                println!(
                    "     {k} {:<34} {:>3} BPM  {}/{}  {} bar{}{}",
                    p.name,
                    p.bpm,
                    p.time_sig.0,
                    p.time_sig.1,
                    p.bars,
                    if p.bars == 1 { "" } else { "s" },
                    if p.swing > 0.0 { format!("  swing {:.0}%", p.swing * 100.0) } else { String::new() }
                );
            }
        }
        println!("\n{} genres, {} grooves, {} fills", lib.genres.len(), lib.grooves.len(), lib.patterns.len() - lib.grooves.len());
        return;
    }

    let mut written = 0;
    for (gi, g) in lib.genres.iter().enumerate() {
        if let Some(f) = &genre_filter {
            if &g.to_lowercase() != f {
                continue;
            }
        }
        let gdir = out.join(format!("{:02} {}", gi + 1, sanitize(g)));
        let mut n_groove = 0;
        let mut n_fill = 0;
        for (idx, p) in lib.patterns.iter().enumerate().filter(|(_, p)| &p.genre == g) {
            let (sub, num, o) = match p.kind {
                Kind::Groove => {
                    n_groove += 1;
                    let bars = if phrase_bars == 0 { p.bars } else { phrase_bars.max(p.bars) };
                    ("Grooves", n_groove, smf::ExportOptions { bars, ..opts.clone() })
                }
                Kind::Fill => {
                    n_fill += 1;
                    ("Fills", n_fill, smf::ExportOptions { bars: p.bars, fill: false, ..opts.clone() })
                }
            };
            let dir = gdir.join(sub);
            fs::create_dir_all(&dir).expect("create output folder");
            let file = dir.join(format!("{:02} {} ({} BPM).mid", num, sanitize(&p.name), p.bpm));
            fs::write(&file, smf::render(lib, idx, &o)).expect("write MIDI file");
            written += 1;
        }
    }
    println!("Wrote {written} MIDI files to {}", out.display());
}
