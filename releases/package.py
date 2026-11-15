#!/usr/bin/env python3
"""Build portable release zips of the Albion Road Audio plugins for the website.

JUCE plugins (vumeter, compressor, slapback, guitar-reverb, vocal-multitool, drums, reference), for each:
  1. configure + build CLAP and VST3 in <project>/build-release with
     -DALBION_PORTABLE_LINUX=ON (static C++ runtime, old glibc symbol versions)
  2. check the binaries need at most glibc 2.35 and no system libstdc++
  3. stage CLAP/, VST3/, README.md (+ docs/), INSTALL.txt, LICENSE, NOTICE.md and
     SOURCE/ (the exact source tree the binaries were built from, for the AGPL)
  4. strip debug/unneeded symbols and zip it

Rust plugins (the Surgical EQ series in rust/surgical-eq, DRS Grooves in rust/drs-grooves):
  1. cargo xtask bundle in the workspace (release, thin LTO, symbols stripped)
  2. the same glibc / libstdc++ check (weak symbol versions are ignored: the loader
     doesn't require them)
  3. regenerate the workspace NOTICE.md + THIRD-PARTY-LICENSES.txt (rust_notices.py), then
     stage CLAP/, VST3/, the plugin's README.md, INSTALL.txt, LICENSE, NOTICE.md,
     THIRD-PARTY-LICENSES.txt, LICENSE-Liberation.txt (embedded fonts) and SOURCE/
     (the whole workspace, without target/)

Usage:  python3 package.py [name ...]      (default: all; names are the keys of PROJECTS and RUST)
Output: releases/dist/AlbionRoadAudio-<Product>-<version>-linux-x86_64.zip (not in git; attach
        the zips to GitHub Releases)
"""
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tarfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent   # the albion-road-audio repo
SCRIPTS = Path(__file__).resolve().parent
OUT = SCRIPTS / "dist"
WEBSITE = "https://www.albionroadaudio.co.uk"
MAX_GLIBC = (2, 35)

PROJECTS = {
    # juce/<folder>: (CMake target,     product name,                       slug for file names, kind)
    "vumeter":    ("VuMeter",         "Albion Road Audio VU Meter",       "VU-Meter",      "effect"),
    "compressor": ("Albion76",        "Albion Road Audio 76 Compressor",  "76-Compressor", "effect"),
    "slapback":   ("AlbionSlapback",  "Albion Road Audio Slapback",       "Slapback",      "effect"),
    "guitar-reverb": ("AlbionGuitarReverb", "Albion Road Audio Guitar Reverb", "Guitar-Reverb", "effect"),
    "vocal-multitool": ("AlbionVocalMultitool", "Albion Road Audio Vocal Multitool", "Vocal-Multitool", "effect"),
    "drums":      ("AlbionDrums",     "Albion Road Audio Drums",          "Drums",         "instrument"),
    "reference":  ("AlbionReference", "Albion Road Audio Reference",      "Reference",     "effect"),
}

RUST = {
    # crate:        (workspace,   product name,       slug for file names, kind,     README inside the workspace, has GUI fonts)
    "kick_eq":     ("rust/surgical-eq", "Albion Road Audio Kick EQ",      "Kick-EQ",     "effect", "kick_eq/README.md",  True),
    "snare_eq":    ("rust/surgical-eq", "Albion Road Audio Snare EQ",     "Snare-EQ",    "effect", "snare_eq/README.md", True),
    "toms_eq":     ("rust/surgical-eq", "Albion Road Audio Toms EQ",      "Toms-EQ",     "effect", "toms_eq/README.md",  True),
    "crash_eq":    ("rust/surgical-eq", "Albion Road Audio Crash EQ",     "Crash-EQ",    "effect", "crash_eq/README.md", True),
    "hihat_eq":    ("rust/surgical-eq", "Albion Road Audio Hi-Hat EQ",    "Hi-Hat-EQ",   "effect", "hihat_eq/README.md", True),
    "bass_eq":     ("rust/surgical-eq", "Albion Road Audio Bass EQ",      "Bass-EQ",     "effect", "bass_eq/README.md",  True),
    "acoustic_eq": ("rust/surgical-eq", "Albion Road Audio Acoustic Guitar EQ", "Acoustic-Guitar-EQ", "effect", "acoustic_eq/README.md", True),
    "guitar_eq":   ("rust/surgical-eq", "Albion Road Audio Distorted Guitar EQ", "Distorted-Guitar-EQ", "effect", "guitar_eq/README.md", True),
    "drs_grooves": ("rust/drs-grooves", "Albion Road Audio DRS Grooves",  "DRS-Grooves", "note",   "drs/README.md",      True),
}

