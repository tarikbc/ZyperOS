# Third-party notices

Diatom's own code is under the Mozilla Public License 2.0: its own files stay
open, and the license reaches no further than them, so Diatom escapes GPL
inheritance either way, which is a founding goal of the project.

## `src/libretro.h`

The libretro API header, vendored so the build is self-contained.

- **Copyright (C) 2010-2024 The RetroArch team**
- **MIT-style permissive license**, and the header states that its license
  statement *"only applies to this libretro API header (libretro.h)"*.

That scoping is deliberate on libretro's part and is what makes the whole
arrangement work: any-license frontends may host any-license cores, because the
interface between them carries no copyleft.

Full text is at the top of the file.

## `vendor/rcheevos/`

RetroAchievements' implementation of their achievement condition language,
vendored so the build stays self-contained.

- **Copyright (c) 2018 RetroAchievements.org**
- **MIT licensed**, full text in `vendor/rcheevos/LICENSE`.
- From https://github.com/RetroAchievements/rcheevos at `f1417fb` (2026-08-12);
  `vendor/rcheevos/README.md` says what was taken and what was left out.

MIT is the whole reason this is possible rather than a problem: it fits beside
diatom's own license, so the project's founding goal - escaping GPL inheritance - is
untouched. The networking halves (`rc_client`, `rapi`, `rurl`) are deliberately
not vendored; `vendor/rcheevos/README.md` lists every omission and why.

## `vendor/lz4/`

LZ4's block compressor, vendored to pack save states (`src/statefile.c`).

- **Copyright (c) 2011-2020, Yann Collet**
- **BSD 2-Clause licensed**; the license asks a binary to carry it, so its
  full text is below as well as in `vendor/lz4/LICENSE`.
- From https://github.com/lz4/lz4 at v1.10.0 (`ebb370c`, 2024-07-22);
  `vendor/lz4/README.md` says what was taken and why.

```
LZ4 Library
Copyright (c) 2011-2020, Yann Collet
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice, this
  list of conditions and the following disclaimer in the documentation and/or
  other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## Cores

**Diatom ships no cores and has no core list.** It loads whatever shared library
it is handed at runtime. Cores keep their own licenses - commonly GPL - and are
the responsibility of whoever distributes them.

`test/stubcore.c` is diatom's own code: a libretro core that is not an emulator,
so the frontend can be exercised with no third-party binary present.

## Libraries it links

None are carried in this repository or shipped with diatom. It links against
zlib (zipped games), SDL2 (the window, input and sound where SDL drives them)
and, on each device, the system's ALSA and graphics libraries (OpenGL ES and
EGL), all provided by the system it runs on: on the GKD Pixel 2 and the TrimUI
Brick that is plastron, whose package list on a card is
`Licenses/plastron-packages.md`.
