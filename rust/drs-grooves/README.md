# Albion Road Audio DRS Grooves: workspace

Rust workspace for **Albion Road Audio DRS Grooves**, a MIDI note-effect plugin with rock, indie, grunge, punk and metal grooves for the DrumGizmo DRS Kit. The user README is [`drs/README.md`](drs/README.md).

- `drs/` is the plugin: parameters, sequencer glue, GUI (egui), the DRS Kit note map (`kit.rs`) and the patterns (`patterns/*.txt`).
- `drumlib/` is the pattern engine: the text pattern format, the sequencer and the MIDI file writer. Its own pattern library and `drumlib-export` tool belong to Albion Road Audio DrumGen, which is a separate project.

## Build

```sh
cargo test                                    # every pattern parses; MIDI export works
cargo xtask bundle -p drs_grooves --release   # -> target/bundled/Albion Road Audio DRS Grooves.clap / .vst3
./check-glibc.sh
cp    "target/bundled/Albion Road Audio DRS Grooves.clap" ~/.clap/
cp -r "target/bundled/Albion Road Audio DRS Grooves.vst3" ~/.vst3/
```

GUI preview without a DAW: `cargo run --release -p drs_grooves --features standalone --bin drs_grooves_standalone -- -b dummy` (`ALBION_THEME=light` for light mode).

## Patterns

One file per genre in `drs/patterns/`, using the DRS Kit's codes (`KD`, `SN`, `HH`, `HS`, `RD`, `CL`, …; see `drs/src/kit.rs`). The format is documented at the top of `drumlib/src/lib.rs`. Run `cargo test` after editing; a bad row is reported with its file and line number.

## Licence

AGPL-3.0-or-later (see `LICENSE`). Third-party components are listed in `NOTICE.md`, with full texts in `THIRD-PARTY-LICENSES.txt`.
