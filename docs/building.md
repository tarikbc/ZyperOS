# Building ZyperOS

Both devices run ZyperOS on [plastron](https://github.com/tarikbc/plastron),
and plastron's build is what builds ZyperOS for them: it runs `mk/cross.mk`
with its own cross-compiler and libraries, and `mk/payload.sh` to lay out what
goes on the card, from this working tree (its Makefile's `ZYPEROS_SRC`). For
work on a Pixel 2 the same build runs here, against plastron's SDK (`make
px2-payload`, under "On device" below). This Makefile is for that and
everything else. The host needs Docker (for plastron and the
mGBA core) and, for regenerating art, Python with Pillow, plus ffmpeg.

```sh
make check      # every check below, offline (also plain `make`)
make vendor     # the fetched libretro cores -> vendor/ (mk/fetch-vendor.sh)
mk/build-mgba-bridge.sh  # mGBA, built  -> vendor/            (see below)
make native     # host build of the launcher, for working on how it looks
                # (driven from the keyboard: the arrows, Return A, Escape or
                # Backspace B, X, Y, comma and period L1/R1, K and L L2/R2,
                # S START, Shift SELECT, M MENU, - and = VOL-/VOL+, [ and ]
                # brightness, P POWER, Q quits; a new userdata starts in the tutorial,
                # which `--set tour.done=1` skips)
make boot       # regenerate the boot animation
make system-art # compile the console sprite atlases and frame timing tables
make design     # write the design tokens (tools/designdef.py) into DESIGN.md,
                # src/design.h, the mock kit and Wi-Fi File Transfer's colors
```

`make system-art` and `check-systemart` need Python 3.9+ with Pillow 9.1+.
`PYIMG=/path/to/python` selects a suitable environment. Source files and credits
are in `res/system-art/`; the manifest covers every configured system and the
synthetic shelves. The compiler preserves original GIF frame colors, alpha and
delays, deduplicates identical frames, and emits bounded PNG atlases and C frame
tables. It never treats opaque black as a transparency key. The standalone check
compares real GB/GBA frames against their decoded sources and rejects malformed,
oversized, cropped or inconsistent assets. Generated outputs ship directly, so
end users do not need Python. Runtime frame selection performs no image decode.

**mGBA is built rather than fetched, and is meant to stop being.** Every MBC2
Game Boy cartridge segfaults on the buildbot core - Kirby's Pinball Land, Wave
Race, Golf, X, both Final Fantasy Legends - a regression reported as
mgba-emu/mgba#3859 and fixed upstream the same day, but in no downloadable core
because libretro builds mGBA from a fork that has not synced.
`mk/build-mgba-bridge.sh` builds that same pinned tree with only upstream's own
fix applied, hardened to match the official build. Without it the payload
refuses the card rather than shipping one whose Game Boy, Game Boy Color and
Game Boy Advance shelves open onto nothing. Restore the fetch and delete the
script the day that fork syncs.

`make check` runs before every commit, and GitHub runs it on every pull
request (`.github/workflows/check.yml`, on macOS). It builds the host launcher
once, then runs every check at once, as many at a time as there are cores
(`CHECK_JOBS=1 make check` for one after another): a line as each ends, each
one's output in `build-native/check-logs/CHECK.log`, and the whole log of any
that failed at the end. About 46 s on an M3; one after another it was 167 s.
Two runs at once, in two worktrees, do not meet: every check writes its own
files and Hare's checks listen on a port the kernel picks.

Each part can be run alone (`make check-NAME`), and each exists because
something once broke in a way nothing noticed:

| | |
|---|---|
| `check-menus` | what each menu CONTAINS in a given state, with no renderer and no device |
| `check-corner` | the widest status corner clear of the focused card in each direction, with a long title and every tall panel a grid step under it, both themes, as each shot checks its own frame; and MENU > Clock turns over and is saved (builds the host launcher; skips without SDL2) |
| `check-status` | the shelf's status corner: the battery's warnings at 10% and 5% and none on the charger, the signal's steps and `wpa_cli signal_poll` read for them, and the marks drawn as the status lab drew them, probed in both themes (no SDL) |
| `check-audioout` | where sound goes given a cable, a headset and a setting - all eight combinations |
| `check-idle` | the Auto Off clock, including the charger case and the counter wrapping |
| `check-cheevos` | the achievement half: parsing and filtering |
| `check-rahash` | the C and Python ROM hashers agree, over the whole library |
| `check-raset` | the C and Python set converters agree (needs `RA_USER`/`RA_PASS`) |
| `check-artscrape` | two name normalizers agree on every candidate |
| `check-artrun` | whole Box Art runs against a fake network: Replace keeps a cover it cannot better, and a checksum names the ROM in a zip, not the readme beside it |
| `check-hare` | nothing on the file server is reachable without the PIN |
| `check-httpd` | request parsing, including the malformed ones |
| `check-xfer` | upload paths cannot escape the directory they were aimed at |
| `check-db` | a shipped default never overwrites a choice, and the two scopes stay apart |
| `check-stats` | play time is recorded, and a LAUNCH still writes nothing |
| `check-sort` | a shelf sorts the way it says it does, and the same shelf twice the same way |
| `check-bt` | a device name from the air is only ever data; an address is validated |
| `check-pkkeys` | the volume keys over a port: MENU+VOL is the backlight whichever device is read first, the kernel's autorepeat is no step, a hold repeats on the launcher's clock, and the levels saved are the ones pressed |
| `check-systemart` | bounded sprite compilation, original frame colors/alpha/timing, disposal, deduplication, source attribution, catalogue coverage and generated output consistency |
| `check-design` | DESIGN.md's tables, `src/design.h`, the mock kit's `tokens.js` and Wi-Fi File Transfer's colors are what `tools/designdef.py` writes, and every text pair the launcher and the page draw meets its contrast |
| `check-linebreak` | a name too wide for its box breaks balanced, as the port loading screen breaks one: one line when it fits, else the fewest lines and, of those, the narrowest widest line, held to a search of every break; and a marquee's window rests with its ends between letters, never through one or through the bytes of one (no SDL) |
| `check-portkeys` | the bar over a port takes the Appearance word from the device database, the zone from the card's library (one on Date & Time's list, else New York) and the theme file only when the database cannot be read, against scratch databases that are missing, empty, not databases or locked (SQLite, no SDL) |
| `check-zyperrig` | Zyper's moves beside the top menu's heading: still at rest but for his breath and a blink every 3 to 6 s, the opening hop, a success's hop and pant and a failure's cocked head at their times, a reaction that starts from where he is, a press that drops what was booked and one on the way back that does nothing, a held key that starts nothing over, the first blink's window over a thousand seeds and a blink on its own time, and all of it across the clock's wrap (no SDL) |
| `check-about-shots` | the two About pages leave the shelf below the panel unchanged; page counts belong in the heading, in both themes |
| `check-about` | About storage and battery estimates: real filesystem reads, capacity fixtures, missing or malformed readings, overflow, decimal GB and rounded mAh |
| `check-tour` | the tutorial's steps: what each button is to it and which glyph part the hand lights on each device, every step in order and what each waits for, the deaf start of a step for every key, START, every wrong press and its words, every button's name, POWER only in its own step, the reward's hold, the demonstration loops, Zyper's moves (the rig's, the corner's rise from where he is, the hello's pop-in), the little shelf's colors against `config/systems.cfg`, and `tour.done` saved only for a tour that is seen (no SDL) |
| `check-tour-shots` | the tutorial as the launcher draws it, in both themes: Zyper only for the reward and a wrong press and one at a time in the power step and through the finish transition, the check disc, his bubble clear of "START: skip"; and the launcher's first start showing it on a fresh database and not after `--set tour.done=1` (builds the host launcher; skips without SDL2) |
| `check-textfade` | cached text first drawn at partial opacity reaches the same pixels as text first drawn fully opaque, in both themes, through an SDL software renderer with no window |
| `check-zyper` | Zyper as the launcher draws him: in the top ZyperOS menu and no other, his tongue in after a failure, his eyes shut in a blink, and his shadow on Beard, in the menu and on the power-off screen where it goes out with him, never on Salt & Pepper; the host build's shots read back with Python's standard library (builds the host launcher; skips without SDL2) |
| `check-portload` | the port loading screen over a white cover, a black one and a Nintendo DS game with a black one, in both themes: its words at 4.5:1 and its band at 3:1 as the screen reads them back, and neither words nor cover in Zyper's frame (builds the host launcher; skips without SDL2) |
| `check-flip` | both home layouts, generated game covers and Now Playing match a cold start after a theme change; source sprites remain present, and removed settings never restore old layouts |
| `check-portkeys-flow` | The actual portkeys reader and loop with fake syscalls: poll races, pending release, overflow, removal, noise, and clocks; and over a port, the power key and an empty battery (two readings 5 s apart, off the charger), each one USR1 to launch.sh and never one to init. |
| `check-portal` | the File Transfer page in headless Chrome over the real routes and a scratch card: its folders' art, the hidden dot folders, the room, the time left, the handheld's stop and the update over the cable (needs Chrome) |
| `check-rasubmit` | what a refused unlock means for the queue: settled, the account's, or that one row's; no request is made |
| `check-rasignin` | what RetroAchievements sign-in says for a wrong password, a refusal and no answer, and the HTTP status the blocking requests report under it |
| `check-noticeq` | the pop-ups over a game, one after another (src/noticeq.h) |
| `check-sshlink` | USB File Transfer's SSH: what it counts in dropbear's log (sign-ins, wrong PINs, a port it cannot have), names that try to say otherwise, and the shadow it gives dropbear |
| `check-usblink` | USB charges only until asked: a computer is asked about once per plug and a charger never, every choice ends with its screen or its cable, and Developer Mode keeps the link |
| `check-sgdb` | SteamGridDB on the device: the real client and a whole run against a fake API (the key in a header and never a URL, the match, the formats, the refusals), its title and ratio held to tools/sgdb-check.py |
| `check-snapshelf` | when the shelf's cover turns into its video snap and back, on a clock the check moves by hand |
| `check-ss` | whether this build can reach ScreenScraper, and whether an account survives a restart |
| `check-savedlib` | the saved library a start shows first: a listing written, read and finished is the scan, and nothing but a whole file is read as one; four headless starts of the launcher, with a game added before the third, rebuilt in place |
| `check-titles` | scraped display names: the database upgrade, the real parser and runner, the library's fallback, and main.c's refresh hooks |
| `check-search` | Search Games' matching: what a query finds and in what order |
| `check-muselib` | Muse's view of the Music folder: the two shapes it reads, the litter a Mac leaves beside every track, and where an album's cover is kept |
| `check-musequeue` | the order Muse plays in: the last track, the first, a shuffle running out, a track that will not open, and the mode changed under a playing track |
| `check-museart` | album covers from MusicBrainz: the rule that picks the record, and a run on a stubbed network |
| `check-mp4lock` | the play modes, and MP4's lock screen: every button blocked, power and volume too, and L1 + R1 held two seconds the one way out |
| `check-mp4frame` | MP4's video frames between the daemon and the launcher: a writer and a reader on two threads over the same pages, every picture the reader keeps whole |
| `check-controls` | what MENU > Controls says: every button on exactly one page, and rows short enough to sit still; and a game's Controls, for every system in systems.cfg: the core's names through its map and turbo, pages that fit |
| `check-controls-shots` | the in-game Controls pages drawn in both themes, each still and its own; every Controls page measured with the font against the panel (`--controls-fit`); the in-game menu's Controls row; MENU > System Order turned both ways (builds the host launcher; skips without SDL2) |
| `check-logpack` | the logs pack: names and Bluetooth addresses masked, and a whole pack unpacked with the same tar and read back |
| `check-clock` | Date & Time: each row moving only itself, the 12-hour and 24-hour words, the shelf's short clock, and the zone list west to east |
| `check-battery` | a charger change reaches the shared battery cache within 250 ms at the shelf's poll rate |
| `check-lowbatt` | the start with a battery too low to play: which start shows the screen, its hold, POWER, the charger's moment, and the reading that decides the power-off |
| `check-heat` | "Getting hot": the 85 C trip, two readings in a row, the hysteresis under 80 C, five quiet minutes, the poll's clock |
| `check-charging` | the charging screen: the charger's kind, its lines, the time to full through CC and CV, and its rules |
| `check-dsv` | DraStic's DS saves taken over by DSperate: footer off, the chip's bytes kept, an .srm already there never touched, written whole and flushed |
| `check-osdbar` | the volume and brightness bar the launcher, Diatom and portkeys all draw: a segment lit per step and no more, at both scales, and its in, hold and out |
| `check-themefile` | the theme file the launcher writes and Diatom and portkeys read: a missing or nonsense file reads as Beard, the word written reads back, no half-written file is ever seen |
| `check-theme` | which theme is on: MENU > Appearance's three settings, Auto's seven to seven and the minute it may be late by, and the file the bar's other programs read after every kind of change |
| `check-config` | systems.cfg's accent field: six hex digits are that color, anything else is no color at all (CFG_NO_COLOR); and the systems in A to Z order |
| `check-kbnav` | the on-screen keyboard's d-pad: onto and off the action row, the wraps at every edge, and the action keys' labels on each layer |
| `check-randgame` | Random Game's pick: every game as likely as every other, nothing off a shelf, never the last pick again |
| `check-tween` | the menus' focus plate and the give of their list: a press mid-move takes it on from where it is, and held, it is on its row before the d-pad's next repeat |
| `check-menumotion` | a menu's panel opening and closing: its curves and scrim, one panel at a time over a submenu and back, a press that puts it at rest, and a held d-pad that starts nothing |
| `check-menusnap` | a menu's panel kept whole for the next screen to draw it leaving: every row's text copied, its marks kept, nothing past the bounds |
| `check-cardgen` | the card drawn where there is no art: its title reads on every row of its slab in both themes, and a card made for one theme is never shown in the other |
| `check-blur` | Beard's shadows: the box blur keeps its light, reaches exactly as far as the texture is padded, and its nine pieces cover a rect of any size once with no seam |
| `check-roundrect` | the rects a rounded rectangle is drawn with cover the old row-by-row shape exactly once, for every radius and a spread of sizes |
| `check-rowmacro` | no row macro is handed a side effect, and ROW_H reads its row once (the MENU-and-DOWN crash of 2026-11-05) |
| `check-emptyhome` | the empty home's words for every device and port state: the screens' names, each way's button, no address the device does not have, a reason exactly when a way cannot start |
| `check-sysmark` | a system's color as a mark, for every shipped system in both themes: no color is text-dim and lime is accent-ink |
| `check-menushots` | a menu's open, close and swap at their ends as the launcher draws them, byte for byte, and a picture halfway through each (builds the host launcher; skips without SDL2) |
| `check-uisound` | the interface sounds' rule: which press plays what, never on a Bluetooth headset, the setting's four positions, the WAVs the launcher reads, and the mix |
| `check-bgm` | menu music's rules: the tracks, the settings, the shuffle, the history L2 goes back along, the gain, the Menu Music rows, a track's name from its tags and Zyper's pop-up |
| `check-bgm-tracks` | the menu music a fresh card plays: every file in res/bgm/ is the one res/bgm/tracks.json records, under a license a card may ship, credited, Ogg Vorbis at 48 kHz |
| `check-sounds` | every WAV in res/sounds/ is what tools/gensounds.py synthesizes from tools/designdef.py's tones, and there is no other |
| `check-messages` | result holds and the real input loop with a fake clock, and the source guard that keeps every result site off blocking delays |
| `check-tour-previews` | the tutorial's Theme and Home Screen previews in res/tour/ are the launcher's own home screen as it draws today (`make tour-previews` makes them again) |
| `check-tour-colors` | the tutorial across frames: low-alpha first frames and a theme round-trip never poison cached text, details or bars |
| `check-update` | System Update end to end: two fake releases packed and signed with a throwaway key, check, download, a cut download resumed, refused lists and packs, the boot switch and its step back (skips without OpenSSL 3, libcurl or zlib) |
| `check-sysupdate` | what MENU > System Update says in each state, held to the update lab's screens |
| `check-boottheme` | the boot screen's theme words (DESIGN.md 11.21) |
| `check-captive` | Wi-Fi File Transfer's hotspot, its pure half: the DNS answer for every name, the Host check that tells a phone's internet check from the page, the QR code's text and the password |
| `check-qr` | the hotspot's QR code, read back by zbarimg where the host has it |
| `check-home` | the home's choice, labels, motion, cached music, renderer state, bounded assets, worker isolation, worker-drawn cards and missing art, on the launcher's own objects |
| `check-home-shots` | the home screens as the launcher draws them in both layouts and themes: footer inside the inset, the song, a flip, a move (builds the host launcher) |
| `check-games` | the games row: the aspect, the wrap, tiny lists, rapid reversals and bounded jumps, on the launcher's own objects |
| `checkmark` | src/zypermark.h, Zyper as triangles, is what tools/zyperdef.py generates |
| `check-installer` | ZyperOS Installer's version is ZyperOS's, and its page wears this repository's colors, fonts and Zyper (its Rust tests run in CI) |
| `check-from-rogue` | tools/from-rogue.sh's table against systems.cfg and against the folders on a ROGUE OS card |
| `check-importers` | the computer-side importers (--meta, --titles) on a scratch card: a title written over a game's details keeps them (builds the host launcher) |
| `check-diatom` | Diatom's own suite (diatom/Makefile's check) on its desktop port with the stub core: the port seam, core facts, achievements, screens, the protocol, the state plane, resident cores (skips without SDL2) |
| `check-simulator` | the simulator's Swift tests: its VNC client, the px2sim link, the state lines and the control socket's commands (skips without Swift) |
| `check-sidelights` | what the launcher tells the side lights' gauge: the lines as plastron's gauge reads them, the FIFO writer that never waits and never dies of SIGPIPE, and when a File Transfer batch is over |
| `check-building-doc` | this table has a row for every check in the Makefile's CHECKS, and none for a check that is gone |
| `check-blog` | zyperos.com's blog, built as Cloudflare Pages builds the site (mk/blog.py): title and description lengths, structured data, every link and picture reaching a file (or a path in `_redirects`, such as `/tip`), every post in the sitemap and llms.txt, and the landing page's colors |

They are offline and need no device. A screen's rows are a pure function of
its state precisely so the first two can exist.

The host build renders exactly what the handheld renders, and can be asked for
a single frame:

```sh
ZYPEROS_HEADLESS=1 SDL_VIDEODRIVER=dummy \
ZYPEROS_ROOT=… ZYPEROS_ROMS=… ZYPEROS_FONT=res/fonts/menu.ttf \
  build-native/zyperos --shot /tmp/shelf.png --screen games
```

`ZYPEROS_HEADLESS` and `SDL_VIDEODRIVER=dummy` keep the shot from opening a
window on the desktop of whoever is using the computer: the window is hidden
and the software renderer draws it. `--shot` sets both itself when the caller
has not, so a shot opens no window whatever the caller does or forgets; the
check scripts set them as well.

`ZYPEROS_FONT` names the Regular face, `menu.ttf`; the title role's SemiBold
is read as `title.ttf` from the same folder, and without one there the title
is drawn in Regular and the launcher says so once on stderr.

`--about [0|1]` draws About: system information on page 0, storage and battery
capacity on page 1. It reads storage from the chosen card; on a host without
battery capacity data those fields show "unknown".

`--tour STEP [MS [DONE]]` draws a frame of the tutorial in place of the shelf:
STEP is `welcome`, `move`, `a`, `b`, `y`, `x`, `menu`, `select`, `vol`,
`bright`, `power`, `finish`, or `skip` for the message a skip leaves over the
shelf; MS into it (2000), its press landed DONE ms ago (none), at a fixed
clock, so the same arguments draw the same picture. The finish includes the
power reward: `--tour finish 0` shows the corner Zyper at entry, `70` shows
him leaving, and `500` shows the large hello after his exit.
`--tour-wrong KEY [MS]`
adds a press of the wrong button KEY MS ago (300), KEY one of `tour_key_name`'s
names in any case: `up`, `down`, `left`, `right`, `a`, `b`, `x`, `y`, `l1`,
`r1`, `start`, `select`, `menu`, `volume`, `brightness`, `power`; and
`--fn-volume` draws the Pixel 2's brightness keys, MENU+VOL, on the host.

`--menu [row]` draws the ZyperOS menu over that shelf (the row by its number
or its label, `--menu Clock`), `--press left|right|a` presses that key on the
row first, through the menu's own handler, and `--slots <n>
[aspect]` draws one frame of the save/load carousel over synthetic game
frames - the two screens that otherwise need a game running on a device before
they can be looked at. On the systems row the menu is the top one, with Zyper
beside its heading.

A shot is at rest unless it asks for a time. `--shot-at <ms>` is the one clock
of every shot that moves: it presses at 0 on a clock held still for the shot,
draws the frames after it as the device would, and takes the one `<ms>` in.
What it presses depends on the rest: with `--menu`, the d-pad onto the menu's
row from the row above; with `--menu --open`, MENU opening the top menu on the
shelf; `--menu --close`, B closing it onto the shelf; `--menu --swap`, A on its
Controls row, the menu leaving and MENU > Controls (`--controls`' page)
arriving; with `--launch`, the zoom into the focused game; otherwise the shelf
stepping one card. Frame strips of a motion are a shot for each time.
`--zyper [works|fails]` is `--open` for Zyper, with a success or a failure at
500 ms: `--menu --zyper --shot-at 1000` is him at rest (1000 is also where he
is without `--shot-at`), 181 the top of the opening hop, and 2681 the middle
of his first blink. `--poweroff` draws the power-off animation's frame
`--shot-at`'s time in (0 without it), in place of the shelf. `--zyper` and
`--poweroff` once took their time as a number of their own; they refuse one
now, and say which flag has it.

`--jump <n>` applies n letter-jumps first (negative for up), so where the
d-pad lands on a real library can be checked without a hand on the device.
`--theme light|dark` draws the shot in that theme whatever
MENU > Appearance has saved, and leaves both the saved setting and
`/tmp/zyperos-theme` alone, so a shot can be run beside a launcher. `--flip`
draws the whole shot in that theme first, then turns to the other one through
Auto's own path and draws it again, which is what `check-flip` compares.
The shelf always runs horizontally. `--status "WORDS"` says what the shelf's
status corner shows, for a host with no radio and no battery: a time `H:MM`
(none for a clock never set), `24h` or `12h`, `wifi=off|idle|1|2|3`, `bt`,
`batt=PCT`, `chg`, and `blinkoff` for a critical battery in its second off;
without it the corner shows what the host has, which is its clock, and a
battery only when `/tmp/zyperos_fake_batt` holds a level. `--gamemenu [row]
[dual]`, on a games shelf, draws the menu over a paused game on a made-up
frame, `dual` with a Nintendo DS game's screen rows, and `--ingame` draws
`--cheevos-screen` over a paused game, as the in-game menu opens it. Every
shot fails (exit 3, a `CORNER:` line) when the status corner meets the
focused card, a games title or a panel, when its clock is not 16 from its
marks, when it draws a clock no `--status` gave, or when `--status` gave it
something to show and none is drawn. In a shot, Date & Time's row shows
`--status`'s time too, or none.
Every shot names the screen and the focused item on stderr, so a sequence of
them reads back as a list of what was actually drawn.

`tools/readme-shots.py` captures the README and website gallery in both themes,
with an isolated demonstration library and optional local artwork. See
[`res/readme/README.md`](../res/readme/README.md) for capture provenance and
the refresh steps.

## On device

A change to ZyperOS goes onto a Pixel 2 as a payload, without a new image:

```sh
make px2-payload                                   # -> build/px2/payload/ZyperOS
tools/px2-deploy.sh build/px2/payload/ZyperOS      # the device on the USB cable
tools/qemu-run.sh deploy -n mine build/px2/payload/ZyperOS   # or a VM (docs/qemu.md)
```

`make px2-payload` (`tools/px2-payload.sh`) builds the launcher, Muse,
portkeys, System Update's program and Diatom from this working tree in
Docker, against plastron's SDK: a read-only volume that plastron's `make sdk`
packs from its last Pixel 2 build, with the cross-compiler and the system's own
SDL2, FFmpeg and ALSA, so what links is what runs. Then `mk/payload.sh` lays
it out with the cores from the main checkout's `vendor/`. The objects stay
unstripped in `build/px2/` and `diatom/build/pixel2/`; the payload's
programs are stripped, as the image's are. Nothing it uses can change under
it, so any number of worktrees build at once, with no lock and no plastron
build volume: about 20 seconds from clean. `PX2_SDK` picks an SDK by name
(`docker volume ls --filter label=plastron.sdk`); without it, the newest.
Make the SDK again in plastron after a change to its libraries.

A whole card image is plastron's: `make` there for the Pixel 2, or `make brick
BRICK_TARGET=zyperos-rebuild` for the Brick. A full `make brick` also writes a
card image.

On a plastron development build, SSH runs over the USB cable with the key
from plastron's `local/authorized_keys`: `ssh root@10.55.0.2` reaches the
Brick and `ssh root@10.42.0.1` the Pixel 2. Copy files with `scp -O`, since no
SFTP server runs, and into `/tmp` first; check that the checksums match on
both sides before you move a file into `/mnt/SDCARD/ZyperOS/`. Release builds
carry no key and run no SSH server.

For the Pixel 2 with nobody holding it, `tools/` has small programs to build
with plastron's toolchain (`aarch64-buildroot-linux-gnu-gcc -O2`, `-pthread`
for the memory test; the SDK has it as `/sdk/bin/aarch64-buildroot-linux-gnu-gcc`
in `docker run --rm -v plastron-sdk-px2-<id>:/sdk:ro plastron-builder`) and run
from `/tmp` as root:

- `px2-shot` saves what the panel shows as a 640x480 PPM, from any program,
  and `px2-shot --fps` counts the frames a second it shows.
- `px2-input down down a` presses the buttons, as the joypad would, and
  `px2-input select+start` presses them together, for an emulator's quit chord.
  `volup` and `voldown` are the volume keys, sent on their own device, and
  `power` the power key on the PMIC's: it turns the handheld off, but in the
  tutorial's power step.
- `px2-sample TID` profiles a thread without perf, by library and, for Mono
  with `MONO_ENV_OPTIONS=--jitmap`, by method.
- `px2-lat` measures at the panel what a button costs: `px2-lat press right`
  times each press to the vertical blank that shows its new frame (`pressr` at
  a random point of the frame, as a thumb presses), `px2-lat hold down 3` holds
  it and counts the frames and how many blanks each stayed up, and `px2-lat
  owner a diatom` times a press to the first frame a process made (a game's
  start; it needs debugfs mounted).
- `px2-catch PID` waits for the process to crash and prints where: the
  signal, its registers and stack as file+offset, for `nm` or `addr2line`
  against the unstripped build. It lets the crash go on afterwards.
- `px2-memtest` checks the RAM with patterns under load and times a memcpy,
  for changes to the memory clock. Stop ZyperOS first, so nothing else uses
  the memory meanwhile.

## Layout

```
src/            the launcher
docs/           the guide, and building
mk/             the device build plastron runs, the payload, the cores
tools/          the boot-animation and card generators, the achievement
                fetcher, and the checks
res/            the boot animation, the system cards and the marks they are
                drawn from, the fonts, Wi-Fi File Transfer's page, and the marks and
                screenshots the README shows
config/         systems.cfg as shipped; the rest is compiled in
sd/             each device's launch.sh, and bt-alsa.sh, as they land on the card
vendor/         the libretro cores: fetched and hash-pinned by
                mk/fetch-vendor.sh, the rest built by mk/build-*.sh
site/           the web page at zyperos.com; mk/site.sh lays it out and
                Cloudflare Pages publishes it from main (tools/site-art.sh
                draws its icons and social card)
```

### portkeys input bounds

Each pass reads every open nonblocking input, regardless of its `POLLIN` flag.
The reader accepts only MENU and volume press or release events into the batch.
It drops other keys and kernel autorepeat events before buffering.
A device gets at most 16 read calls per pass, including interrupted calls.
Each call reads at most 64 raw events.

The batch holds 256 relevant events. The pending queue holds 64 events.
Pending events block repeats. New events can defer an entire sorted suffix.
Pending events otherwise resolve on the next pass, even after a wall-clock change.
A delayed repeat emits at most one step per held key, then waits another 90 ms.

If either queue overflows, portkeys discards the whole batch before applying any step.
The same rule applies when the read budget ends, input disappears, or `SYN_DROPPED` reports lost events.
Portkeys clears pending events and held keys, then reads each input's state with `EVIOCGKEY`.
It waits through `SYN_REPORT` before querying an input with a dropped fragment.
Recovered MENU state affects later presses. Recovered volume holds produce no steps or repeats.
A failed state query removes that input. This policy can lose a press during overload.
It prevents partial volume changes from a batch with unreliable event order.

If monotonic event clocks fail, every input must accept the realtime clock reset.
Portkeys exits before processing keys if any reset fails. It never mixes the two clocks.

`make check-portkeys-flow` extracts the current reader and loop statements at build time.
It uses fake syscalls and hardware output. Linux builds use real evdev headers.
The macOS test uses a small evdev definition for the host build.
The test checks both device orders above the pending and batch limits.