# Inputs for rust_notices.py, per workspace: (title, description, plugin crates)
NOTICE_FONTS = """
## Liberation fonts (SIL Open Font License 1.1)

{who} embeds Liberation Sans, Liberation Mono and Liberation Serif
(Copyright (c) 2012 Red Hat, Inc. and contributors). Licensed under the SIL Open Font
License, Version 1.1; the full licence is in `LICENSE-Liberation.txt`.
"""
WORKSPACES = {
    "rust/surgical-eq": ("Albion Road Audio Surgical EQ series",
                  "The Albion Road Audio Surgical EQ series (Kick EQ, Snare EQ, Toms EQ, Crash EQ, Hi-Hat EQ, Bass EQ, "
                  "Acoustic Guitar EQ and Distorted Guitar EQ) are "
                  "per-instrument EQ plugins by Albion Road Audio.",
                  ["kick_eq", "snare_eq", "toms_eq", "crash_eq", "hihat_eq", "bass_eq", "acoustic_eq", "guitar_eq"], "The GUI",
                  "eqcore/assets/fonts/LICENSE-Liberation.txt"),
    "rust/drs-grooves": ("Albion Road Audio DRS Grooves",
                  "Albion Road Audio DRS Grooves (rock, indie, grunge, punk and metal grooves for the DrumGizmo "
                  "DRS Kit) is a MIDI note-effect plugin by Albion Road Audio.",
                  ["drs_grooves"], "The GUI",
                  "drs/assets/fonts/LICENSE-Liberation.txt"),
}

EXCLUDE_FROM_SOURCE = {".git", ".cache", ".vscode", ".idea", "target", "CLAUDE.md", ".claude"}
CARGO = Path.home() / ".cargo" / "bin" / "cargo"


def run(cmd, cwd):
    print("  $", " ".join(str(c) for c in cmd))
    subprocess.run(cmd, cwd=cwd, check=True)


def version_of(project_dir: Path) -> str:
    m = re.search(r"project\(\w+ VERSION ([0-9.]+)", (project_dir / "CMakeLists.txt").read_text())
    return m.group(1) if m else "0.0.0"


def glibc_needs(binary: Path):
    """Highest glibc version the loader insists on (WEAK version needs are optional), and
    whether the binary needs the system libstdc++."""
    out = subprocess.run(["readelf", "-V", "--wide", str(binary)], capture_output=True, text=True, check=True).stdout
    versions = [tuple(int(x) for x in v.split("."))
                for v, flags in re.findall(r"Name: GLIBC_([0-9.]+)\s+Flags: (\w+)", out) if flags != "WEAK"]
    return max(versions) if versions else (0, 0), "GLIBCXX" in out


def build(project_dir: Path, target: str):
    deps = project_dir / "build" / "_deps"
    cfg = ["cmake", "-B", "build-release", "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release", "-DALBION_PORTABLE_LINUX=ON"]
    # Reuse the sources the dev build already downloaded, but only when build-release is
    # configured for the first time: CMake can't switch an existing build to other sources.
    if (deps / "juce-src").is_dir() and not (project_dir / "build-release" / "CMakeCache.txt").exists():
        cfg += [f"-DFETCHCONTENT_SOURCE_DIR_JUCE={deps / 'juce-src'}",
                f"-DFETCHCONTENT_SOURCE_DIR_CLAP_JUCE_EXTENSIONS={deps / 'clap_juce_extensions-src'}"]
    run(cfg, project_dir)
    run(["cmake", "--build", "build-release", "--target", f"{target}_CLAP", f"{target}_VST3"], project_dir)


