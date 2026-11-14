# Albion Road Audio — Surgical EQ series

Per-instrument EQ plugins (CLAP + VST3, Rust/nih-plug) sharing one look and one engine.

Plugins: Albion Road Audio Kick EQ, Albion Road Audio Snare EQ, Albion Road Audio Toms EQ, Albion Road Audio Crash EQ, Albion Road Audio Hi-Hat EQ, Albion Road Audio Bass EQ, Albion Road Audio Acoustic Guitar EQ.

Every plugin has a single row of knobs, each with an on/off switch:

| Knob | Range | Notes |
|---|---|---|
| Low cut (high-pass) | Off / 10 … 150 Hz in 10 Hz steps | Butterworth, 24 dB/oct by default (Acoustic: 48 dB/oct) |
| Instrument bands | ±6 dB, 0 dB at 12 o'clock (notches ±12 dB) | fixed Q per band; the slider under each knob moves the frequency within the band's zone |
| High cut (low-pass) | Off / 12 … 22 kHz in 1 kHz steps by default (Bass: 4 … 20 kHz) | Butterworth, 12 dB/oct by default (Acoustic: 48 dB/oct), capped at 0.45 × sample rate |

Band types: `Peak` (standard digital bell), `AnalogPeak` (bell with the analog shape all the way to 20 kHz, no squeezing near Nyquist), `Notch` (narrow analog-shaped bell, ±12 dB), and shelves.

The graph shows the incoming audio's spectrum as a solid fill, the post-EQ spectrum as a faint line, and the combined EQ response; the Spectrum button turns the analyser off. Shaded areas are each band's sweep zone. The numbered handles drag frequency (left/right) and gain (up/down). Knobs: drag vertically, shift for fine, double-click to reset, scroll to step.

## Layout

- `eqcore/` holds the shared DSP (RBJ and matched "analog" biquads, in f64), parameters, plugin implementation and egui GUI.
- `kick_eq/`, `snare_eq/`, `toms_eq/`, `crash_eq/`, `hihat_eq/`, `bass_eq/`, `acoustic_eq/`, `guitar_eq/` each only define their bands (an `InstrumentDef` in `src/lib.rs`).

## Adding an instrument

1. Copy `kick_eq/` to e.g. `cello_eq/` and rename the crate.
2. Edit its `InstrumentDef`: name, default low/high cut, and the bands with their name, boost/cut hint, kind, default frequency, sweep range and Q. End it with `..InstrumentDef::SERIES` for the standard slopes and high cut steps, or set `hp_sections` / `lp_sections` (1–4 biquads = 12–48 dB/oct) and `lp_steps_khz` yourself.
3. Give it a new `CLAP_ID` and a unique 16-byte `VST3_ID`.
4. Add the crate to the workspace `Cargo.toml` and to `bundler.toml`.

The window widens automatically for extra bands; up to 8 bands are supported.

## Build

```sh
cargo xtask bundle -p kick_eq -p snare_eq -p toms_eq -p crash_eq -p hihat_eq -p bass_eq -p acoustic_eq -p guitar_eq --release
./check-glibc.sh            # must pass for Bitwig's Flatpak (glibc 2.42)
cp target/bundled/*.clap ~/.clap/
cp -r target/bundled/*.vst3 ~/.vst3/
```

Each plugin crate needs the `build.rs` that wraps `acosf`/`atan2f`. Without it, a build on a glibc 2.43 host won't load in the Flatpak Bitwig. Also do log/exp maths in f64: f32 `log10f` and friends pull in GLIBC_2.43 too.

GUI preview without a DAW: `cargo run --release -p kick_eq --features standalone --bin kick_eq_standalone -- -b dummy` (set `ALBION_THEME=light` to start in light mode; the theme is shared with every Albion Road Audio plugin via `~/.config/Albion Road Audio/settings.xml`).

## Licence

Free software under the **GNU Affero General Public License v3.0 or later** (see `LICENSE`). Third-party components are listed in `NOTICE.md`, with full texts in `THIRD-PARTY-LICENSES.txt`. Each plugin has its own user README (`kick_eq/README.md`, …). Release zips are built with `releases/package.py` at the top of the repo.
