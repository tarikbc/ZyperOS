# Third-party notices

ZyperOS itself - the launcher in `src/`, the tools, scripts and configs, and
the art made for this project - is under the **GNU General Public License,
version 3** (`LICENSE`), with the notice in `NOTICE`. The name ZyperOS, Zyper
and his art, and the logo are trademarks, kept by `NOTICE` and `TRADEMARK.md`.
It includes code by Eric
Reinsmidt from the MIT-licensed versions of
[TortOS](https://github.com/ericreinsmidt/TortOS), which stays under his MIT
notice (`LICENSES/MIT-TortOS.txt`).

ZyperOS Installer, in `installer/`, runs on a computer and is not on the card.
It is under the same GNU General Public License, keeps Eric Reinsmidt's MIT notice
for the TortOS Installer code it includes
(`LICENSES/MIT-TortOS-Installer.txt`), and names the crates it is built from in
`installer/THIRD-PARTY.md`.

Everything else on a ZyperOS card is someone else's work under its own
license. This file names each piece, its source and its license. The card
carries it as `Licenses/ZyperOS.md` (`mk/payload.sh`), next to the notices for
Diatom and for plastron, the Linux system under ZyperOS, and ZyperOS's own
`LICENSE`, `NOTICE` and MIT notices.

---

## Diatom, the emulator that runs the games

- **Origin:** this repository's `diatom/` folder, with its history, built by
  its own Makefile and shipped as `ZyperOS/diatom`; from
  https://github.com/ericreinsmidt/diatom by way of tarikbc/diatom, where it
  lived until 2026-11-04
- **License:** **Mozilla Public License 2.0**; its code from before 2026-10-25
  is also under the MIT License. The `libretro.h` it carries is MIT, under the
  RetroArch team's notice, and so is the part of RetroAchievements' rcheevos
  it carries.
- ZyperOS starts it as a process of its own and talks to it through a Unix
  socket. The launcher does not link to it.

---

## libretro cores (`vendor/cores/`, shipped as `ZyperOS/cores/`)

Every core is the work of its authors. Unless a row says otherwise, the
libretro buildbot (https://buildbot.libretro.com) built it, and
`mk/fetch-vendor.sh` pins it by its sha256. The commit given is the source the
binary names; Beetle PCE Fast's binary names none, so its row gives the commit
that rebuilds it to the same bytes.

| Core | Systems | License | Source |
|------|---------|---------|--------|
| `fceumm_libretro.so` | NES | GPL-2.0-or-later | https://github.com/libretro/libretro-fceumm at 236ccdf |
| `mednafen_pce_fast_libretro.so` | TurboGrafx-16 and CD | GPL-2.0 (Mednafen-derived) | https://github.com/libretro/beetle-pce-fast-libretro at bebe2b1 (no commit in the binary; this one rebuilds the same bytes, `mk/fetch-vendor.sh`) |
| `mednafen_ngp_libretro.so` | Neo Geo Pocket and Color | GPL-2.0 (Mednafen-derived) | https://github.com/libretro/beetle-ngp-libretro at a50d5ac |
| `pcsx_rearmed_libretro.so` | PlayStation | GPL-2.0 | https://github.com/libretro/pcsx_rearmed at 7b0e8f8 |
| `mgba_libretro.so` | Game Boy, Color and Advance | MPL-2.0 | https://github.com/libretro/mgba at e31759b plus mGBA's fix 543a197 (https://github.com/mgba-emu/mgba); `mk/build-mgba-bridge.sh` builds it |
| `snes9x2010_libretro.so` | SNES | **Non-commercial** | https://github.com/libretro/snes9x2010 at 7db129b |
| `genesis_plus_gx_libretro.so` | Genesis, Master System, Game Gear | **Non-commercial** | https://github.com/libretro/Genesis-Plus-GX at b7e79b3 |
| `stella2014_libretro.so` | Atari 2600 | GPL-2.0 | https://github.com/libretro/stella2014-libretro at 7d1361e |
| `prosystem_libretro.so` | Atari 7800 | GPL-2.0 | https://github.com/libretro/prosystem-libretro at 8a88014 |
| `gw_libretro.so` | Game & Watch | zlib | https://github.com/libretro/gw-libretro at dddc9d5 (the hash the binary reports as its own) |
| `fbneo_libretro.so` | Arcade, Neo Geo | **Non-commercial** | https://github.com/libretro/FBNeo at da14132 |
| `picodrive_libretro.so` | Sega CD, 32X | **Non-commercial** | https://github.com/libretro/picodrive at 1890c29 |
| `neocd_libretro.so` | Neo Geo CD | LGPL-3.0 | https://github.com/libretro/neocd_libretro at b1e04c7 |
| `fake08_libretro.so` | PICO-8 | MIT | https://github.com/jtothebell/fake-08 at 814991a, with plorpOS's three patches (MIT, Copyright (c) 2026 Thwonp, `mk/patches/fake08-*.patch`), built by `mk/build-fake08.sh` |
| `dsperate_libretro.so` | Nintendo DS | GPL-3.0-or-later | https://github.com/thwonp/dsperate-lr at 093d50f, the libretro wrapper, with https://github.com/beebono/DSperate v3.0.5 (ddf2910) as its submodule and the wrapper's two patches to it, plus ZyperOS's `mk/patches/dsperate-lr-frameskip.patch` and `mk/patches/dsperate-lr-threads.patch`; built by `mk/build-dsperate.sh`, not the buildbot |
| `flycast2021_libretro.so` | Dreamcast | GPL-2.0-or-later | https://github.com/metallic77/flycast at 603814c (ROCKNIX's flycast2021-lr pin, a fork of libretro's Flycast), plus ZyperOS's `mk/patches/flycast2021-postprocess-render.patch`; built by `mk/build-flycast2021.sh`, not the buildbot |
| `a5200_libretro.so` | Atari 5200 | GPL-2.0 | https://github.com/libretro/a5200 at 40c6f2f |
| `handy_libretro.so` | Atari Lynx | zlib | https://github.com/libretro/libretro-handy at bc55d46 |
| `mednafen_wswan_libretro.so` | WonderSwan and Color | GPL-2.0 (Mednafen-derived) | https://github.com/libretro/beetle-wswan-libretro at 4b01295 |
| `pokemini_libretro.so` | Pokémon Mini | GPL-3.0 | https://github.com/libretro/PokeMini (the binary names v0.60 and no commit) |
| `bluemsx_libretro.so` | MSX2 | GPL-2.0 | https://github.com/libretro/blueMSX-libretro at e3086eb |
| `opera_libretro.so` | 3DO | **LGPL-2.1 and non-commercial** | https://github.com/libretro/opera-libretro at 370d9a2 |
| `mame2003_plus_libretro.so` | MAME | **Non-commercial** (the MAME license) | https://github.com/libretro/mame2003-plus-libretro at 91f2f15 |
| `mednafen_vb_libretro.so` | Virtual Boy | GPL-2.0 (Mednafen-derived) | https://github.com/libretro/beetle-vb-libretro at d5ae54c, built without OpenGL by `mk/build-small-cores.sh`, not the buildbot |
| `vecx_libretro.so` | Vectrex | GPL-3.0 | https://github.com/libretro/libretro-vecx at dccff54, built with its software renderer by `mk/build-small-cores.sh`, not the buildbot |
| `mupen64plus_next_libretro.so` | Nintendo 64 | GPL-2.0 (Mupen64Plus, GLideN64) | https://github.com/libretro/mupen64plus-libretro-nx at f275caf (ROCKNIX's mupen64plus-nx-lr pin), for OpenGL ES 3; built by `mk/build-mupen64plus-next.sh`, not the buildbot |
| `scummvm_libretro.so` | ScummVM | GPL-3.0-or-later | https://github.com/scummvm/scummvm at 7d2709d (v2026.4.0), its own libretro backend, without OpenGL; with https://github.com/libretro/libretro-deps at bab7d25 and https://github.com/libretro/libretro-common at 879c8d5, the backend's pins; built by `mk/build-scummvm.sh`, not the buildbot. Its engine data files and GUI themes (`ZyperOS/scummvm`, copied to `Bios/scummvm`) are ScummVM's, from the same tree |
| `yabasanshiro_libretro.so` | Saturn | GPL-2.0-or-later | https://github.com/libretro/yabause at 39535a6 (its yabasanshiro branch; ROCKNIX's yabasanshiro-lr pin), for OpenGL ES 3 with its ARM64 dynarec; built by `mk/build-yabasanshiro.sh`, not the buildbot |
| `freej2me_plus_libretro.so` | J2ME | GPL-3.0 | https://github.com/TASEmulators/freej2me-plus at bdd5edc, the core and its Java app (`freej2me_plus-lr.jar`, which bundles ObjectWeb ASM, BSD-3-Clause), plus ZyperOS's `mk/patches/freej2me-plus-rumble.patch`, `mk/patches/freej2me-plus-busybox-tar.patch` and `mk/patches/freej2me-plus-close-pipes.patch`; built by `mk/build-freej2me.sh` |

### The Java runtime J2ME games run on

`freej2me_plus_runtime_linux-arm64.tar.gz`, which FreeJ2ME-Plus's core
unpacks into `Bios/freej2me_plus_runtime`, is a Java runtime made with jlink
from Eclipse Temurin 21.0.13+9 (Adoptium's OpenJDK build for aarch64 Linux):
the java.base and java.desktop modules only. OpenJDK is GPL-2.0 with the
Classpath Exception, and the runtime carries its own legal notices in its
`legal/` folder, as jlink copies them. ZyperOS adds one font to it, DejaVu
Sans 2.37 (https://github.com/dejavu-fonts/dejavu-fonts; the Bitstream Vera
Fonts license, with DejaVu's changes in the public domain), for the text Java
draws on a system without fontconfig; its license is beside it,
`lib/fonts/LICENSE-DejaVu`.

### OpenBOR

`ZyperOS/openbor/OpenBOR` on the Pixel 2's card is OpenBOR, a program of its
own rather than a libretro core: https://github.com/DCurrent/openbor at
b8303cc (build 7144, ROCKNIX's openbor pin), built by `mk/build-openbor.sh`.
BSD-3-Clause, Copyright (c) 2003 Roel van Mastbergen & Senile Team and (c)
2004 OpenBOR Team. It links libvpx (BSD-3-Clause, the WebM Project) into
itself; SDL2, libpng, zlib, libogg and libvorbis it takes from the device's
system. The two licenses' notices, conditions and disclaimers ship beside it,
`ZyperOS/openbor/LICENSE` and `LICENSE.libvpx`. The modules it plays are not
part of it.

**snes9x2010, genesis_plus_gx, fbneo, picodrive, opera and mame2003_plus carry
a non-commercial restriction.** They are not open source under either the OSI or FSF
definition and they forbid commercial redistribution outright: a card
carrying them can't be sold, or shipped with a device that is. Hobby
redistribution is what every firmware shipping them relies on. A card without
SNES, the Sega systems, Arcade, Neo Geo, 3DO and MAME carries no such
restriction.

The ScummVM core carries, linked in, the libraries its backend builds from
libretro-deps: zlib, libpng, libjpeg, FreeType, FLAC, libogg, libvorbis,
libtheora, libmad, faad2, libmpeg2, giflib, FriBidi and FluidSynth, and from
ScummVM's own tree Munt (mt32emu), Dear ImGui and its other bundled code,
each under its own terms (GPL, LGPL, BSD, MIT, zlib and the FreeType
License), which ScummVM's `LICENSES/` folder and libretro-deps list in full.
The games themselves are not part of it.

fake08 bundles z8lua and Lua, tac08, utf8-util, simpleini and miniz (MIT),
parts of zepto8 (WTFPL-2), LodePNG (its own permissive terms) and an oval
routine from Stack Overflow (CC BY-SA 3.0), each under its own terms, which
its LICENSE.MD lists in full. It is not Lexaloffle's PICO-8 and is not related
to it.

---

## Runtime libraries

None are on the card. ZyperOS links against libraries plastron's system
carries: SDL2, SDL2_image and SDL2_ttf (with FreeType, libpng and
libjpeg-turbo under them), SQLite, libcurl and OpenSSL for RetroAchievements
and box art, FFmpeg (built without its GPL parts) for MP4's music,
audiobooks and videos, ALSA and glibc. plastron's package list on the card,
`Licenses/plastron-packages.md`, gives their versions and licenses.

The Pixel 2's PortMaster installer (`res/portmaster/pm-install.py`, ZyperOS's
own) runs on the system's Python 3. It downloads **PortMaster**
(https://portmaster.games, MIT, by the PortMaster team) from PortMaster's own
GitHub releases onto the card when the player asks, and PortMaster updates
itself from then on. ZyperOS carries no part of PortMaster: the other files
in `res/portmaster/` are ZyperOS's own settings for it, which launch.sh puts
in PortMaster's folder before each port.

---

## Files ZyperOS carries

- Pixel console icons - original artwork by **BenJelter**, from the
  [Console Icon Pack](https://benjelter.itch.io/console-icon-pack).
  **YOjeremyRaps** added consoles, icons and animations for
  **Pixel UI - Smart Folders v1.4**, the user-supplied pack used here.
  The pack's metadata credits both artists. See
  [res/system-art/README.md](res/system-art/README.md) for asset provenance.
  Imported artwork retains its authorship and is not relicensed by this
  repository's license. The supplied archive contains no separate license
  text; this attribution does not assert additional redistribution rights.
- Nine system drawings in `res/system-art/source` (3DO, Game & Watch, MSX2,
  Vectrex, Neo Geo CD, NAOMI, Atomiswave, Muse, J2ME) are drawn from photos on
  Wikimedia Commons by Evan-Amos, Xxm, Black Squirrel, Jpl752 and Mahcann.
  Each keeps its photos' terms (public domain, CC BY 4.0, CC BY-SA 3.0 or
  4.0); every photo, author and license is listed in
  [res/system-art/README.md](res/system-art/README.md) and the manifest.
- `res/fonts/menu.ttf` and `res/fonts/title.ttf` - **Lexend** Regular and
  SemiBold, by The Lexend Project Authors (https://github.com/googlefonts/lexend),
  from Google Fonts, SIL Open Font License 1.1, with the Reserved Font Name
  "RevReading Lexend"; full text in `res/fonts/OFL.txt`. `menu.ttf` (Regular)
  is the UI face, the in-game menu's, the face of the titles on the cards the
  launcher generates (src/ui.c) and of tools/gensingles.py's cover;
  `title.ttf` (SemiBold) is the title role's (DESIGN.md 4), the file transfer page's
  bold and the face of system and game titles. The ZyperOS name is not set in a font:
  tools/zyperdef.py draws its letters as strokes.
- `res/fonts/zh-menu.otf` and `res/fonts/zh-title.otf` - **Noto Sans SC**
  2.002 Regular and Bold, and `res/fonts/ja-menu.otf` and
  `res/fonts/ja-title.otf` - **Noto Sans JP** 2.001 Regular and Bold, by
  Adobe and Google (https://github.com/notofonts/noto-cjk), from Google
  Fonts, SIL Open Font License 1.1, with the Reserved Font Name "Source";
  full text in `res/fonts/OFL-NotoSansSC.txt` and `OFL-NotoSansJP.txt`. Cut
  by tools/cjkfont.py to the characters ZyperOS writes and the first level
  of GB 2312 or of JIS X 0208's kanji. The UI's faces in 简体中文 and
  日本語, Lexend having neither, and the faces of Chinese or Japanese text
  in any language.
- `src/third_party/minimp3.h` - **minimp3**, an MP3 decoder by Lion (lieff)
  and its contributors (https://github.com/lieff/minimp3 at ea99364), CC0 1.0
  public domain dedication (`src/third_party/minimp3-LICENSE.txt`); and
  `src/third_party/stb_vorbis.c` - **stb_vorbis**, an Ogg Vorbis decoder by
  Sean Barrett and its contributors (https://github.com/nothings/stb at
  d14d46f), public domain or MIT, at the end of the file. Both are compiled
  into the launcher for menu music (`src/bgm_dec.c`, `src/bgm_ogg.c`).
  Menu music plays what the card's `Roms/BGM` holds, and ZyperOS's own
  tracks below only while it holds none.
- `res/ssl/cacert.pem`, shipped as `ZyperOS/cacert.pem` - **Mozilla's root
  certificates**, in the form the curl project publishes
  (https://curl.se/ca/cacert.pem), under the Mozilla Public License 2.0.
  ZyperOS checks HTTPS servers against it; `res/ssl/README.md` gives the date
  it was fetched.

---

## Menu music (`res/bgm/`, shipped as `ZyperOS/res/bgm/`)

Ten free 8-bit tracks, played behind the shelf and the menus while the card's
`Roms/BGM` has no tracks of its own (`src/bgm.h`). Each is its author's work
under the license named, taken from the page linked, and is not relicensed
by this repository's license. `res/bgm/tracks.json` records, for each, the
exact file it was made from (its URL and SHA-256), the page and the license;
`tools/bgm-tracks.py` made the MP3s from those files: decoded to 48 kHz
stereo, silence trimmed from both ends, set to one loudness (-16 LUFS, with
the highest peaks of a few held under -1 dBTP) and encoded as MP3 (LAME, VBR
-V 2). The
CC0 1.0 legal code is in `res/bgm/LICENSE-CC0-1.0.txt`, on the card as
`Licenses/menu-music-CC0-1.0.txt`
(https://creativecommons.org/publicdomain/zero/1.0/).

- **Napping on a Cloud** by **congusbongus**, CC0 1.0,
  <https://opengameart.org/content/napping-on-a-cloud>. Rendered with
  libopenmpt from the author's own MilkyTracker module, `napping on a cloud.xm`.
- **Apple Cider** by **Zane Little Music**, CC0 1.0,
  <https://opengameart.org/content/apple-cider>. From the WAV master.
- **Hush Hamlet** by **Zane Little Music**, CC0 1.0,
  <https://opengameart.org/content/hush-hamlet>. From the WAV master; part
  of the collection Another Bag of Chips.
- **Truth** by **HydroGene**, CC0 1.0,
  <https://opengameart.org/content/8-bit-truth>.
- **Scheatt** by **Spring Spring**, CC0 1.0,
  <https://opengameart.org/content/scheatt>.
- **Ice Cave** by **TAD**, CC0 1.0,
  <https://opengameart.org/content/8-bit-lofi-ice-cave>. The page offers it
  under CC0 or CC BY 4.0; ZyperOS takes it under CC0.
- **Starfall Island** by **John Oestmann**, CC0 1.0,
  <https://opengameart.org/content/minimalworldsii-game-boy-style-album>.
  From the WAV master; part of the album minimal_worlds_ii.
- **Leap** by **nene**, CC0 1.0,
  <https://opengameart.org/content/leap-8bit>. From the WAV master.
- **The Arplands** by **LordZintick**, CC0 1.0,
  <https://opengameart.org/content/the-arplands>. From the WAV master.
- **Bossa Nova** by **Joth**, CC0 1.0,
  <https://opengameart.org/content/bossa-nova>.

---

## Services ZyperOS talks to

Not software on the card, but ZyperOS wouldn't be what it is without them:

- **RetroAchievements** (https://retroachievements.org): achievements, and
  rcheevos, the library Diatom uses to check them.
- **ScreenScraper** (https://www.screenscraper.fr): box art and the names of
  games.