def source_tarball(project_dir: Path, dest: Path, stem: str):
    def keep(info: tarfile.TarInfo):
        parts = Path(info.name).parts
        if len(parts) > 1 and (parts[1] in EXCLUDE_FROM_SOURCE or parts[1].startswith("build")):
            return None
        return info
    with tarfile.open(dest, "w:gz") as tar:
        tar.add(project_dir, arcname=stem, filter=keep)


def install_text(product: str, version: str, kind: str, third_party_file: bool = False) -> str:
    third = " (full texts in\n  THIRD-PARTY-LICENSES.txt)" if third_party_file else ""
    where = {"instrument": "load it on an instrument track.",
             "effect": "load it on a track or bus as an effect.",
             "note": "put it in a track's note FX chain\n  (Bitwig: Note FX), before a drum instrument."}[kind]
    return f"""{product} {version}: Linux (x86-64)
{'=' * (len(product) + len(version) + 20)}

Free plugin by Albion Road Audio. {WEBSITE}

REQUIREMENTS
  64-bit Linux with glibc 2.35 or newer (Ubuntu 22.04+, Debian 12+, Fedora 36+,
  Linux Mint 21+, current Arch/Manjaro) and a CLAP or VST3 host
  (Bitwig Studio, Reaper, Ardour, Carla, ...).

INSTALL (just for you)
  mkdir -p ~/.clap ~/.vst3
  cp    "CLAP/{product}.clap" ~/.clap/
  cp -r "VST3/{product}.vst3" ~/.vst3/

  Then rescan plugins in your DAW and {where}
  You only need one format: CLAP is recommended where your host supports it.

INSTALL (all users)
  sudo cp    "CLAP/{product}.clap" /usr/lib/clap/
  sudo cp -r "VST3/{product}.vst3" /usr/lib/vst3/

UNINSTALL
  Delete the .clap file and the .vst3 folder you copied.

LICENCE AND SOURCE CODE
  Free software under the GNU Affero General Public License v3.0 or later
  (see LICENSE). Third-party notices are in NOTICE.md{third}. The complete source code
  for this exact build is in the SOURCE folder. README.md describes the plugin
  and how to build it.
"""


def strip_binary(path: Path):
    subprocess.run(["strip", "--strip-unneeded", str(path)], check=True)


def stage_binaries(stage: Path, clap: Path, vst3: Path):
    if stage.exists():
        shutil.rmtree(stage)
    (stage / "CLAP").mkdir(parents=True)
    (stage / "VST3").mkdir()
    (stage / "SOURCE").mkdir()
    shutil.copy2(clap, stage / "CLAP" / clap.name)
    shutil.copytree(vst3, stage / "VST3" / vst3.name, symlinks=True)
    binaries = [stage / "CLAP" / clap.name] + list((stage / "VST3" / vst3.name).rglob("*.so"))
    for b in binaries:
        strip_binary(b)
        need, cxx = glibc_needs(b)
        if need > MAX_GLIBC or cxx:
            sys.exit(f"ERROR: {b.name} needs glibc {'.'.join(map(str, need))}"
                     f"{' and a system libstdc++' if cxx else ''}; expected <= 2.35 and static C++")
        print(f"  {b.name}: needs glibc {'.'.join(map(str, need))}, no system libstdc++")


def make_zip(stage: Path, stem: str) -> Path:
    zip_path = OUT / f"{stem}.zip"
    zip_path.unlink(missing_ok=True)
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for p in sorted(stage.rglob("*")):
            z.write(p, Path(stem) / p.relative_to(stage))   # keeps the executable bit on the .so files
    print(f"  -> {zip_path} ({zip_path.stat().st_size / 1e6:.1f} MB)")
    return zip_path


def cargo_version(ws: Path, crate: str) -> str:
    for toml in ws.glob("*/Cargo.toml"):
        t = toml.read_text()
        if re.search(rf'^name = "{crate}"', t, re.M):
            return re.search(r'^version = "([^"]+)"', t, re.M).group(1)
    sys.exit(f"crate {crate} not found in {ws}")


_bundled = set()
_notices = set()


