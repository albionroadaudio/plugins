# Albion Road Audio Reference: licence and notices

Albion Road Audio Reference is a mix reference analyser with profiles, reference capture and a gentle match EQ, by Albion Road Audio.

Copyright (C) 2026 Albion Road Audio

This program is free software: you can redistribute it and/or modify it under the
terms of the GNU Affero General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public License along with
this program (the `LICENSE` file). If not, see <https://www.gnu.org/licenses/>.

**Source code:** the complete source for every released build is this repository,
at the git tag matching the release. Build instructions are in `README.md`.

# Third-party software

These are not stored in this repository. CMake downloads them at build time
(exact versions are pinned in `CMakeLists.txt`), and they are compiled into the
released plugin binaries.

## JUCE 8 (pinned tag 8.0.4)

Copyright (c) Raw Material Software Limited. https://juce.com

Used under the **GNU Affero General Public License v3** (JUCE is dual-licensed
AGPLv3 / commercial JUCE licence; this project uses the AGPLv3 option).
JUCE modules used in these builds include the following bundled libraries, each under
its own licence (see JUCE's `LICENSE.md` and the files it links to):

- FLAC (BSD), Ogg Vorbis (BSD)
- jpeglib (Independent JPEG Group licence), pnglib (zlib/libpng), zlib (zlib)
- HarfBuzz (Old MIT), SheenBidi (Apache 2.0)
- CHOC (ISC) including QuickJS (MIT)
- The JUCE MP3 decoder (enabled for loading reference files; part of JUCE)
- LV2 headers (ISC), pslextensions (public domain)

## VST3 SDK 3.7.12 (bundled with JUCE)

Copyright (c) Steinberg Media Technologies GmbH.

Used under the **GNU General Public License v3** option of its dual licence
(Steinberg VST3 licence / GPLv3). GPLv3 code may be combined with AGPLv3 code
(AGPLv3 section 13).
VST is a registered trademark of Steinberg Media Technologies GmbH.

## CLAP (MIT)

Copyright (c) 2021 Alexandre BIQUE

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
SOFTWARE.

## clap-helpers (MIT)

Copyright (c) 2021 Alexandre BIQUE

(MIT licence text as above.)

## clap-juce-extensions (MIT)

Copyright 2019-2020, Paul Walker

(MIT licence text as above.)
