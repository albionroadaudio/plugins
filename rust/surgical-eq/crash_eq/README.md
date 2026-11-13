# Albion Road Audio Crash EQ

![Albion Road Audio Crash EQ](docs/screenshot.png)

Part of the **Albion Road Audio Surgical EQ series**: quick, focused EQs for crash cymbals. Each
knob is already in the right area for that instrument, with enough room to tweak.

Formats: **CLAP** and **VST3** for 64-bit Linux. Mono or stereo, zero latency.

In your DAW it's listed as **ARA Crash EQ**.

## Controls

| Knob | Move | Frequency zone | Default | What it's for |
|---|---|---|---|---|
| Low cut | high-pass | Off / 10 … 150 Hz | 150 Hz | 24 dB/oct; removes rumble and bleed below the instrument |
| 1 · Body | boost / cut | 280–400 Hz | 340 Hz | The cymbal's low-mid weight |
| 2 · Bleed | cut | 400–650 Hz | 500 Hz | Drum bleed picked up by the cymbal mic |
| 3 · Harsh | cut | 3–5.5 kHz | 4 kHz | Harsh, clangy mids |
| 4 · Sizzle | boost | 8–14 kHz | 10 kHz | Air and sizzle |
| High cut | low-pass | Off / 12 … 22 kHz | 22 kHz | 12 dB/oct; tames hiss and spill above the instrument |

- Every band is **±6 dB**, with 0 dB at 12 o'clock. "Move" is a hint of the usual
  direction, not a limit.
- The **slider** under each band knob moves its frequency within its zone.
- Every knob has its own **On/Off** switch.
- The **graph** shows the incoming audio as a solid fill, the processed audio as a faint
  line, and the EQ curve. The shaded areas are the band zones. Drag the numbered handles:
  left/right moves frequency, up/down moves gain.
- **Spectrum** turns the analyser off. **Light/Dark mode** switches the theme, and the choice is
  shared by every Albion Road Audio plugin.
- Knobs: drag vertically, hold Shift for fine control, double-click to reset, scroll to step.

## Install

See `INSTALL.txt` in the release zip. In short: copy the `.clap` file to `~/.clap/` and/or the
`.vst3` folder to `~/.vst3/`, then rescan plugins in your DAW.

## Build from source

The series shares one Rust workspace (`eqcore` holds the DSP and GUI; each instrument crate
only defines its bands). See the workspace `README.md`:

```sh
cargo xtask bundle -p crash_eq --release
```

## Licence

Free software under the **GNU Affero General Public License v3.0 or later** (see `LICENSE`).
Third-party components are listed in `NOTICE.md`, with their full licence texts in
`THIRD-PARTY-LICENSES.txt`.

Albion Road Audio · https://www.albionroadaudio.co.uk
