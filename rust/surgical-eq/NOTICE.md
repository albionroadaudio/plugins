# Albion Road Audio Surgical EQ series: licence and notices

The Albion Road Audio Surgical EQ series (Kick EQ, Snare EQ, Toms EQ, Crash EQ, Hi-Hat EQ, Bass EQ, Acoustic Guitar EQ and Distorted Guitar EQ) are per-instrument EQ plugins by Albion Road Audio.

Copyright (C) 2026 Albion Road Audio

This program is free software: you can redistribute it and/or modify it under the
terms of the GNU Affero General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public License along with
this program (the `LICENSE` file). If not, see <https://www.gnu.org/licenses/>.

**Source code:** release zips include the complete source for that exact build in
`SOURCE/`. Build instructions are in `README.md`.

# Third-party software

The plugins are written in Rust with [nih-plug](https://github.com/robbert-vdh/nih-plug)
(ISC). Its VST3 support uses `vst3-sys`, which is licensed under the **GNU General Public
License v3**. GPLv3 code may be combined with AGPLv3 code (AGPLv3 section 13).
VST is a registered trademark of Steinberg Media Technologies GmbH.

## Liberation fonts (SIL Open Font License 1.1)

The GUI embeds Liberation Sans, Liberation Mono and Liberation Serif
(Copyright (c) 2012 Red Hat, Inc. and contributors). Licensed under the SIL Open Font
License, Version 1.1; the full licence is in `LICENSE-Liberation.txt`.

The following crates are compiled into the plugin binaries. Each crate's full licence
text (and copyright notice) is reproduced in `THIRD-PARTY-LICENSES.txt`.

| Crate | Version | Licence | Source |
|---|---|---|---|
| ab_glyph | 0.2.32 | Apache-2.0 | https://github.com/alexheretic/ab-glyph |
| ab_glyph_rasterizer | 0.1.10 | Apache-2.0 | https://github.com/alexheretic/ab-glyph |
| addr2line | 0.27.1 | Apache-2.0 OR MIT | https://github.com/gimli-rs/addr2line |
| adler2 | 2.0.1 | 0BSD OR MIT OR Apache-2.0 | https://github.com/oyvindln/adler2 |
| ahash | 0.8.12 | MIT OR Apache-2.0 | https://github.com/tkaitchuck/ahash |
| anyhow | 1.0.104 | MIT OR Apache-2.0 | https://github.com/dtolnay/anyhow |
| anymap3 | 1.1.0 | BlueOak-1.0.0 OR MIT OR Apache-2.0 | https://github.com/reivilibre/anymap3 |
| as-raw-xcb-connection | 1.0.1 | MIT OR Apache-2.0 | https://github.com/psychon/as-raw-xcb-connection |
| atomic_float | 0.1.0 | Apache-2.0 OR MIT OR Zlib | https://github.com/thomcc/atomic_float |
| atomic_refcell | 0.1.14 | Apache-2.0 OR MIT | https://github.com/mozilla/atomic_refcell |
| atty | 0.2.14 | MIT | https://github.com/softprops/atty |
| backtrace | 0.3.77 | MIT OR Apache-2.0 | https://github.com/rust-lang/backtrace-rs |
| baseview | 0.1.0 | MIT OR Apache-2.0 |  |
| bitflags | 1.3.2 | MIT/Apache-2.0 | https://github.com/bitflags/bitflags |
| bitflags | 2.13.2 | MIT OR Apache-2.0 | https://github.com/bitflags/bitflags |
| bytemuck | 1.25.2 | Zlib OR Apache-2.0 OR MIT | https://github.com/Lokathor/bytemuck |
| bytemuck_derive | 1.12.1 | Zlib OR Apache-2.0 OR MIT | https://github.com/Lokathor/bytemuck |
| calloop | 0.13.0 | MIT | https://github.com/Smithay/calloop |
| cfg-if | 1.0.5 | MIT OR Apache-2.0 | https://github.com/rust-lang/cfg-if |
| clap-sys | 0.5.0 | MIT/Apache-2.0 | https://github.com/micahrj/clap-sys |
| copypasta | 0.10.2 | MIT / Apache-2.0 | https://github.com/alacritty/copypasta |
| crossbeam | 0.8.5 | MIT OR Apache-2.0 | https://github.com/crossbeam-rs/crossbeam |
| crossbeam-channel | 0.5.17 | MIT OR Apache-2.0 | https://github.com/crossbeam-rs/crossbeam |
| crossbeam-deque | 0.8.8 | MIT OR Apache-2.0 | https://github.com/crossbeam-rs/crossbeam |
| crossbeam-epoch | 0.9.21 | MIT OR Apache-2.0 | https://github.com/crossbeam-rs/crossbeam |
| crossbeam-queue | 0.3.14 | MIT OR Apache-2.0 | https://github.com/crossbeam-rs/crossbeam |
| crossbeam-utils | 0.8.23 | MIT OR Apache-2.0 | https://github.com/crossbeam-rs/crossbeam |
| cursor-icon | 1.2.0 | MIT OR Apache-2.0 OR Zlib | https://github.com/rust-windowing/cursor-icon |
| deranged | 0.5.9 | MIT OR Apache-2.0 | https://github.com/jhpratt/deranged |
| dlib | 0.5.3 | MIT | https://github.com/elinorbgr/dlib |
| dpi | 0.1.2 | Apache-2.0 AND MIT | https://github.com/rust-windowing/winit |
| ecolor | 0.31.1 | MIT OR Apache-2.0 | https://github.com/emilk/egui |
| egui | 0.31.1 | MIT OR Apache-2.0 | https://github.com/emilk/egui |
| egui-baseview | 0.5.0 | MIT | https://github.com/BillyDM/egui-baseview |
| egui_glow | 0.31.1 | MIT OR Apache-2.0 | https://github.com/emilk/egui/tree/master/crates/egui_glow |
| emath | 0.31.1 | MIT OR Apache-2.0 | https://github.com/emilk/egui/tree/master/crates/emath |
| epaint | 0.31.1 | MIT OR Apache-2.0 | https://github.com/emilk/egui/tree/master/crates/epaint |
| epaint_default_fonts | 0.31.1 | (MIT OR Apache-2.0) AND OFL-1.1 AND Ubuntu-font-1.0 | https://github.com/emilk/egui/tree/master/crates/epaint_default_fonts |
| errno | 0.3.15 | MIT OR Apache-2.0 | https://github.com/lambda-fairy/rust-errno |
| gethostname | 1.1.0 | Apache-2.0 | https://codeberg.org/swsnr/gethostname.rs.git |
| gimli | 0.34.0 | MIT OR Apache-2.0 | https://github.com/gimli-rs/gimli |
| glow | 0.16.0 | MIT OR Apache-2.0 OR Zlib | https://github.com/grovesNL/glow |
| is-docker | 0.2.0 | MIT | https://github.com/TheLarkInn/is-docker |
| is-wsl | 0.4.0 | MIT | https://github.com/TheLarkInn/is-wsl |
| itoa | 1.0.19 | MIT OR Apache-2.0 | https://github.com/dtolnay/itoa |
| keyboard-types | 0.6.2 | MIT/Apache-2.0 | https://github.com/pyfisch/keyboard-types |
| libc | 0.2.191 | MIT OR Apache-2.0 | https://github.com/rust-lang/libc |
| libloading | 0.8.9 | ISC | https://github.com/nagisa/rust_libloading/ |
| linux-raw-sys | 0.12.1 | Apache-2.0 WITH LLVM-exception OR Apache-2.0 OR MIT | https://github.com/sunfishcode/linux-raw-sys |
| linux-raw-sys | 0.4.15 | Apache-2.0 WITH LLVM-exception OR Apache-2.0 OR MIT | https://github.com/sunfishcode/linux-raw-sys |
| lock_api | 0.4.14 | MIT OR Apache-2.0 | https://github.com/Amanieu/parking_lot |
| log | 0.4.35 | MIT OR Apache-2.0 | https://github.com/rust-lang/log |
| memchr | 2.8.3 | Unlicense OR MIT | https://github.com/BurntSushi/memchr |
| memoffset | 0.6.5 | MIT | https://github.com/Gilnaa/memoffset |
| memoffset | 0.9.1 | MIT | https://github.com/Gilnaa/memoffset |
| midi-consts | 0.1.0 | MIT OR Apache-2.0 | https://codeberg.org/PieterPenninckx/midi-consts |
| miniz_oxide | 0.9.1 | MIT OR Zlib OR Apache-2.0 | https://github.com/Frommi/miniz_oxide/tree/master/miniz_oxide |
| nih_log | 0.3.1 | ISC | https://github.com/robbert-vdh/nih-log |
| nih_plug | 0.0.0 | ISC | https://github.com/robbert-vdh/nih-plug |
| nih_plug_derive | 0.1.0 | ISC |  |
| nih_plug_egui | 0.0.0 | ISC |  |
| nix | 0.22.3 | MIT | https://github.com/nix-rust/nix |
| nohash-hasher | 0.2.0 | Apache-2.0 OR MIT | https://github.com/paritytech/nohash-hasher |
| num-conv | 0.2.2 | MIT OR Apache-2.0 | https://github.com/jhpratt/num-conv |
| num_threads | 0.1.8 | MIT OR Apache-2.0 | https://github.com/jhpratt/num_threads |
| object | 0.40.0 | Apache-2.0 OR MIT | https://github.com/gimli-rs/object |
| once_cell | 1.21.4 | MIT OR Apache-2.0 | https://github.com/matklad/once_cell |
| open | 5.4.5 | MIT | https://github.com/Byron/open-rs |
| owned_ttf_parser | 0.25.1 | Apache-2.0 | https://github.com/alexheretic/owned-ttf-parser |
| parking_lot | 0.12.5 | MIT OR Apache-2.0 | https://github.com/Amanieu/parking_lot |
| parking_lot_core | 0.9.12 | MIT OR Apache-2.0 | https://github.com/Amanieu/parking_lot |
| percent-encoding | 2.3.2 | MIT OR Apache-2.0 | https://github.com/servo/rust-url/ |
| pin-project-lite | 0.2.17 | Apache-2.0 OR MIT | https://github.com/taiki-e/pin-project-lite |
| polling | 3.11.0 | Apache-2.0 OR MIT | https://github.com/smol-rs/polling |
| powerfmt | 0.2.1 | MIT OR Apache-2.0 | https://github.com/jhpratt/powerfmt |
| proc-macro2 | 1.0.107 | MIT OR Apache-2.0 | https://github.com/dtolnay/proc-macro2 |
| profiling | 1.0.18 | MIT OR Apache-2.0 | https://github.com/aclysma/profiling |
| quote | 1.0.47 | MIT OR Apache-2.0 | https://github.com/dtolnay/quote |
| raw-window-handle | 0.5.2 | MIT OR Apache-2.0 OR Zlib | https://github.com/rust-windowing/raw-window-handle |
| raw-window-handle | 0.6.2 | MIT OR Apache-2.0 OR Zlib | https://github.com/rust-windowing/raw-window-handle |
| rustc-demangle | 0.1.28 | MIT/Apache-2.0 | https://github.com/rust-lang/rustc-demangle |
| rustix | 0.38.44 | Apache-2.0 WITH LLVM-exception OR Apache-2.0 OR MIT | https://github.com/bytecodealliance/rustix |
| rustix | 1.1.6 | Apache-2.0 WITH LLVM-exception OR Apache-2.0 OR MIT | https://github.com/bytecodealliance/rustix |
| scopeguard | 1.2.0 | MIT OR Apache-2.0 | https://github.com/bluss/scopeguard |
| serde | 1.0.229 | MIT OR Apache-2.0 | https://github.com/serde-rs/serde |
| serde_core | 1.0.229 | MIT OR Apache-2.0 | https://github.com/serde-rs/serde |
| serde_derive | 1.0.229 | MIT OR Apache-2.0 | https://github.com/serde-rs/serde |
| serde_json | 1.0.154 | MIT OR Apache-2.0 | https://github.com/serde-rs/json |
| slab | 0.4.12 | MIT | https://github.com/tokio-rs/slab |
| smallvec | 1.16.5 | MIT OR Apache-2.0 | https://github.com/servo/rust-smallvec |
| smol_str | 0.2.2 | MIT OR Apache-2.0 | https://github.com/rust-analyzer/smol_str |
| syn | 1.0.109 | MIT OR Apache-2.0 | https://github.com/dtolnay/syn |
| syn | 2.0.119 | MIT OR Apache-2.0 | https://github.com/dtolnay/syn |
| syn | 3.0.10 | MIT OR Apache-2.0 | https://github.com/dtolnay/syn |
| termcolor | 1.4.1 | Unlicense OR MIT | https://github.com/BurntSushi/termcolor |
| thiserror | 1.0.69 | MIT OR Apache-2.0 | https://github.com/dtolnay/thiserror |
| thiserror | 2.0.21 | MIT OR Apache-2.0 | https://github.com/dtolnay/thiserror |
| thiserror-impl | 1.0.69 | MIT OR Apache-2.0 | https://github.com/dtolnay/thiserror |
| thiserror-impl | 2.0.21 | MIT OR Apache-2.0 | https://github.com/dtolnay/thiserror |
| time | 0.3.55 | MIT OR Apache-2.0 | https://github.com/time-rs/time |
| time-core | 0.1.9 | MIT OR Apache-2.0 | https://github.com/time-rs/time |
| time-macros | 0.2.32 | MIT OR Apache-2.0 | https://github.com/time-rs/time |
| tracing | 0.1.44 | MIT | https://github.com/tokio-rs/tracing |
| tracing-core | 0.1.36 | MIT | https://github.com/tokio-rs/tracing |
| ttf-parser | 0.25.1 | MIT OR Apache-2.0 | https://github.com/harfbuzz/ttf-parser |
| unicode-ident | 1.0.27 | (MIT OR Apache-2.0) AND Unicode-3.0 | https://github.com/dtolnay/unicode-ident |
| vst3-com | 0.1.0 | MIT |  |
| vst3-com-macros | 0.2.0 | MIT |  |
| vst3-com-macros-support | 0.2.0 | MIT |  |
| vst3-sys | 0.1.0 | GPLv3 |  |
| widestring | 1.2.1 | MIT OR Apache-2.0 | https://github.com/VoidStarKat/widestring-rs |
| winit | 0.30.13 | Apache-2.0 | https://github.com/rust-windowing/winit |
| x11 | 2.21.0 | MIT | https://github.com/AltF02/x11-rs.git |
| x11-clipboard | 0.9.3 | MIT | https://github.com/quininer/x11-clipboard |
| x11-dl | 2.21.0 | MIT | https://github.com/AltF02/x11-rs.git |
| x11rb | 0.13.2 | MIT OR Apache-2.0 | https://github.com/psychon/x11rb |
| x11rb-protocol | 0.13.2 | MIT OR Apache-2.0 | https://github.com/psychon/x11rb |
| xcursor | 0.3.11 | MIT | https://github.com/esposm03/xcursor-rs |
| xkbcommon-dl | 0.4.2 | MIT | https://github.com/rust-windowing/xkbcommon-dl |
| xkeysym | 0.2.1 | MIT OR Apache-2.0 OR Zlib | https://github.com/notgull/xkeysym |
| zerocopy | 0.8.63 | BSD-2-Clause OR Apache-2.0 OR MIT | https://github.com/google/zerocopy |
| zmij | 1.0.23 | MIT | https://github.com/dtolnay/zmij |