def package_rust(crate: str):
    ws_name, product, slug, kind, readme, fonts = RUST[crate]
    ws = ROOT / ws_name
    version = cargo_version(ws, crate)
    stem = f"AlbionRoadAudio-{slug}-{version}-linux-x86_64"
    print(f"\n=== {product} {version}")

    if ws_name not in _bundled:   # bundle every plugin crate of the workspace in one go
        crates = [c for c, v in RUST.items() if v[0] == ws_name]
        run([str(CARGO), "xtask", "bundle", *sum((["-p", c] for c in crates), []), "--release"], ws)
        _bundled.add(ws_name)
    if ws_name not in _notices:
        sys.path.insert(0, str(SCRIPTS))
        import rust_notices
        title, blurb, roots, who, _ = WORKSPACES[ws_name]
        if rust_notices.write(ws, title, blurb, roots, NOTICE_FONTS.format(who=who)):
            sys.exit("ERROR: some crates have no licence text; add them to NO_FILE_HOLDERS in rust_notices.py")
        _notices.add(ws_name)

    bundled = ws / "target" / "bundled"
    stage = OUT / "staging" / stem
    stage_binaries(stage, bundled / f"{product}.clap", bundled / f"{product}.vst3")

    shutil.copy2(ws / readme, stage / "README.md")
    if (ws / readme).parent.joinpath("docs").is_dir():
        shutil.copytree((ws / readme).parent / "docs", stage / "docs")
    for name in ("LICENSE", "NOTICE.md", "THIRD-PARTY-LICENSES.txt"):
        shutil.copy2(ws / name, stage / name)
    if fonts:
        shutil.copy2(ws / WORKSPACES[ws_name][4], stage / "LICENSE-Liberation.txt")
    (stage / "INSTALL.txt").write_text(install_text(product, version, kind, third_party_file=True))
    ws_slug = Path(ws_name).name
    source_tarball(ws, stage / "SOURCE" / f"{ws_slug}-{version}-source.tar.gz",
                   f"albion-road-audio-{ws_slug}-{version}")
    return make_zip(stage, stem)


def package(folder: str):
    if folder in RUST:
        return package_rust(folder)
    target, product, slug, kind = PROJECTS[folder]
    project_dir = ROOT / "juce" / folder
    version = version_of(project_dir)
    stem = f"AlbionRoadAudio-{slug}-{version}-linux-x86_64"
    print(f"\n=== {product} {version}")

    build(project_dir, target)
    art = project_dir / "build-release" / f"{target}_artefacts" / "Release"
    clap = art / "CLAP" / f"{product}.clap"
    vst3 = art / "VST3" / f"{product}.vst3"

    stage = OUT / "staging" / stem
    stage_binaries(stage, clap, vst3)

    for name in ("LICENSE", "NOTICE.md", "README.md"):
        shutil.copy2(project_dir / name, stage / name)
    if (project_dir / "docs").is_dir():
        shutil.copytree(project_dir / "docs", stage / "docs")
    (stage / "INSTALL.txt").write_text(install_text(product, version, kind))
    source_tarball(project_dir, stage / "SOURCE" / f"{slug.lower()}-{version}-source.tar.gz",
                   f"albion-road-audio-{slug.lower()}-{version}")

    return make_zip(stage, stem)


def main():
    OUT.mkdir(exist_ok=True)
    wanted = sys.argv[1:] or list(PROJECTS) + list(RUST)
    unknown = [w for w in wanted if w not in PROJECTS and w not in RUST]
    if unknown:
        sys.exit(f"unknown: {unknown}; choose from {list(PROJECTS) + list(RUST)}")
    zips = [package(f) for f in wanted]
    sums = OUT / "SHA256SUMS.txt"
    lines = {}
    if sums.exists():
        for line in sums.read_text().splitlines():
            h, _, name = line.partition("  ")
            lines[name] = h
    for z in zips:
        lines[z.name] = hashlib.sha256(z.read_bytes()).hexdigest()
    sums.write_text("".join(f"{h}  {n}\n" for n, h in sorted(lines.items())))
    shutil.rmtree(OUT / "staging", ignore_errors=True)
    print(f"\nDone. Checksums in {sums}")


if __name__ == "__main__":
    main()
