#!/usr/bin/env python3
"""Write NOTICE.md and THIRD-PARTY-LICENSES.txt for an Albion Road Audio Rust workspace.

The crates listed are the ones linked into the plugins: the normal (non-build, non-dev)
dependencies of the given plugin crates, for x86_64 Linux. Licence texts are copied from
each crate's own source folder (cargo registry or git checkout), so nothing is downloaded.

Usage:  python3 rust_notices.py <workspace dir> <title> <one-line description> <plugin crate> [...]
"""
import json
import subprocess
import sys
from pathlib import Path

LICENCE_PREFIXES = ("license", "licence", "copying", "notice", "unlicense", "copyright")
OWNER = "Albion Road Audio"

# Crates published without a licence file. The copyright lines come from their upstream
# repositories (egui: github.com/emilk/egui, profiling: github.com/aclysma/profiling);
# both are "MIT OR Apache-2.0", and these builds use them under MIT.
NO_FILE_HOLDERS = {
    "ecolor": "Copyright (c) 2018-2021 Emil Ernerfeldt <emil.ernerfeldt@gmail.com>",
    "egui": "Copyright (c) 2018-2021 Emil Ernerfeldt <emil.ernerfeldt@gmail.com>",
    "egui_glow": "Copyright (c) 2018-2021 Emil Ernerfeldt <emil.ernerfeldt@gmail.com>",
    "emath": "Copyright (c) 2018-2021 Emil Ernerfeldt <emil.ernerfeldt@gmail.com>",
    "epaint": "Copyright (c) 2018-2021 Emil Ernerfeldt <emil.ernerfeldt@gmail.com>",
    "epaint_default_fonts": "Copyright (c) 2018-2021 Emil Ernerfeldt <emil.ernerfeldt@gmail.com>",
    "profiling": "Copyright (c) Philip Degarmo <aclysma@gmail.com>",
}
MIT = """MIT License

{holder}

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE."""
YEAR = "2026"


def linked_crates(ws: Path, roots: list[str]):
    meta = json.loads(subprocess.run(
        ["cargo", "metadata", "--format-version", "1", "--filter-platform", "x86_64-unknown-linux-gnu"],
        cwd=ws, capture_output=True, text=True, check=True).stdout)
    pkgs = {p["id"]: p for p in meta["packages"]}
    members = set(meta["workspace_members"])
    nodes = {n["id"]: n for n in meta["resolve"]["nodes"]}
    todo = [i for i in members if pkgs[i]["name"] in roots]
    if len(todo) != len(roots):
        sys.exit(f"crates not found: {set(roots) - {pkgs[i]['name'] for i in todo}}")
    seen = set()
    while todo:
        i = todo.pop()
        if i in seen:
            continue
        seen.add(i)
        for d in nodes[i]["deps"]:
            if any(k["kind"] is None for k in d["dep_kinds"]):
                todo.append(d["pkg"])
    third = [pkgs[i] for i in seen if i not in members]
    return sorted(third, key=lambda p: (p["name"].lower(), p["version"]))


def licence_files(pkg):
    root = Path(pkg["manifest_path"]).parent
    files = [f for f in sorted(root.iterdir()) if f.is_file() and f.name.lower().startswith(LICENCE_PREFIXES)]
    if pkg.get("license_file"):
        lf = root / pkg["license_file"]
        if lf.is_file() and lf not in files:
            files.append(lf)
    # crates from a git checkout (nih-plug and friends) often keep the licence at the repo root
    if not files and "/git/checkouts/" in str(root):
        for parent in root.parents:
            cand = [f for f in sorted(parent.iterdir()) if f.is_file() and f.name.lower().startswith(LICENCE_PREFIXES)]
            if cand:
                files = cand
                break
            if (parent / ".git").exists() or parent.name == "checkouts":
                break
    # bundled font licences (epaint_default_fonts)
    if (root / "fonts").is_dir():
        files += [f for f in sorted((root / "fonts").iterdir()) if f.suffix == ".txt"]
    return files


def write(ws: Path, title: str, blurb: str, roots: list[str], extra: str = ""):
    crates = linked_crates(ws, roots)
    rows, texts, missing = [], {}, []
    for p in crates:
        lic = p.get("license") or ("see licence file" if p.get("license_file") else "UNKNOWN")
        src = p.get("repository") or ""
        rows.append(f"| {p['name']} | {p['version']} | {lic} | {src} |")
        files = licence_files(p)
        if not any(f.parent == Path(p["manifest_path"]).parent or "/git/" in str(f) for f in files):
            holder = NO_FILE_HOLDERS.get(p["name"])
            if holder:
                texts.setdefault(MIT.format(holder=holder), []).append(f"{p['name']} {p['version']} (MIT, no licence file in crate)")
            else:
                missing.append(f"{p['name']} {p['version']} ({lic})")
        for f in files:
            body = f.read_text(errors="replace").strip()
            texts.setdefault(body, []).append(f"{p['name']} {p['version']} ({f.name})")

    notice = f"""# {title}: licence and notices

{blurb}

Copyright (C) {YEAR} {OWNER}

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
{extra}
The following crates are compiled into the plugin binaries. Each crate's full licence
text (and copyright notice) is reproduced in `THIRD-PARTY-LICENSES.txt`.

| Crate | Version | Licence | Source |
|---|---|---|---|
""" + "\n".join(rows) + "\n"
    (ws / "NOTICE.md").write_text(notice)

    out = [f"Third-party licences for {title}", "=" * 60, ""]
    for body, users in sorted(texts.items(), key=lambda kv: kv[1][0].lower()):
        out += ["-" * 78, "Used by: " + ", ".join(users), "-" * 78, "", body, ""]
    (ws / "THIRD-PARTY-LICENSES.txt").write_text("\n".join(out))
    print(f"{ws}: {len(crates)} third-party crates, {len(texts)} distinct licence texts")
    if missing:
        print("  no licence file found for: " + "; ".join(missing))
    return missing


if __name__ == "__main__":
    ws, title, blurb, *roots = sys.argv[1:]
    write(Path(ws), title, blurb, roots)
