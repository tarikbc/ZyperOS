# Building ZyperOS

Both devices run ZyperOS on [plastron](../plastron/), in this repository's `plastron/`,
and plastron's build is what builds ZyperOS for them: it runs `mk/cross.mk`
with its own cross-compiler and libraries, and `mk/payload.sh` to lay out what
goes on the card, from this working tree (its Makefile's `ZYPEROS_SRC`). For
work on a Pixel 2 the same build runs here, against plastron's SDK (`make
px2-payload`, under "On device" below). This Makefile is for that and
everything else. The host needs Docker (for plastron and the
mGBA core) and, for regenerating art, Python with Pillow, plus ffmpeg.

```sh
make check      # every check below, offline (also plain `make`)
make vendor     # the fetched libretro cores -> vendor/ (mk/fetch-vendor.sh, from the cores release)
mk/build-mgba-bridge.sh  # mGBA, built  -> vendor/            (see below)
make native     # host build of the launcher (build-native/zyperos): shots, checks,
                # and in a window desk mode, below
make desk       # the launcher in a window as the Pixel 2, a game in it too
                # ("Work on the UI without a device" below; make desk-brick)
make boot       # regenerate the boot animation
make system-art # compile the console sprite atlases and frame timing tables
make design     # write the design tokens (tools/designdef.py) into DESIGN.md,
                # src/design.h, the mock kit and Wi-Fi File Transfer's colors
```

`make system-art` and `check-systemart` need Python 3.9+ with Pillow 9.1+.
`PYART=/path/to/python` selects a suitable environment. The art is ZyperOS's
own, in `res/system-art/original` (its README says how it was made and
exported); it covers every configured system, the synthetic shelves and the
folder. The compiler (`tools/systemart.py`) keeps each design's frame colors,
alpha and delays, and writes for each one its first pose whole with the moving
region of every pose packed under it, a 128-pixel still, and the C frame
tables with each region. The check rebuilds every pose of every design from
the atlas and compares it with the export, and rejects malformed, oversized
or inconsistent exports. Generated outputs ship directly, so end users do not
need Python. Runtime frame selection performs no image decode.

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
request (`.github/workflows/check.yml`, on macOS, and on Linux without the checks
its `CHECKS_SKIP` names, which do not pass there yet). It builds the host launcher
once, then runs every check at once, as many at a time as there are cores
(`CHECK_JOBS=1 make check` for one after another): a line as each ends, each
one's output in `build-native/check-logs/CHECK.log`, and the whole log of any
that failed at the end. About 46 s on an M3; one after another it was 167 s.
Two runs at once, in two worktrees, do not meet: every check writes its own
files and Hare's checks listen on a port the kernel picks. They share the
computer, though: the heaviest checks (`HEAVY` in `tools/check-run.py`:
`check-i18n-shots`, `check-diatom`, desk mode's, Docker's) each take one of
`CHECK_HEAVY_SLOTS` (2) slots first, the same two for every run of yours on
the Mac, while the rest go on; `cat ~/Library/Caches/ZyperOS/heavy-slots/*`
says who holds them (`~/.cache/zyperos/heavy-slots` on Linux,
`CHECK_HEAVY_DIR` to move them; a folder anyone else can write, or a slot
that is a link, is refused). Five runs at once had put the load past 30 and desk mode's
presses were lost (2026-11-20). `python3 tools/check-run.py check-i18n-shots`
runs one heavy check under a slot too.

On CI the macOS suite is four jobs, `check 1/4` to `check 4/4`, one runner
each (`make check CHECK_SHARD=I/4`), and `check` is green when all four are.
`tools/check-run.py --shard` deals the checks out by CI's own times
(`tools/check-times.tsv`, longest first, each to the part with the least), and
`check-i18n-shots` shoots its own part of the languages and handhelds in each
(`--part`: dealt by cost, a Brick run counting as two and a half Pixel 2
runs, each part with English on the handhelds it has, and the first with
English on both for the strings no shot draws). One runner took 32 to 39 minutes, 26 of them `check-i18n-shots`.
After a check is added or gets much slower, write the table again from a
green run: `gh run view RUN --log | python3 tools/check-run.py --times-from -`.

On a fresh Ubuntu 24.04 machine (a container, a VM, a cloud workspace),
`tools/ubuntu-setup.sh` installs what CI's Linux jobs install, with the
GitHub runner's own tools a plain Ubuntu lacks; it installs only what is
missing, so it can run at every start, and a cloud workspace can take the file
whole as its setup script. Then, from a git checkout (`check-np-footer` reads
`HEAD`), `make native`, `make check` with check-linux's `CHECKS_SKIP` and
`make check-device-compile` run there as on CI. In an `ubuntu:24.04` container
on 4 cores the first setup took 2 minutes and a second one a tenth of a
second, and the Linux list passed: 238 checks in 250 s, two at a time; on a
4-core x86_64 cloud VM, as root, 238 in 211 s. The
card image (plastron in Docker, the device SDKs) and anything that talks to a
handheld stay on the Mac.

Each part can be run alone (`make check-NAME`), and each exists because
something once broke in a way nothing noticed:

| | |
|---|---|
| `check-menus` | what each menu CONTAINS in a given state, with no renderer and no device |
| `check-corner` | the widest status corner clear of the focused card in each direction, with a long title and every tall panel a grid step under it, both themes, as each shot checks its own frame; the widest Time Left text in every language fits its room; and MENU > Clock and MENU > Battery Icon turn over and are saved (builds the host launcher; skips without SDL2) |
| `check-status` | the shelf's status corner: the battery's warnings at 10% and 5% and none on the charger, the signal's steps and `wpa_cli signal_poll` read for them, and the marks drawn as the status lab drew them, probed in both themes (no SDL) |
| `check-audioout` | where sound goes given a cable, a headset and a setting - all eight combinations |
| `check-idle` | the Auto Off clock, including the charger case and the counter wrapping |
| `check-taskhold` | long tasks and Auto Off (src/taskhold.h): what a loop says holds the clock for 2 s and lets go by itself, across the counter's wrap, and the real clock does not fire during a task and starts a whole countdown after it |
| `check-saver` | the screensaver's policy (src/saver.h): the delay's ladder, when it starts and what holds it back (a button, music, a long task, a low battery, time away), when it ends on its own (the cable-less cap, a low battery), a clip's fades and timing, which game is next (none of the last twelve), and that Auto Off fires at its own time with it up, down or off |
| `check-cheevos` | the achievement half: parsing and filtering |
| `check-rahash` | the C and Python ROM hashers agree, over the whole library; PlayStation and PSP discs in `.chd`, `.cso` and `.pbp`, and Dreamcast discs in `.gdi`, `.cue`, `.chd` and `.cdi`, give upstream rcheevos's hashes (`tools/fixtures/rahash`, made by `tools/rahash-fixtures.py`; a `.cdi` through Flycast's own reader), and broken ones are refused |
| `check-flacdec` | the FLAC decoder under a CHD's FLAC hunks matches libFLAC's encoder at every setting (needs Homebrew's `flac`) |
| `check-achtab` | the in-game menu's Achievements tab: each state its sentence, the counts line, an achievement's row |
| `check-raset` | the C and Python set converters agree (needs `RA_USER`/`RA_PASS`) |
| `check-artscrape` | two name normalizers agree on every candidate |
| `check-artrun` | whole Scraper runs against a fake network: Replace keeps a cover it cannot better, a checksum names the ROM in a zip, not the readme beside it, libretro comes right after a ScreenScraper miss, ten in a row leave a system to libretro, misses are kept 30 days, and a system with covers off gets none |
| `check-hare` | nothing on the file server is reachable without the PIN; the page is named after its link (`/link.js`: the cable's or Wi-Fi's, never kept by a browser), and a web folder from before that still serves its page; an upload or an update's file of an exact multiple of 128 KiB sent with the page's trailer is stored to the byte, one sent plainly as sent, and one with a trailer it cannot make sense of refused, the file of its name kept and no part-file left; an upload cut halfway goes on from where it stopped to the byte (a wrong offset 409 with X-Upload-Have, another file under the same name, a broken writer taken over, the trailer on a resumed tail, the 30-minute keep on a fake clock, DELETE, File Transfer closed and opened again); a game in a folder of games listed with its console's cover, and a listing of `.media` with no covers named for its pictures |
| `check-httpd` | request parsing, including the malformed ones |
| `check-xfer` | upload paths cannot escape the directory they were aimed at |
| `check-db` | a shipped default never overwrites a choice, and the two scopes stay apart |
| `check-gpuframes` | the systems whose cores need GPU frames (systems.cfg's `needs` lines) have no shelf on a handheld without them (`--device brick`) and keep theirs on the Pixel 2 |
| `check-gpu-upright` | every picture Diatom's GPU ports draw, the right way up on the panel: the shared presenting code (`diatom/port/gles_*.h`) built as each port builds it (`diatom/test/upright_test.c`, each port's `PRESENT_TOP_DOWN` read from its own file) and run on Mesa's llvmpipe with no display, each frame read back as that device's display reads it: the TrimUI Brick's page of fb0 (a texture under a framebuffer object, as `pages_start` makes it) from its first byte, the GKD Pixel 2's window surface (a pbuffer) from its top row, its portrait panel turned. A frame at Nearest the picture to the pixel; at Sharp, Sharp-Shimmerless, CRT, CRT Curved, Scanlines, LCD Grid and LCD No Backlight, and under 100%, its bright quarter top left; a DS frame's top screen at the top; a GPU core's frame, drawn GL's way up and top down, to the pixel; the Border's quarters, a notice, the level bar, On-Screen Stats and the low-battery disc each in its place and nowhere else; a capture of a frame and of a GPU core's frame the right way up. Without the Brick's `PRESENT_TOP_DOWN` every one of them but the captures came out upside down on its pages (issue #419). And the launcher's CRT Curved, drawn from a target texture as the Brick's `bend_tex` is and bent with `sfx_draw_curved` (`tools/gpu-upright-bend.c`), on SDL's OpenGL ES 2 renderer, the Brick's: its bright quarter stays top left. Linux only; elsewhere it says it skipped, and `make check-gpu-upright UPRIGHT_DOCKER=1` runs it in `tools/p8fx-gl.Dockerfile`'s Ubuntu |
| `check-coreopts` | a game's core options: the player's rows, then the device's defaults, then every device's, in the library's order (fast forward's frameskip at each core's automatic value, PPSSPP's own frameskip off on the Pixel 2: a late PSP frame is the frontend's to skip, from the PSP's frameskip line in `config/systems.cfg`; the Saturn's rotating backgrounds kept on the CPU, as the compute shader crashes the core); a row too long or with no tag refused whole; the one-time cleanup of what 1.0.5 seeded, which keeps every row the player changed |
| `check-mp4keys` | MP4's settings under its own name: a handheld's `muse.` settings from before 1.1.0 moved under `mp4.` at its first start, each value kept (the mode, Show, Show's one-time All, the Player Look), a book's and a video's places left as they are; after a step back to 1.0.6 the `mp4.` one kept; a second start moves nothing; no shipped default under either name |
| `check-gamedel` | Delete Game on a scratch card holding a cartridge, a multi-disc set in its folder, a `.cue` with its tracks and an arcade set beside `neogeo.zip`, each with saves, states, a cover, a video and a set: exactly the game's files go, never a neighbour's, never `neogeo.zip`, never a file another game shares; its rows in the library database go and the neighbours' stay; a sheet or a folder that leads out of its folders is refused with nothing touched. Then the real launcher, headless (`--delete-now`): the shelf after, in place, on the next game or the one before, from Favorites, down to the empty home (builds the host launcher; that part skips without SDL2) |
| `check-gamern` | Rename on a scratch card: a cartridge with its saves, states, cover, video, Before, achievement set and library rows, beside a `.gbc` that shares its save (copied, not moved); a `.cue` with its `.sbi`, a disc set's folder and a ScummVM folder; an EmulationStation folder whose pictures, video and `gamelist.xml` entry follow; a change of case; and the names, collisions, arcade sets and ports it refuses with nothing moved |
| `check-esmedia` | EmulationStation's folders and media on a scratch card in three layouts (Knulli's `gamelist.xml` with `images/` and `videos/`, Skraper's `media/`, ROGUE OS's `images/`): each game's cover and video in order, the details as ZyperOS keeps them, the import into the library only for games with no row, Rename's half (the files and the list's entry), the scan leaving ES's folders out of what it left out, the rule for which folder a shelf reads, the one lookup the shelf and the File Transfer page share (`game_media`: the game's own in `.media` first, an empty one not counted, then ES's), and one folder's index read again once forgotten |
| `check-media` | A game's Cover and Video page, what a device cannot show: where a cover came from as its record's line in the library database; Use as Cover and Put Back on a scratch card, the cover there was to `.media/.before` and the records with it, both ways, a copy that fails changing nothing; an MP4's length from its `mvhd`, and crafted MP4s (a box of size 0, a 64-bit size past the file, under its header or negative, a box past its parent or the file, an empty or short `mvhd`, 5000 boxes) that all give none and read nothing out of bounds, built with AddressSanitizer and UBSan where the compiler has them |
| `check-snapsize` | A video snap of any shape (640x480, a Nintendo DS game's 320x480, a Game Boy game's, 534x480, a width of 1, odd ones) published as MP4 publishes it, read into its texture and drawn on the shelf and on a game's Cover and Video page: its own shape where it is drawn, a texture never under 640x480 (the Pixel 2's GPU faulted on narrower ones), nothing out of bounds under UBSan and, on a Mac, Guard Malloc (builds the host launcher's objects; skips without SDL2) |
| `check-corepins` | `mk/fetch-vendor.sh` fetches the cores from ZyperOS's cores release and never from libretro's buildbot, whose files change under the same name (nine of nineteen pins had stopped matching it); the release's tag is no v<number>, so the installer and System Update never take it for a version; each pin is a sha256, each core pinned once and with its source in THIRD-PARTY-LICENSES.md (offline; no vendor/) |
| `check-targets` | every texture the launcher draws into is made `PLAT_TARGET_FORMAT` (src/platform.h), a format SDL's GLES2 renderer keeps unswapped: in an ARGB8888 one a video snap on the Pixel 2 came out with red and blue swapped; `make yuvtarget` shows it with the device's own SDL, in Docker ; and the Pixel 2's screen goes onto the panel opaque, made so and opaque again after a video's frame (left blending after the first MP4 video, whatever the screen held at alpha 0 came out black, an RGB texture among it) (a reading of the source; no SDL) |
| `check-storage` | MENU > Storage's numbers on a scratch card: each part, each game's bytes (what Delete Game gives back) and the bar held to the clusters the files take; a second start reads the cache and lists no folder again, a game added lists its own folder alone, a cache it cannot read is measured again; the walk on its thread ends and stops when asked; Unused Art and Old Save States delete exactly their files. Then the real launcher, headless (`--storage delete-open`): Delete Game's question opens on the page from nothing, never as a swap from the panel before it (builds the host launcher; that part skips without SDL2) |
| `check-stats` | play time is recorded, and a LAUNCH still writes nothing; a program of its own's session handed over (its row open at 0 s, the note) and closed by the monotonic clock, two in one second apart, a clock gone back 0 s, a power cut lost on Recents still, notes that are none refused; and when as Recents and Screenshots say it, "today 09:05", 12-hour, "yesterday" with its clock, "3d ago" |
| `check-sort` | a shelf sorts the way it says it does, and the same shelf twice the same way |
| `check-bt` | a device name from the air is only ever data; an address is validated |
| `check-pkkeys` | the volume keys over a port: MENU+VOL is the backlight whichever device is read first, the kernel's autorepeat is no step, a hold repeats on the launcher's clock, and the levels saved are the ones pressed; and over your PICO-8, SELECT's hotkeys by Diatom's own chord rule (tools/pkhot.c: SELECT then the button, the button just before SELECT, SELECT held long then the button; not after the d-pad, not Quick Save, not the kernel's repeat, not a SELECT a resync found down) and CTRL-R as portkeys writes it to its uinput keyboard (tools/pkkbd.c), into a pipe; and the screenshot (tools/pkshot.c): PICO-8's own 128 x 128 out of a turned and a landscape panel in every Display mode's rect (src/p8draw.h), the cut rows of Zoomed and Edges Cut black, the PNG's chunks and pixels back through zlib, and the shot laid in the pop-up's square with its corners rounded; what PICO-8 reads while portkeys holds the joypad for the hotkeys (`pkhot_out`): every press at once, MENU never, a chord never (the button just before SELECT taken back), SELECT alone given back as it is let go for two turns at least and taken back after, the game's with the d-pad or held past 400 ms; and that pad's writes (tools/pkvpad.c), one event a button that changed and a report, into a pipe |
| `check-systemart` | the system art compiler: each pose rebuilt exactly from its atlas, frame timing, moving regions inside their bounds, the decoded budget, catalogue coverage and generated output consistency; and the art itself against its records (tools/systemart-original-check.py, tools/systemart-symbol-pixels-check.py) |
| `check-design` | DESIGN.md's tables, `src/design.h`, the mock kit's `tokens.js`, Wi-Fi File Transfer's colors and the play page's are what `tools/designdef.py` writes, and every text pair the launcher and the page draw meets its contrast |
| `check-strings` | every string in `res/strings/` in all five languages with English's printf conversions, every ID used and defined, every character in the font that draws it, `src/i18n_ids.h` and `res/web/i18n/` what `tools/i18n.py` writes, and `src/i18n_tables.c` not committed (the build writes it from `res/strings`, so no two pull requests conflict over its offsets) |
| `check-i18n-shots` | the main screens in every language and the pseudo-locale, from the host build, as the Pixel 2 and as the Brick (`--device`; the Brick's MENU is the word on its key, wider than the Pixel 2's three dots, and a row it has not, such as Strength, is said, not failed; a shot that fails is taken once more, and one that then passes is said as flaky with its first reason): no string from `res/strings` cut, broken past its lines or off the screen in any language, and no string's ink within 3 units of a key's glyph or a more mark (`ui_fit_mark`; five older screens listed as known in the tool until their fixes); and a list of what the pseudo-locale's longer words do not fit; one set of scratch cards cloned for each language and handheld, and a pass kept by the launcher's bytes and the verdict's inputs, so the same again passes at once (`ZYPEROS_SHOTS_CACHE=0` shoots anyway) |
| `check-rootswitch` | how the Brick's initramfs (plastron's `brick-rootswitch`) reads the kernel's line: the slot's root partition the boot script names (`zyperos.root=`), the name lookup when there is none or it is not a device, and a developer's `rootupdate=1` only as a word of its own |
| `check-datapart` | where the Brick's first start puts the games partition (its `datapart`), over a made-up `/sys`: right after root B on a 4 MiB boundary as partition 5, nothing on a card without root B, and a games partition already there found by its name |
| `check-brick-closed` | the Brick's start checking its closed files (its `init.d/brick`, `closed_check`) against the lists its image carries, over a made-up card: all there passes quietly; one missing, one cut short, no folder or no list fails and names what is wrong |
| `check-kitbuild` | a test kit's code reaches a kit alone (`src/kit.h`): `mk/cross.mk` adds `ZYPEROS_KIT_BUILD` and the device's kit file only for a VERSION with `-kit`, and nothing else; the files with a kit's hooks have none of it compiled without the define and all of it with it; the host launcher, never a kit, has none; no start script seeds a kit's files |
| `check-kitwalk` | the Brick kit's lights walk (`src/device/brick_kit_walk.h`) on a clock moved by hand: the LEDs among the kernel's names in its order by number, each LED's red, green and blue; the 5 V on before any write and off after the last, what the kernel held lit cleared first, each LED alone at TrimUI's ceiling (60) for its second, the writes 40 ms apart and never two in one pass, a page held up still ending; a stop anywhere turning off what is lit and then the 5 V, and nothing more; the row's words as it goes |
| `check-hwreport` | the log pack's hardware report (`src/hwreport.c`): its lines from a device's facts, `-` for what isn't known, cut at its buffer, and the readers of the kernel's log and the input devices' list it is made from |
| `check-linebreak` | a name too wide for its box breaks balanced, as the port loading screen breaks one: one line when it fits, else the fewest lines and, of those, the narrowest widest line, held to a search of every break; a sentence filled (the Achievements tab's), each line as far as it goes, held to a greedy break, in Chinese too; no line starting with the ">" of a way through the menus ("MENU > Achievements"); and a marquee's window rests with its ends between letters, never through one or through the bytes of one (no SDL) |
| `check-portkeys` | the bar over a port takes the Appearance word from the device database, the zone from the card's library (one this system has, on Date & Time's list or set by its Auto, else New York; never a path) and the theme file only when the database cannot be read, against scratch databases that are missing, empty, not databases or locked (SQLite, no SDL) |
| `check-zyperrig` | Zyper's moves beside the top menu's heading: still at rest but for his breath and a blink every 3 to 6 s, the opening hop, a success's hop and pant and a failure's cocked head at their times, a reaction that starts from where he is, a press that drops what was booked and one on the way back that does nothing, a held key that starts nothing over, the first blink's window over a thousand seeds and a blink on its own time, and all of it across the clock's wrap (no SDL) |
| `check-about-shots` | the two About pages leave the shelf below the panel unchanged; page counts belong in the heading, in both themes |
| `check-about` | About storage and battery estimates: real filesystem reads, capacity fixtures, missing or malformed readings, overflow, decimal GB and rounded mAh |
| `check-tour` | the tutorial's steps: what each button is to it and which glyph part the hand lights on each device, every step in order and what each waits for, the deaf start of a step for every key, START, every wrong press and its words, every button's name, POWER only in its own step, the reward's hold, the demonstration loops, Zyper's moves (the rig's, the corner's rise from where he is, the hello's pop-in), the little shelf's colors against `config/systems.cfg`, and `tour.done` saved only for a tour that is seen (no SDL) |
| `check-tour-shots` | the tutorial as the launcher draws it, in both themes: Zyper only for the reward and a wrong press and one at a time in the power step and through the finish transition, the check disc, his bubble clear of "START: skip"; and the launcher's first start showing it on a fresh database and not after `--set tour.done=1` (builds the host launcher; skips without SDL2) |
| `check-textfade` | cached text first drawn at partial opacity reaches the same pixels as text first drawn fully opaque, in both themes, through an SDL software renderer with no window |
| `check-ds` | the design system's rules (DESIGN.md section 15, tools/ds-check.py): src/ds/ includes nothing of main.c; outside it no foot typed as 704, 714, 720 or 724 at a page_hint, a keyhints_draw or words on the keys' line (ui_cap_y), no Zyper typed at a page's (123, 168), no tile mark or tiles style read off a name (tile_glyph, tiles_are_list) |
| `check-layout-ratchet` | the layout ratchet (DESIGN.md "The system", tools/layout-ratchet.py): each launcher source's typed layout numbers (a layout #define set to numbers, words at a typed height, a rectangle typed in place) held to tools/layout-ratchet.txt, which only goes down; over its line fails, and under it fails until the line is lowered (`--write`); drawings (a handheld, Zyper's face) are not counted |
| `check-flex` | the layout engine (src/ds/flex.c): each rule (padding, gaps, grow and shrink, min and max, justify and align, a wrapping row as keyhints_rows breaks one, whole steps, a kept aspect, a measured text), a thousand random trees that stay in their parents the same way every time, and its time per screen |
| `check-hover` | what an option list's cursor shows (src/ds/hover.c): a show once per choice the cursor comes to, the kept one back on the cursor's return and when the list closes, and `ds_hover_showing` true exactly while a choice that is not kept shows |
| `check-hover-sweep` | Theme's Accent, Light Background and Dark Background on three cards (Pixel 2 light and dark, the Brick): every choice shown and the kept one back with no database row and no file written, the page reading the same after, and the slowest show and frame after it on this machine |
| `check-glyphs` | the marks drawn from strokes (src/ds/glyph.c): every one within 32 strokes and 4 arrowheads, counted as it asks for them, kept or not; and the cache, full, giving the same pixels for a mark drawn again (needs SDL2) |
| `check-zyper` | Zyper as the launcher draws him: in the top ZyperOS menu and no other, his tongue in after a failure, his eyes shut in a blink, and his shadow on Beard, in the menu and on the power-off screen where it goes out with him, never on Salt & Pepper; the host build's shots read back with Python's standard library (builds the host launcher; skips without SDL2) |
| `check-portload` | the port loading screen over a white cover, a black one and a Nintendo DS game with a black one, in both themes: its words at 4.5:1 and its band at 3:1 as the screen reads them back, and neither words nor cover in Zyper's frame (builds the host launcher; skips without SDL2) |
| `check-portout` | the port's latest output at the foot of that screen, with plastron's splash's own code (`splash/src/portout.h`): read as a terminal shows it (`\r`, `\r\n`, backspace, escapes, UTF-8, ZyperOS's own lines passed over, in the middle of a line too), shown once the screen has been up 5 s in a row, drawn over the screen's own box, cut with an ellipsis, turned as the Pixel 2's panel is; and the launcher's letters for it in English and German, both themes, on the foot under Zyper's frame, drawn once and read back the same (builds the host launcher; skips that part without SDL2). `sh tools/portout-check.sh --shots DIR` shoots the screen as a stand-in port's lines change, and at 4 s and 6 s |
| `check-flip` | both home layouts, generated game covers and Now Playing match a cold start after a theme change; source sprites remain present, and removed settings never restore old layouts |
| `check-zyhome` | MENU+START's chord (tools/zychord.h): held a second in either order it fires once, again only after both are let go, and a tap of both does nothing |
| `check-pkover` | portkeys's one picture on the cursor window (tools/pkover.h): the bar alone and the heat notice alone where and as they went before, byte for byte (the turn onto the portrait panel and the window's place), the low-battery disc Diatom's to the pixel (radius 9 at 34 px in, its every span) and its blink (always when low, the even seconds when critical), On-Screen Stats' line where src/hud.h's hud_place puts it in hud_render's pixels in every corner, clear of the disc, Speed left out over a program and no line before a frame is counted, all of them at once in one buffer turned and on a landscape panel (the bar over the line, the disc over all, the width padded to 16), and the frames a second counted from the framebuffer at each blank (60, 30, 0, not known after lost) |
| `check-portkeys-flow` | The actual portkeys reader and loop with fake syscalls: poll races, pending release, overflow, removal, noise, and clocks; and over a port, the power key and an empty battery (two readings 5 s apart at 2%, or three under the Pixel 2's 3.45 V empty voltage at 50%, off the charger), each one USR1 to launch.sh and never one to init; every battery reading passed on for the low-battery disc, the charger with it, and On-Screen Stats' vertical-blank file polled with the keys and read only when ready; over your PICO-8, the joypad's events through the chord rule to one act after the pass (none from another device, with no table, after the d-pad, for a SELECT a resync found down, or for Quick Save), and Auto Off: its time with no key one USR1, the charger holding it, a gap past IDLE_GAP_MS starting it again, the loop looking inside that gap, Never ending nothing; and the pad PICO-8 reads, every joypad event handed to it in order and no other device's, asked each pass, told of a resync and of the joypad gone, and no menu from a MENU let go after MENU+START sent the port home. |
| `check-portal` | the File Transfer page in headless Chrome over the real routes and a scratch card: its folders' art, the hidden dot folders, each game's picture and video on a card with EmulationStation's layouts (Knulli's `gamelist.xml`, `images/` and `videos/`, Skraper's `media/`, ArkOS's `downloaded_images/`) and ZyperOS's own `.media` the same as the handheld's lookup (`portal-serve games`: the shelf's scan and `game_media`), a disc set's folder and a game in a folder of games too, each picture drawn, and a picture or a `gamelist.xml` sent from the page shown on the next listing, a console's `.media` dropped or picked going up with its covers and nothing else hidden, files of an exact multiple of 128 KiB (which Safari over the cable never hears the answer to) sent with the trailer and landing to the byte, the room, the time left, the handheld's stop, the update over the cable, its name after the link it came over, USB File Transfer or Wi-Fi File Transfer, opened over one and then the other, and a drop in the middle of an upload: the file waits for the handheld and goes on to the byte, after a stall too, Stop sending, the give-up and Try again, and a reload with the file picked again (needs Chrome) |
| `check-p2share` | NES player 2's shared file (`src/p2share.h`), Diatom's frames out and p2d's pads in, the writer and the reader on one block: a frame read back whole at its size, a pitch wider than its row, 0RGB1555 and RGB565 widened so full is full, a torn slot skipped, nothing read before the magic or into a short buffer, the three slots in turn, a frame too big refused; the header's offsets the Python checks read it by, and SETPLAYER2's rule for a system's buttons (within the twelve RetroPad bits B to R, the NES's 0x01FD when none is left) and players (2 to 4, else 2) |
| `check-webtheme` | the handheld's theme as the play page's `theme.css` (`src/webtheme.c`): with nothing customised the file gives exactly the values of `play.css`'s own token blocks, for the properties `play.css` lets it set and no others; a custom look moves the accent, Square makes `--corner` 0, dark says `color-scheme: dark`, a buffer too small is refused, and the write leaves a whole file |
| `check-p2ws` | p2d's WebSocket with no socket (`src/p2d/ws.c`, `sha1.c`): SHA-1's vectors, RFC 6455's own handshake key, a server frame's head at 125, 126, 65535 and 65536 bytes, a masked client frame unmasked and one cut short waited for, and an unmasked frame, one over 125 bytes, one without FIN, with RSV1 or of an unknown opcode refused; a close and a ping |
| `check-p2codec` | player 2's frame codec (`src/p2d/codec.c`): 100 frames round trip to the pixel, the first a KEY with its palette and a still one under 200 bytes, a new color sends the palette, more than 256 colors refused and the next a KEY, a full palette history started over, a size change a KEY, a KEY every 120 frames and bad messages refused; writes the fixtures `check-play` reads and prints the encode time |
| `check-p2d` | p2d, player 2's server, on the host against a feed of frames over real sockets: the page, `/` and a phone's internet check sent to it, `/i18n.js` in the handheld's language or English; anything but a WebSocket request on `/p2ws` refused, and any Origin but the page's own; frames 30 a second and more while acked, never more than 2 unacked; the first guest told it is player 2 with the NES's buttons (509) or the share's (`p2d-feed --buttons`), its pad those buttons only, let go after a second of silence and when it leaves; a second told it is taken while the first plays on, and every newcomer told so even in a burst with no room to be told from; with `--players 3` two browsers as players 2 and 3, each its own pad, bit in `guests` and frames (unpacked to the feed's picture), a third taken, a freed port the next newcomer's; a guest silent 5 s with a frame unacked dropped and one that acks kept; pings answered, a message the page never sends closing it; the share removed, its magic cleared or a SIGTERM, the guest told the game ended and p2d gone within a second, and gone with 1 after 5 s with no share; the DNS answer; `p2d-feed`'s buttons, players and `--rgba` frames |
| `check-play` | NES player 2's play page (`res/web/play`) in headless Chrome over the host's p2d and a feed of the codec check's fixture frames, in French: the canvas is the frame it says it drew, past the fixture going round and a periodic KEY; every key and touch reaches the share's pad (X and Right 0x0180, A and Left 0x0140, a thumb rolled from A to B, the d-pad's diagonal), held past p2d's second of silence and 0 let go; the pad drawn from the game's buttons (the NES's 0x01FD until the status names others) and no bit sent outside them; the pad on the screen and clear of the picture, and the picture whole device pixels on common phones on their side (667x375 at 2x, 731x411 at 2.625x, 844x390 at 2x and 3x) and on a laptop (1280x800 at 1x and 2x, 768x720); Picture off holds the canvas still while the acks go on; a second tab is told the game is full and keeps trying, and is player 2 once the first closes; a browser without `DecompressionStream` says it is too old and opens no socket; and the share removed, the page says the game ended and stays so, and a tab told the game is full says it is connecting once p2d has gone (needs Chrome) |
| `check-p2` | the launcher's side of NES Player 2 (`src/p2.c`), on fakes for every process and protocol action: Player 2 offered on the NES's core (`fceumm`) alone, with its buttons (0x01FD) and two players, and only on a device that has it; the cable when a computer is on it, else the hotspot when an adapter is in; the page's address on each link; Diatom's PLAYER2 answer read by its pairs; on the hotspot `SETPLAYER2 on=1 buttons=0x01FD players=2`, the theme file and the hotspot, and p2d (with `--dns`) only once the hotspot is up; on the cable the link, zyperos.local (asked again while it would not start) and p2d at once; a refusal starts nothing and leaves no link to close, and no answer in time is told `on=0` too; the stop in the reverse order after any end of a game (p2d's SIGTERM, SIGKILL a second later if it stays, `on=0`, the share's file, then the hotspot or zyperos.local and the cable's link), a second stop doing nothing; the game's end saying nothing (no notice and nothing to Diatom from a tick after it, nor when the share goes or p2d ends with 0); a notice as each guest's bit comes and goes, and none for a bit past the system's players; a failure the tick meets (p2d or the hotspot gone) said once and stopping only what stops at once; the theme file again on a theme change |
| `check-resident` | the launcher's reader of Diatom's answers (`src/resident.c`), the device stubbed, against a made-up Diatom on a socket of its own: SETPLAYER2's answer read while paused with the picture on the way noted, a game's EXIT before the answer left for the wait loop (which then ends the game) rather than eaten, and no answer after the time given (needs SDL2); MP4 behind a running game told each step of its volume boost as its LEVEL line is read, before the pause or the end, and never a line from before Diatom took the launcher's cells; and the pid file a resident the launcher started again goes in (`plat_resident_note_pid`): launch.sh's own format, written over the crashed one's through a renamed file |
| `check-spawn` | `spawn_detached` (`src/spawn.h`), the start of a program that outlives the launcher (MP4, a resident Diatom started again), with real processes: its own pid back (alive, in a session of its own, not this process's child), a program that cannot be exec'd refused with the exec's errno rather than a pid that names nothing, and the environment, folder and descriptors it gets |
| `check-rasubmit` | what a refused unlock means for the queue: settled, the account's, or that one row's; an unlock's age (`o`) and its signature, against md5 sums from Python; no request is made |
| `check-raoffline` | achievements earned offline (src/raqueue.h) through the store, the queue's worker and the request, with a stand-in for curl: nothing sent with no network and nothing lost; a network back on the shelf sends what waited, dated (`o`, at most 14 days) and with its ROM's hash; the notice counts only what waited; RetroAchievements down keeps it all, tried again after 5 minutes. No request is made (needs SDL2; skips without it) |
| `check-radl` | MENU > Achievements' Download (src/radl.h) against a RetroAchievements of the check's own, the check as curl: a mixed card (a set right, one out of date, one new, an unknown hash, one with nothing to earn, a `.none` RetroAchievements knows now, a game on another console's shelf, a Game Boy library behind a 429, Ports with no console) gets exactly the requests it needs; a second run asks only each system's two and hashes nothing; the network gone mid-run waits and goes on; a stopped run is finished by the next; libraries that never come fall back to one `gameid` a game; a refused account stops the run. The time is the check's. No network |
| `check-dlq` | The downloads' queue (src/dlq.h) and net.c's slots, the check as curl: a slow request on one slot and a quick one on another each end with their own answer, and dropping one leaves the other; two lanes of three slow requests finish in the time of one; while a game runs they wait and go on after it; Stop is its lane's done, once, its request dropped, and POWER stops every lane; a lane below the card's floor stops as full; the input wait is cut only while a lane runs; the corner shows a lane whose page is closed; the step hook runs after every step of each lane, on its slot, the last before its done; a lane started with none running is a new run. No network |
| `check-dl-recent` | MENU > Downloads left and opened again (src/dl_screen.c), the real launcher headless on the demo card with a cover for every other game and a stand-in run on the art lane (ZYPEROS_DOWNLOADS_TEST's `fake=N`): left with B, the lanes stepped behind it and a game (nothing comes in it), the page opened again lists every arrival of the run so far on its first pass, newest first and at most 14, those it had with the very pictures they had, and the rest a moment later; done's wall is the run's newest 14; a run that ended behind the page opens on its own wall, its pictures read again after the renderer went (a game in the fallback); the list goes once its end was seen. Every wait is for an event (the stand-in holding after its Kth, every picture in, K more behind the page, the page's first pass), never a time, so a loaded runner is only slower. Needs `make native` (it runs it) |
| `check-dl-look` | a game's panel in MENU > Downloads looking at the card after a fetch or its Cover and Video page (src/dl_screen.c's `one_look`), the real launcher headless on the demo card (`ZYPEROS_DOWNLOADS_TEST`'s `look=` step): Videos is On card for a game's own snap and for EmulationStation's video, and not for a game with none, an empty snap, or a snap deleted after a look (needs SDL2) |
| `check-cheatsdl` | The cheats download (src/cheatsdl.h) on a zip the check makes: its end and index are read, only the card's collections kept, a name with a folder in it, a hidden one and one bigger than 4 MB refused; a ROM matches its own name, its name with a code type, then its title and region in libretro's loose rule (`(U)` is `(USA)`), never another game that starts the same; a deflated and a stored entry are inflated, checked against their CRC-32 and written byte for byte where the in-game list reads them, a damaged one is refused; `.none`, files and no folder are told apart; a system is counted from the card; deleting the downloaded ones keeps your own file. No network |
| `check-cheats` | A game's cheats with no device (src/cht.h, src/cheats.h): the .cht reader (RetroArch's keys quoted or not, CRLF, a byte-order mark, keys out of order, counts too small and too big, RetroArch's own memory pokes and cheats with no code left out, a long description cut at a character, a text that is no .cht) and the list on a card made of small files (the player's own files first as one group, under NextUI's names too, a game in a subfolder, libretro's files each a group named for its code type, why a game has none, which are on kept as ids that survive a reload and an update, the codes Diatom is handed, the on-first order) |
| `check-signin` | RetroAchievements sign-in against a stand-in for curl: a token signs in; a 401 says the user or password is wrong; a 403, a 503 or a redirect says retroachievements.org would not sign you in, with the number; a reply cut short, too long, or nothing at all is never read as a token; and as curl, the check holds that the password is never on its command line, every file the request makes is new and 0600 from the start and gone after, and the log names the failure and no credential |
| `check-noticeq` | the pop-ups over a game, one after another (src/noticeq.h) |
| `check-badges` | achievement badges (src/rabadge.h) through the real fetch with a stand-in for curl: kept beside the set; one curl for what is missing and nothing else; starting it and drawing 4,000 icons while it runs never wait for it; a badge that has not come is a plain one of the same 64 pixels, never a hole; a 404 page is never kept as a badge; then the plain badge and Zyper as pixels (src/noticeart.h), in both themes, his shadow on Beard only. No network (needs SDL2 and SDL2_image; skips without them) |
| `check-badge-shots` | the unlock notice with Zyper at its right end and its badge, or the plain one where the card has none; a count's notice with the plain one and no Zyper; the set's arrival with neither; the achievements list with a badge in every row, plain until the art is there, and the rows' words not moving when it comes; an achievement's card with its badge centered under the heading, or the plain one, and a long description moving under a still footer; in both themes (builds the host launcher; skips without SDL2) |
| `check-sshlink` | USB File Transfer's SSH: what it counts in dropbear's log (sign-ins, wrong PINs, a port it cannot have), names that try to say otherwise, and the shadow it gives dropbear; Developer Mode's choice between the cable's SSH and SSH over Wi-Fi for every combination of cable, radio, choice and a spent plug (the password never with a radio), dropbear's words for each (`-s` and every address over Wi-Fi, the cable's address alone on the cable), and what counts as a key; over a port, portkeys' SSH keys only on every link and none in the in-game menu |
| `check-usblink` | USB charges only until asked: a computer is asked about once per plug and a charger never, every choice ends with its screen or its cable, and Developer Mode keeps the link |
| `check-devmode` | developer mode on a release image: the unlock hold (L2 and R2 for five seconds, a bar that only rises, and letting go early, another page or holding on starting again), the setting kept in the card's database (and SSH over Wi-Fi, only while Developer Mode is on and off with it), a random one-time password in its alphabet, and only public keys without options taken from the card's key file, once each |
| `check-devices` | the device profiles (src/device/profile.h): each one whole (named, its folders, a 4:3 screen with its glass in millimetres, every control printed once), today's answers for the GKD Pixel 2, the TrimUI Brick and the host's desktop (their battery's empty voltages among them: 3.45 V, 3.40 V and none), and the choice of one by its id, as `--device` makes it |
| `check-devicegen` | `config/device/<id>.mk` and `<id>.system`, which the cross build, the payload and plastron read, are what each device's `src/device/<id>_build.c` says (`make device-files` writes them) |
| `check-model` | the handhelds' 3D models (src/device/td_model.h): every key once, and the GKD Pixel 2's key places, views and kept boxes as the drawing had them before they were data |
| `check-ctl` | the launcher's command channel, the one `px2-open` talks to: the command lines, good and bad; a game's path split into its system's folder and the file, and anything else turned away (outside `Roms`, relative, `..`, a link that leads out); the system found by its folder in either case and the game by its launch path or its disc folder; a core by its name or its Core row's; the socket in a folder in `/tmp`, closed to others, a command in and an answer out, and the port record |
| `check-sgdb` | SteamGridDB on the device: the real client and a whole run against a fake API (the key in a header and never a URL, the match, the formats, the refusals, a key refused with a 401 or 403 when it is entered said so and the one before it kept), its title and ratio held to tools/sgdb-check.py |
| `check-snaps` | MENU > Scraper's settings: covers and videos per system (covers on and videos off to start, a card that had 1.0.4's Fetch with Box Art on still getting them), Videos for (all games, favorites, played) and which games it lets through |
| `check-snapshelf` | when the shelf's cover turns into its video snap and back, on a clock the check moves by hand; the Video view's snap held on its picture under a page or a menu, and the cover row's going back to its cover |
| `check-ss` | whether this build can reach ScreenScraper, and whether an account survives a restart |
| `check-savedlib` | the saved library a start shows first: a listing written, read and finished is the scan, and nothing but a whole file is read as one; four headless starts of the launcher, with a game added before the third, rebuilt in place |
| `check-titles` | scraped display names: the database upgrade, the real parser and runner, the library's fallback, and main.c's refresh hooks |
| `check-search` | Search Games' matching: what a query finds and in what order |
| `check-mp4lib` | MP4's view of the Music folder: the two shapes it reads, the litter a Mac leaves beside every track, and where an album's cover is kept |
| `check-mp4queue` | the order MP4 plays in: the last track, the first, a shuffle running out, a track that will not open, and the mode changed under a playing track; videos' Repeat as Y on a video turns it (Off, One, All and round) |
| `check-mp4art` | album covers from MusicBrainz: the rule that picks the record, and a run on a stubbed network |
| `check-mp4lock` | the play modes, and MP4's lock screen: every button blocked, power and volume too, and L1 + R1 held two seconds the one way out |
| `check-mp4look` | MP4's Player Looks' motion: bars up at once and down on follow at any frame rate, caps held for peak-hold and down with gravity, at rest after, the bands over a look's bars |
| `check-mp4views` | START on MP4's shelf, on the real launcher in desk mode's dummy window (no window opens), on the README's media card: SELECT opens MP4, START let go turns its View Covers, Grid, List and round (never Video), kept as `view.MUSE`; SELECT with START, at once or START first, turns nothing and closes nothing |
| `check-mp4resume` | MP4 back after a power-off: the `mp4.resume` row written and read back (playing or paused, an album's or Shuffle All's queue, the second, the file), a broken one refused; a session counted second by second (a write when a queue starts, every 30 s of playing, at a pause, a new song or a power-off, none while paused); then the real launcher started in desk mode's dummy window on the README's media card with a song's, a book's, a video's and Shuffle All's row: Now Playing paused at its second, the row then saying paused so the next start opens nothing; nothing for a paused row, a broken one or one whose file is gone (which takes it away); and a start that goes back into a game a power-off left running (a Diatom of the check's own on the resident's socket), a video's or a song's row: the game first, at its state with its menu up, MP4 held under it with nothing of its own drawn, and SELECT in that menu Now Playing on it, Paused. Every wait in a start is for an event (the screen it starts on drawn, the screen each key opens drawn, the row saying what it should), never a time or a count of frames, so a loaded runner is only slower; a string drawn is found wherever its line begins (on Linux with no sound card ALSA's complaints can come first on it); a wait that runs out prints the start's own lines |
| `check-covercolor` | an album cover's main color, the cassette's stripe and the vinyl's: one color, a title on a white sleeve, the larger of two, a gradient over a band, grey |
| `check-bands` | the daemon's bands for the bars: each lit by a tone at its own middle, silence dark, the window where the listener is; `build-native/bands-check --song FILE` plays a song through it |
| `check-mp4frame` | MP4's video frames between the daemon and the launcher: a writer and a reader on two threads over the same pages, every picture the reader keeps whole |
| `check-mp4size` | MP4's video picture of any shape (4:3 at 1440x1080, 480x360 and 320x240, 16:9, odd, tiny, the largest, the size changing inside one file) published as the daemon publishes it and read into the player's texture: a texture as wide as the frame's rows (a multiple of 128) and as high as the picture, each plane fitting the Pixel 2's Mesa tile buffer as rows (it ran past it, a crash or a green bottom edge, for 4:3 widths), the picture drawn whole from its own rectangle, its shape kept, nothing out of bounds under UBSan and, on a Mac, Guard Malloc (skips without SDL2) |
| `check-controls` | what MENU > Controls says: every button on exactly one page, and rows short enough to sit still; and a game's Controls, for every system in systems.cfg: the core's names through its map and turbo, pages that fit; the Saturn's page from systems.cfg's own map, L1/R1 Z/C |
| `check-hotkeys` | the hotkeys a player sets: ZyperOS's own four, a key set, the swap that loses none, what is stored and read back, Diatom's RUN form, and the three scopes a game falls back through |
| `check-remap` | the remap rules: Diatom's map language read and written, a button's change and its swap worked from the base, what is stored and loaded, the N64's layer kept, the choices a system offers and its letter layout; the Saturn's buttons from its own map line in systems.cfg (each face cap its letter, C on R1 and Z on L1 as the standalone YabaSanshiro and Beetle Saturn, Face Buttons still By Letter, a player's own pairs kept over the new default) |
| `check-rumble` | Rumble's levels (Off, Low 35%, Medium 65%, Full), the share a game is sent (the level times the system's, `rumble=` on RUN), and what is kept for MENU, a system and a game, each falling back to the next |
| `check-eq` | the equalizer's settings: its presets and their curves, each output's own (the speaker's, the headphones'), Custom's bands kept, a band's step making a preset Custom, the words SETEQ and MP4's EQ carry, and the file a port's plugin reads |
| `check-eqdsp` | the equalizer's five bands on test tones: flat touches nothing, each band lifts and cuts its own tone by 6 dB, a step has no click and ends in 20 ms, the knee never cuts flat, and the cost of a second of 48 kHz stereo |
| `check-eqplugin` | the ports' equalizer plugin (plastron's zyperos-eq) built for this computer and loaded as alsa-lib's ladspa PCM loads it: no file and flat untouched, Voice +3 dB at 1 kHz, the launcher's file followed and a half-written one not taken, its cut (your PICO-8 quiet while MP4 plays) silence after a period and 20 ms, faded, never louder, a stream starting cut silent and cleared the sound back to the bit, and its cost; then on Linux a port's whole chain (`sd/zyperos/eq-port.sh`) opened as SDL 2 opens it, at 22050 Hz mono and stereo, 44100, 48000, 32000 and 11025, over a stand-in for the Pixel 2's dmix with its limits (`tools/eqchain-check.sh`, `tools/pcm-dmixlike.c`), the system's default first to show the stand-in holds (tools/eqplugin-alsa.sh runs it through the handheld's own alsa-lib, by hand) |
| `check-controls-shots` | the in-game Controls pages drawn in both themes, each still and its own; every Controls page measured with the font against the panel (`--controls-fit`); the in-game menu's Controls row; its Display row over a favorite saying the game's own system's mode (needs sqlite3); MENU > System Order turned both ways (builds the host launcher; skips without SDL2) |
| `check-logpack` | the logs pack: names and Bluetooth addresses masked, and a whole pack unpacked with the same tar and read back |
| `check-clock` | Date & Time: each row moving only itself, the 12-hour and 24-hour words, the shelf's short clock, the zone list west to east, Auto before its west end, and the list A on Time Zone opens (Auto first with what it found, then every zone, the tick and the cursor on the choice that is on) with its rule: a zone picked by hand turns Auto off |
| `check-autozone` | Date & Time's Auto (src/autozone.c): ipwho.is's reply read, and a refusal, a rate limit, a page that is not JSON, a reply cut short, a name that is a path and offsets no zone has not; the zone it comes to (the name when the system has it, else the offset's: Etc/GMT-N, or the city of an offset off the hour, each in the host's zone database); the row's words; and when it asks: once a connection after the network settles, again on a failure up to four tries, not within three hours of an answer, never with the clock unset, and again when Auto is chosen again; and 1.0.5's default on scratch library databases: a new card on Auto, a card from before switched on once with its zone kept until the first answer, a zone picked by hand after that kept |
| `check-battery` | a charger change reaches the shared battery cache within 250 ms at the shelf's poll rate |
| `check-battery-eta` | the time left on the battery: the estimate from how long a percent takes to fall, against a simulated gauge at several loads, across the clock's wrap, a change of load, a stall, the cable and a gauge that corrects itself; and the corner's rounding |
| `check-dslang` | the Nintendo DS's language: each language MENU > Language has maps, in Auto, to the DS's own or to English (Português (Brasil) and 简体中文 have none), the Game Language row's eight choices round trip through the database and go round both ways, the core's option and its values are the patch's, and the launcher sends it first with every DS game and with no other; the row is on the DS shelf's menu in every language with nothing cut, and Left, Right and A go round it, saved (builds the host launcher for the second half; skips without SDL2) |
| `check-pacing` | Timing, a system menu row: its two choices, Smooth and Exact, the words the database keeps and RUN's `pace=` sends for them (`smooth`, `exact`), a word that is none being the system's own default from `config/systems.cfg`, the database key by the system's tag (`pace.GBA`), and Left and Right going round. The row on the menu is `check-menus`', RUN's keys and which shelves have the row `check-config`'s, and what Diatom does with `pace=smooth` Diatom's `check-displaylock` |
| `check-sgbmode` | Super Game Boy, a system menu row: its two choices, Off and Its Games, the words the database keeps and RUN's `sgb=` sends for them (`off`, `games`), Off for a word that is none, the database key by the system's tag (`sgb.GB`), Left and Right going round, and the shelves that have it (the Game Boy's and the Game Boy Color's, on mGBA). What Diatom does with `sgb=` is Diatom's `check-sgb` |
| `check-border` | Border, a display row: its kinds (Off, Blur, Your Own) and the words kept, Off for a word that is none, the database key by tag (`border.GB`), what the row offers and steps round (no Blur where the device has none, no Your Own without a picture), a kept kind it cannot show reading Off, whether each display mode leaves room around a picture on the Pixel 2's and the Brick's surfaces, and NextUI's folder tags. What Diatom draws is Diatom's `check-border` and `check-border-e2e` |
| `check-handframe` | the handheld frames (the three Game Boys) on 640x480 and 1024x768: Handheld where Sharp Pixels puts the game, Whole a step smaller with the lens in the middle and never under 2x, one game pixel an LCD dot, straight edges on whole pixels (each side of an edge exactly one color), the Game Boy Color's lens's bottom arc where its model has it, every part inside the body, the Color's six colors named. `python3 tools/handframe-sheet.py OUT` draws every frame under its model's outlines for the eye |
| `check-corestate` | a game's resume when its auto state was left by the shelf's other core (the N64's ParallelN64 and Mupen64Plus-Next): Diatom's state header read back, version 1 and the packed version 2, and the start fresh or resume (src/corestate.h) |
| `check-dsresume` | a Nintendo DS game's resume by its language: the firmware's language as DSperate's direct boot takes it, the language a game starts in for each option, the `.lang` beside its auto state, and a game left in another language started fresh (src/dsresume.h) |
| `check-core-stamp` | the patched cores' stamps (mk/core-stamp.sh): a stamped core passes; a core built before a patch changed, one changed after its stamp, one with no stamp and a patch no core claims each fail, with what to run |
| `check-core-room` | the libretro cores systems.cfg names against the resident Diatom's room for them (`DIATOM_MAX_RESIDENT`): more cores than that, and the last in folder order would start every game in the one-off Diatom (1.0.5 had 34 for 32) |
| `check-px2-perf` | px2-perf's arithmetic: percentiles as a spreadsheet gives them, the frame-time spread from the times new frames were seen, a process's CPU ticks from a /proc stat line whose name has spaces and parentheses, and the speed against the clock from two of Diatom's now files, refused across two games or a Diatom started again |
| `check-px2-snap` | px2-snap on a scratch card: a game's .srm, states, slots and pictures, fbneo's, a NAOMI game's and a ScummVM game's own files, never another game's that starts with its name; its play sessions, favorite and Pak through the host launcher's `--game-rows`, and no other game's; a test's changes undone byte for byte, the files it made removed; an open game, a name that is a path, a name taken, another game's rows and a launcher with no `--game-rows` refused or said (builds the host launcher; skips without SDL2 or sqlite3) |
| `check-px2` | tools/px2 with a stand-in for ssh: wrong uses refused before the lock or the device, the test card and a VM's card go ahead and any other only with `--real-card`, the lock's owner file, a lock held refused or waited for, the lock let go after a failure, a TERM and a Ctrl-C, and each argument one word on the device; `deploy` and `restore` against a card that is a folder here: only what changed sent, the card's own files kept once however many deploys, a restore that puts them back and deletes what was added, ZyperOS restarted or, for a `launch.sh`, the device |
| `check-powerhold` | MENU > Power Button's rule: a press turns the handheld off on Press, a hold of one second on Hold, once per time down, a hold the charging screen took never, even one it took after the second, and the pill's segments and the waits that move them |
| `check-homesys` | MENU > Display > Home Systems' setting: the word of the shelves that are off, read and written back, a hand edit's spaces and repeats, whole tags only when it is cut |
| `check-collect` | Collections on the card (src/collect.h): the lists read in their every form and kept as written, added to and taken from, made, renamed and deleted, a game's rename and delete followed into every list |
| `check-shotname` | a screenshot's file name (src/shotname.h): read as the Screenshot hotkey writes it, `<Game> 2026-11-18 21.40.05.png`, OpenBOR's own and every other file left alone, a time the calendar has not got refused, the time kept by the calendar alone and read back to the same name to 2100, and the Screenshots shelf's three orders, the newest first in each |
| `check-screencard` | Recents' Last Screen card (src/screencard.h): the resume picture scaled up whole toward its height, the cover in its lower left corner over its shadow, a wide box held to its share, no picture no card |
| `check-lowbatt` | the start with a battery too low to play: which start shows the screen, its hold, POWER, the charger's moment, and the reading that decides the power-off |
| `check-battempty` | the battery run flat while the handheld runs (src/battempty.h), as `battery_empty` and portkeys read it every 5 s: the level at 2% or under on two readings in a row, as before, or the voltage under the device's empty voltage (3.45 V on the Pixel 2) on three whatever the level says (a worn battery's 43%); never on the cable, never with no battery read; one or two readings under with one over between, a reading with no voltage, a voltage under 2.7 V (a bad reading) and a device with none each start the run again; the level's rule named first when both hold, and a long run never wraps |
| `check-heat` | "Getting hot": the 85 C trip, two readings in a row, the hysteresis under 80 C, five quiet minutes, the poll's clock |
| `check-charging` | the charging screen: the charger's kind, its lines, the time to full through CC and CV, and its rules, POWER held a second (hold-power) to start |
| `check-dsv` | DraStic's DS saves taken over by DSperate: footer off, the chip's bytes kept, an .srm already there never touched, written whole and flushed |
| `check-osdbar` | the volume and brightness bar the launcher, Diatom and portkeys all draw: a segment lit per step and no more, at both scales, and its in, hold and out |
| `check-boost` | the boost past the top of the volume and brightness (src/boost.h): the hold and the three presses that open it, the charge draining as it filled, the held key stopping, the way back down, and where it may not open |
| `check-levels` | the launcher's level keys (src/levels.c) as the screens call them, the device, the database and MP4 stubbed: with MP4 heard behind the home, a shelf or a menu, its boost works there as in the player (the cells on the bar, the hold and the three presses that open it, VOL- emptying a cell before the level moves, each cell told to MP4 and the ports' file); over a paused game (the in-game menu, a port's menu) the game's the same way, VOL- emptying one cell; a running game's steps told to MP4 at once (`levels_game_boost`); with neither, the menus' own (needs SDL2's headers) |
| `check-hud` | On-Screen Stats (src/hud.h): the line for each item, the speed as a multiplier and none at full speed (0.98x-1.02x), rounded and clamped; every style's place at both scales over every set of items, inside the screen, clear of the level bar and the low-battery disc, and still as the numbers change; its colors; and Diatom's copies the same |
| `check-shaderdemo` | the Shader and Strength lists' pictures (res/shaderdemo): each one what tools/shaderdemo.c makes now, pixel for pixel; one for every filter the Shader rows offer and every Strength step, on each panel; and Diatom's GLSL and the Brick's Sharp-Shimmerless on its CPU unchanged since they were made (res/shaderdemo/SOURCES). tools/shaderdemo-device.sh sets them beside a Pixel 2's own shots |
| `check-shaderpic` | the Shader and Strength lists' pictures as the launcher loads them: every filter and Strength step on each panel a texture with an alpha channel, opaque, since the Pixel 2's SDL (2.32, GLES2) draws an RGB888 texture into the screen's target with alpha 0 and the panel's copy then shows it black (a computer's SDL writes 255, so no shot sees it); a picture not on the card NULL, said once and not read each frame; on the launcher's own objects |
| `check-screenfx` | ZyperOS's own screens' shader (src/screenfx.h): each old screen's picture within one level of what tools/shaderdemo.c, Diatom's GLSL in C, makes of a white picture at a game's size, every pixel, at 100%, 50% and 10%; on the Pixel 2's 640x480 Scanlines dims every other row, the LCD's grid a row and a column of two, the CRT shows its lines and its grille, CRT Curved is black in its corners; the masks kept (src/screenfx_draw.c): each made once however often it is asked for, six kept, the one used longest ago let go, none for Off; and the time a picture takes on this machine |
| `check-scale` | a row's graded bar (src/ds/scale.h): its value reads back as written and keeps its range, a row's words or a bad kind or range is never a scale, and the knob is where the value is, with its ring round it and zero's mark away from it, in both themes, on a software renderer |
| `check-dmodefit` | the Display list's pictures: each display mode's rect as the launcher works it out (src/dmodefit.c), against Diatom's own arithmetic (diatom/src/scale.c), over every mode, the sizes cores report, their shapes and the handhelds' surfaces; and Square Pixels' own sizes, the NES's, the SNES's, the Game Gear's and the PC Engine's square at full height on both handhelds (an NES game's 256x224 on the Brick: 878x768), and a wide game's at full width |
| `check-themefile` | the theme file the launcher writes and Diatom and portkeys read: a missing or nonsense file reads as Beard, the word written reads back, no half-written file is ever seen |
| `check-theme` | which theme is on: MENU > Appearance's three settings, Auto on the Night Hours as they ship (21:00 to 7:00) and on any hours to the minute (round midnight too), the row's three round both ways, the minute it may be late by, and the file the bar's other programs read after every kind of change |
| `check-look` | the player's look (MENU > Display > Theme): every accent with every background tone, in both themes, held to the contrast each color is owed; the look as it ships is the tokens to the byte, and its words read back |
| `check-config` | systems.cfg's accent field: six hex digits are that color, anything else is no color at all (CFG_NO_COLOR); the systems in A to Z order; the frameskip lines (`frameskip`, a tag, `late`): RUN's `frameskip=late` for a system with one screen, once after `screens=2` for two, no other word, and the PSP the one shelf with one; and the `es` lines: each name on one shelf, none a shelf's own folder in other capitals, every line read whole, and `playstation`, every ROCKNIX folder for a system ZyperOS has and the other EmulationStation systems' read on their shelves |
| `check-bios` | a shelf's BIOS as its core judges it: every disc bios name in systems.cfg has a size, and so has every name a type of file needs (a bios line with extensions: an .fds image needs disksys.rom, a cartridge beside it nothing), and a 0-byte, short or long file in Bios/ is refused and named in the core's order (with the one delete that fixes it when a good later name is there), while a right-size one starts |
| `check-gamefile` | a game's own file before its core: a 0-byte file of any kind is refused as empty (a 0-byte PSP .iso crashed PPSSPP), a zip, 7z, CHD, CSO, PBP or iso under its kind's least size as incomplete, while a ROM, cue or m3u is judged by being empty alone; what the shelf says, in two languages |
| `check-startfail` | what the shelf says when a core would not load (src/startfail.h): another machine's build, a library it needs, a library too old, a function nothing has, or nothing more known, in each of the eight languages with the core's file name and the name dlerror gave, a name too long for the panel cut with an ellipsis, an error of another code left to the log, and French's no-break space before its colon (no SDL); Diatom's side is `check-corefail` (the ELF header and dlerror's text as glibc, musl and macOS write it) and `check-corefail-e2e` (the real dlopen) |
| `check-kbnav` | the on-screen keyboard's d-pad: onto and off the action row, the wraps at every edge, and the action keys' labels on each layer; a name's keyboard Shift for its first letter only (kb_layer_typed) |
| `check-randgame` | Random Game's pick: every game as likely as every other, nothing off a shelf, never the last pick again |
| `check-shelfdir` | Folders on a shelf: what a folder shows, its folders first by name with their games counted at every depth, then its games; a disc set in a folder a game there |
| `check-tween` | the menus' focus plate and the give of their list: a press mid-move takes it on from where it is, and held, it is on its row before the d-pad's next repeat |
| `check-menumotion` | a menu's panel opening and closing: its curves and scrim, one panel at a time over a submenu and back, a press that puts it at rest, and a held d-pad that starts nothing |
| `check-menusnap` | a menu's panel kept whole for the next screen to draw it leaving: every row's text copied, its marks kept, nothing past the bounds |
| `check-cardgen` | the card drawn where there is no art: its title reads on every row of its slab in both themes, and a card made for one theme is never shown in the other |
| `check-cardart` | a game's card with no art (src/cardart.h): each system's era texture, a name's ground always its own, a grey system still given a color, the name at 5:1 or better on its band over the brightest ground any system makes, and how a name breaks (one size, balanced lines, no lone "2", " - " splitting, an ellipsis only when cut) |
| `check-blur` | Beard's shadows: the box blur keeps its light, reaches exactly as far as the texture is padded, and its nine pieces cover a rect of any size once with no seam |
| `check-focusgeo` | Highlight's marks on whole panel pixels (src/focusgeo.h, DESIGN.md 11.47): every pixel edge drawn at the unit that lands cleanest through the Pixel 2's linear scaling of the 1024 x 768 picture (edges 0, 2 and 3 of each 5 pixels exact, the others a tenth out at most), the focused cover, its ring's gap and the ring, and Bar's bar on whole pixels, a cell of any shape moved two pixels at most, a unit a pixel (the Brick) changed by none of it, and the step back of the tiles out of focus |
| `check-gridrule` | the grid's rule (DESIGN.md 11.47): the shape a shelf takes from its art for every common shape, a shelf mostly of one shape, an even mix (square), cut-outs alone and no art, kept between 1:2 and 2:1; the cells on the 1024 canvas as the design's table gives them and on a wider one, inside their room, filling its width, six covers at least, each cell within 12% of the shape with the fewest columns; and which art is a cut-out |
| `check-roundrect` | the rects a rounded rectangle is drawn with cover the old row-by-row shape exactly once, for every radius and a spread of sizes |
| `check-rowmacro` | no row macro is handed a side effect, and ROW_H reads its row once (the MENU-and-DOWN crash of 2026-11-05) |
| `check-emptyhome` | the empty home's words for every device and port state: the screens' names, each way's button, no address the device does not have, a reason exactly when a way cannot start |
| `check-sysmark` | a system's color as a mark, for every shipped system in both themes: no color is text-dim and lime is accent-ink |
| `check-menushots` | a menu's open, close and swap at their ends as the launcher draws them, byte for byte, and a picture halfway through each (builds the host launcher; skips without SDL2) |
| `check-uisound` | the interface sounds' rule: which press plays what, never on a Bluetooth headset, the setting's four positions, the WAVs the launcher reads, and the mix |
| `check-bgm` | menu music's rules: the tracks, from the card's Menu Music folder and its folders and ZyperOS's own, the settings, the shuffle, the history L2 goes back along, the gain, the Menu Music rows, a track's name from its tags and Zyper's pop-up |
| `check-bgm-tracks` | the menu music a fresh card plays: every file in res/bgm/ is the one res/bgm/tracks.json records, under a license a card may ship, credited, Ogg Vorbis at 48 kHz |
| `check-sounds` | every WAV in res/sounds/ is what tools/gensounds.py synthesizes from tools/designdef.py's tones, and there is no other |
| `check-messages` | result holds and the real input loop with a fake clock, and the source guard that keeps every result site off blocking delays |
| `check-tour-previews` | the tutorial's Theme and Home Screen previews in res/tour/ are the launcher's own home screen as it draws today (`make tour-previews` makes them again) |
| `check-tour-colors` | the tutorial across frames: low-alpha first frames and a theme round-trip never poison cached text, details or bars |
| `check-update` | System Update end to end: two fake releases packed and signed with a throwaway key, check, download, a cut download resumed, refused lists and packs, the boot switch and its step back (skips without OpenSSL 3, libcurl or zlib) |
| `check-sysupdate` | what MENU > System Update says in each state, held to the update lab's screens, and what it starts by itself (no download over a phone's mobile data) |
| `check-pminstall` | what the Install PortMaster page says in each state (src/pminstall.c), and the Ports shelf's PortMaster entry added to a listing that lacks it, once |
| `check-pico8` | your PICO-8 (src/pico8.c): which files of a zip are taken and from what (its version from the manual or the name, an AArch64 ELF for `pico8_64`), zips the check writes itself with made-up bytes taken out of `Bios/PICO-8` (the Raspberry Pi layout deflated and stored, the Windows zip, a program for another CPU, a file that is no zip, the two files alone, a newer zip taken again), the PICO-8 shelf's Splore entry added once and pinned first, and the How to Add page's words; and its picture (src/p8draw.h): every Display mode's rect Diatom's own for a 128 x 128 picture (diatom/src/scale.c) on both a 640 x 480 and a 1024 x 768 window, the Pixel 2's table, none or an unknown word Original Shape, `P8_NEG_OK` 0 drawing Zoomed as Sharp Pixels and Edges Cut as Original Shape, and sd/pixel2/PICO-8.sh run on a scratch card with a stand-in `pico8_64` for every mode, none and an unknown word, on a turned 480x640 panel and a landscape one, with `NEG_OK` as it ships (which is `P8_NEG_OK`) and at 0: `-draw_rect` and `-pixel_perfect` as p8_draw_rect says, and 640 x 480 with no panel mode to read |
| `check-pico8-wget` | your PICO-8's wget (`sd/pixel2/PICO-8-wget.sh`, `ZyperOS/pico8/bin/wget` on the card: pico8_64 downloads every Splore list and cart with `wget`, and busybox's has no HTTPS) against a stand-in curl and system wget: Splore's two calls (`-q -O FILE`, and a post with `--post-file`) go to curl with redirects followed, HTTP and HTTPS only, ZyperOS's certificate bundle, no `-k` and no `-q` (the launcher's `.curlrc` read); the file whole under its name or not there, no `.part` left, curl's exit on a failure; an empty reply an empty file; `-O -` to stdout; any other call, or no curl, the next wget on PATH with the same arguments, never itself again (exit 127 with none); PICO-8.sh puts `bin/` first on PATH, and mk/payload.sh puts the file there, runnable |
| `check-pico8-display` | your PICO-8's Display default: a shelf never set plays it at Original Shape (its row says so, a cart opens with `aspect`, no row written), fake08 on it Full Screen, a "stretch" from before 1.1.0 let go of once with the marker kept, another shelf's change writing that shelf's row alone, and Full Screen picked on the shelf's row opening with `stretch`; the in-game menu's Display row over your PICO-8 (src/portmenu.h's `port_menu_display`, PICO-8 draws its picture where it was told as it started): launch.sh's run_port with stand-ins, the restart file (`/tmp/zyperos_port_restart`) and USR2 as the menu's start again sends them ending the program with TERM and starting it again with the file's Display, once, portkeys and MP4's follower going on through it and MP4 not stopped, the file gone; a word that is no mode's a plain Quit, a file from before the start none, POWER while the program ends starting nothing; then the real launcher in desk mode's dummy window as portkeys starts it (`--port-menu PICO8 CART`): Continue, Display (the mode `display.PICO8` keeps), Controls and Quit, Left and Right stepping nothing, A its list, another mode asking "Start the cart again?" with the mode named and the cursor on Cancel, Cancel keeping nothing, the mode on asking nothing, Start Again exiting 14 (`PORT_MENU_RESTART`) with the mode kept and in the file, B Continue removing a file from before, no Display row over OpenBOR, and `--display` (the Next Display hotkey's) asking for the next mode round with no menu, Cancel 10 and Start Again 14; and On-Screen Stats after Display (none over OpenBOR), Off, Right turning it On in the device's database and Left Off; every wait in its walks a wait for an event, never for a time or a count of frames (tools/deskwalk.py: the panel a key lands on, from desk's `state`, a setting or a file), and a drawn string found wherever its line begins |
| `check-pico8-keys` | your PICO-8's hotkeys, Reset and buttons as the launcher and PICO-8.sh do them (src/portmenu.h, src/p8pad.h): PICO-8.sh against a stand-in pico8_64 exporting the pad's mapping line the launcher left (`/tmp/zyperos-pico8-pad`) and the one that ships with none, another pad's or one with what no mapping has; then the real launcher in desk mode's dummy window, `hotkeys.PICO8` and `map.PICO8` in its library: a cart opened (px2-open's channel) leaving portkeys the hotkeys in force in Diatom's RUN form and `Screenshots/PICO-8/<cart>` (`/tmp/zyperos-port-keys`), PICO-8.sh the swapped line, "Screenshot saved" in both themes with an empty square and its `SLOT` trailer, and with Next Shader on a key "Shader / <name>" for each of PICO-8's six shaders in both themes; the menu over it (`--port-menu PICO8 CART`) with Reset between Controls and Quit, asking "Reset the game?" with the cursor on Cancel, Reset exiting 13 (`PORT_MENU_RESET`); `--reset` (the hotkey's) asking with no menu, Cancel 10, Reset 13; Controls opening the buttons (Applies To, This Game, the A cap giving O), a step kept as `map.PICO8.<cart>`, B writing the new line and asking "Start the cart again?" for the new buttons, Cancel the menu again and the hotkeys written again as it ends, the folder kept, Start Again exiting 14 with the Display on in the restart file; no Reset row over OpenBOR. PICO-8.sh exports the joypad's line and portkeys's pad's (`06000000595a…`, the same body), each the shipped one when the file has none for it, and sets `merge_joysticks 2` in config.txt, its other lines kept. portkeys's half is `check-pkkeys`'s and `check-portkeys-flow`'s; every wait in its walks a wait for an event, never for a time or a count of frames (tools/deskwalk.py: the panel a key lands on, from desk's `state`, a setting or a file), and a drawn string found wherever its line begins |
| `check-p8fx` | your PICO-8's Shader and Border (src/p8fx.h), all but the drawing: the look file (`/tmp/zyperos-pico8fx`) written whole by a rename and read back, lines it does not know passed over and values out of range the default's; the shaders PICO-8 is offered the launcher's SHADERS for its shelf on a GPU device, in order, never LCD No Backlight, Off and Sharp-Shimmerless with no GPU, the steps round and their words; Next Shader on a scratch database (`p8fx_next_shader`, portkeys's hotkey) from the look file's shader, else `shader.PICO8`, else the old `shader`, kept and the file's shader line alone changed; PICO-8's 128x128 out of its preblit at 1x to 4x and padded rows, any other shape refused; sd/pixel2/PICO-8.sh with a stand-in `pico8_64` loading `p8fx.so` beside it first (`LD_PRELOAD`), before one already set, and none with nothing there, said in the log; then the real launcher in desk mode's dummy window as portkeys starts it (`--port-menu PICO8 CART`): Continue, Display, On-Screen Stats, Border, Shader, Controls and Quit, Strength under an old screen, Shader, Strength and Border each kept (`shader.PICO8`, `shader_strength.PICO8`, `border.PICO8`) and written for the shim, Your Own's picture made into its DTOV file the panel's size, no Border at Full Screen, none of them over OpenBOR, and a cart opened from the shelf writing the look file from the shelf's rows; every wait in its walks a wait for an event, never for a time or a count of frames (tools/deskwalk.py: the panel a key lands on, from desk's `state`, a setting or a file), and a drawn string found wherever its line begins |
| `check-p8fx-gl` | your PICO-8's Shader and Border drawn: the shim (`src/p8fx/shim.c`, built from `mk/cross.mk`'s `P8FX_SRC`) loaded with `LD_PRELOAD` over a stand-in that calls SDL as pico8_64 0.2.7 does (`tools/p8fx-standin.c`), on SDL's offscreen video and its OpenGL ES 2 renderer on Mesa's llvmpipe, each frame read back before the present: with no look file, Off, a shader of none, LCD No Backlight, a border picture not there, OpenGL ES 2 only (`MESA_GLES_VERSION_OVERRIDE=2.0`) and SDL's software renderer the very frame drawn without the shim; each shader, inside PICO-8's rect, within 3 levels of Diatom's GLSL in C (`shaderdemo --image`) at Full Screen (preblit 3), Sharp Pixels (preblit 1 and 2), under 100% and for a rect past the screen's edges, black round it; Blur round SDL's own picture, never black and never brighter than the picture; a picture border the picture; PICO-8's FPS line, which SDL draws after, as without the shim; a look written while it runs drawn next. Linux only; elsewhere it says it skipped, and `make check-p8fx-gl P8FX_DOCKER=1` runs it in `tools/p8fx-gl.Dockerfile`'s Ubuntu |
| `check-pico8-mp4` | MP4 over your PICO-8 as over a Diatom game (src/mp4hand.h): the queue's file (`/tmp/zyperos_mp4_queue`) written and read back whole, each kind of queue and a file of no tracks, anything else refused both ways, under AddressSanitizer and UBSan where the compiler has them, and your PICO-8 the one program MP4 plays on over (`port_keeps_mp4`); launch.sh's run_port with stand-ins: a request with `mp4` fifth leaves MP4 playing, starts the follower (`zyperos.elf --mp4-follow`) beside the program, gives portkeys `--follow` with its pid and ends it before the launcher comes back, and OpenBOR's request stops MP4 as before; then the real launcher in desk mode's dummy window against a stand-in daemon (`ZYPEROS_MP4_SOCKET`): back from PICO-8 the queue taken on with no PLAY and no resume, a cart opened writing the request and the file, the follower's next track at an END (its log lines `port: ` ones, which splash leaves off the loading screen), and SELECT in the in-game menu over PICO-8 (the follower stopped) opening Now Playing, L1 there, SELECT let go closing it, B Continue, the file written back and the follower going on from it; over a program with no file SELECT opens nothing; PICO-8 quiet while MP4 plays: the equalizer's file's cut (`ZYPEROS_EQ_FILE`) set as the launcher hands over with MP4 playing, cleared at a pause and set again by the follower within 0.5 s, written by the in-game menu as it closes, cleared as the follower ends and as the launcher starts, and eq-port.sh's PCMs byte for byte every port's, no softvol stage; a USB-C DAC in (`/proc/asound/card1/usbid`): your PICO-8 into the system's `usb` PCM once `zyperos.elf --alsa-try` opens it, the mix as before when it does not, with no such PCM, with no DAC, and for every other port; and `--alsa-try` against this computer's alsa-lib on Linux, a chain that opens and one that does not. Every wait is for an event (the stand-ins beside the program up, a screen drawn, a key taken and the frame after it drawn, the daemon's song drawn), never a time, and a string drawn is found wherever its line begins (ALSA's lines on Linux) |
| `check-pmhelper` | the PortMaster installer (res/portmaster/pm-install.py) against a made-up release in a folder: an install, a second run, a lost PortMaster.sh put back, an MD5, SHA-256 or size that does not match, members outside PortMaster/, a release list that makes no sense, runtimes kept, a second run at once turned away |
| `check-portlibs` | the libraries PortMaster's ports and their runtimes load from the system (plastron's `board/zyperos/ports-libs.txt`: the LOVE runtime's, Stardew Valley's OpenAL, and what the Westonpack runtime takes beyond its own folder, libevdev and libbz2 among them) asked for in the image of each device whose ZyperOS runs ports (`HAS_PORTS=1` in its `sd/<device>/device.sh`): each option set in its Buildroot config and each package in its package list; then plastron's `scripts/ports-libs.sh`, which its release runs over the built Pixel 2 image, over made-up roots: all there passes, one missing or a link to nothing fails and names it, 1.1.0-rc.6's image without libevdev.so.2 and libbz2.so.1.0 fails on those two (no image built) |
| `check-pmart` | PortMaster's covers in both themes and its page's icon (res/portmaster/*.png) are the system art's (res/system-art/original/utility), byte for byte |
| `check-portdoc` | what a port's own files say for the Controls pages of the in-game menu over it (src/portdoc.c: port.json, its gptokeyb keymap, its README's controls) and the pages they make, on the made-up ports in tools/fixtures/ports and the forms porters write by hand; PortMaster's own page; the menu over every port |
| `check-boottheme` | the boot screen's theme and language words (DESIGN.md 11.21) |
| `check-autotheme` | Appearance's Auto on Night Mode's Night Hours (1.1.0): light in the day and dark in the night as the database keeps them, across midnight and to the exact minute, a change on Night Mode's page moving it at once with Night Mode Off, dark at exactly the minutes Night Mode's nights are on (every minute of a day, and New York's at summer time's end), the row's three, the first start's migration once a card (a 1.0.5 card's Auto kept on its old hours, an Auto picked since left alone at every start), and the boot screen's word with the hours, read by plastron's splash to the launcher's theme every minute of the day. And Tarik's no-conflict rules: Auto writes no night.* key (picking it, leaving it, starts, a day and a half of ticks), Night Mode the same under Light, Dark and Auto (3 x 3 modes x 8 times), the theme turning on the warmth's own second at From and Until with no table load of its own (also at a minute set by hand and in New York at summer time's end), and a 1.0.6 card's Night Mode settings untouched by the update |
| `check-splashbright` | the backlight value plastron's splash lights the panel at (`zybright` on the kernel's line, from theme.txt): within the backlight's maximum, and none for `-`, 0, a word or a value above it |
| `check-bootbright` | plastron's init sets the backlight from the card's `boot-brightness` as soon as the card is mounted, within its maximum or through the Brick's tool, and leaves the kernel's level for anything else |
| `check-seed-version` | the boot's ZyperOS version compare (plastron's `zyperos.init`, `version_of` and `newer_seed`, as each board's post-build.sh takes it for `SEED_V`): 1.1.0-rc.N, -kit.N and -test.N are versions below 1.1.0 and in order (test below kit below rc), the image's ZyperOS goes over an older card's and never over a newer one, and what is not a version (none, "", 1..2, x, 1.08.0, 1.2.3.4) is false without stopping the shell |
| `check-panelgamma` | the display's gamma table (src/panelgamma.h), against a stand-in display with Linux's request numbers: an older splash's table emptied on its CRTC alone, an empty one left alone, a kernel with none, a refusal without DRM master; the dimming table under the backlight's floor; Screen Colors' Temperature, Tint, Contrast and Gamma per channel on every rung (every value of every row a rising table, black kept), Standard exactly as before, and the file the launcher leaves for Diatom and portkeys (a FIFO or a link at its name not followed) |
| `check-panelgamma-sunxi` | the TrimUI Brick's display's gamma table (src/panelgamma_sunxi.h), against a stand-in `/dev/disp` with the disp2 driver's command numbers: Standard turns the table off and sends none; a look sends its 256 entries (the shared math's at the floor, red, green and blue in the TCON's 24 bits), then turns it on; Night Mode alone is a table; a refused table is never turned on; and Saturation's steps in the display engine's units (25 to 75, 50 the picture as drawn) |
| `check-nightmode` | Night Mode (src/nightmode.h): its warmth in the display's table (Temperature's gains again, multiplied after the player's), the file's warmth and nights for Diatom and portkeys and the clock against them, the nights by the hours across midnight, on the same day, none for the same hour twice, On and Off, and in New York across both ends of summer time; and the first start's migration from 1.0.5's Appearance Auto, pure and on a scratch database: Auto kept, its hours kept as Night Hours (and as Auto's), Night Mode left Off, once a card |
| `check-ingame-colors` | Screen Colors in a game (DESIGN.md 11.42): the real launcher in desk mode's dummy window, as the Pixel 2 and as the Brick, started back into a game against a Diatom of the check's own, the menu up: R1's Display tab has a Screen Colors row, A on it (right over On-Screen Stats) opens MENU's own page over the paused game, Right on its Look keeps Vivid in the database and puts it on the panel (desk mode's stand-in for the table, `tools/desk state`'s `colors`) with the game still paused and told nothing, B is back on the menu and B again resumes the game; as the host's own profile, which has no table, no row |
| `check-displayback` | the display taken back after a game (src/displayback.h), against a stand-in display and clock with Linux's request numbers: a clean end (master at once, the game's last frame left up), a crash (master asked for again until the dying Diatom lets go, then a black picture put up on the CRTC it left empty, in SDL's own format so SDL's next flip is allowed, kept for the next crash), a crash that turned the CRTC off (the panel's preferred mode), QEMU's XRGB8888 frames, a panel listing more modes than it holds, a Diatom that never lets go (refused on the bound), the per-frame try, and a refusal without master |
| `check-px2-push` | plastron's `make push` (scripts/px2-slot.sh): it writes the system slot the Pixel 2 is not running and never the running one, a card with one system or a command line that does not add up gets nothing, bootstate.txt edited as System Update does, and the device's script parses and writes no partition by number |
| `check-wifi-watch` | the Pixel 2's Wi-Fi watcher (plastron's `/usr/lib/zyperos/wifi-watch`) against a stand-in wpa_cli and log: a signal line soon after a join, at once on a jump of 8 dB and every minute, none while not joined or for the drivers' -9999; a line with the last look when the link drops; after a drop that cut a handshake on a network that joined with its password, one retry (disable_network then enable_network) per join, never for a network that never joined or another reason; no network name in its lines; it ends with the supplicant |
| `check-launch-start` | launch.sh's start on a card a power cut broke, on each device (`sd/zyperos/launch.sh` with its `sd/DEVICE/device.sh` beside it) with the card's paths moved to a scratch folder and the device's commands stood in for: a junk boot.env is read as off and the script goes on to the launcher (sourced, a syntax error in it ended launch.sh before ZyperOS); a launcher that stops at once five times sets launch.sh aside, so the next start copies ZyperOS again, says so and powers off; a second time it is left as it is; a power-off asked for takes the marker away; and launch.sh with no device.sh beside it is set aside the same way, the launcher never started, and the handheld powers off; and on the Pixel 2 the kernel's line about the battery at the start is copied into the start's log once |
| `check-crashlog` | the Pixel 2's crash report (plastron's `/usr/sbin/crashlog`) against a stand-in pstore folder: nothing kept says nothing; a panic's dump becomes one crash-DATE.txt with the dump and the end of the console, plain line ends, and the dump is let go, so the next start adds nothing; a console that ends in the kernel's own restart or power-off says nothing, one that ends otherwise is a report; two reports in one second keep a file each; only the newest CRASH_KEEP stay; the RCU stall panic is set at every start |
| `check-captive` | Wi-Fi File Transfer's hotspot, its pure half: the DNS answer for every name, the Host check that tells a phone's internet check from the page, the QR code's text and the password |
| `check-mdns` | zyperos.local's responder (src/mdns.c): the queries a Mac sent, read off the wire, each answered as RFC 6762 says; every piece of them and random bytes under the address sanitizer; two handhelds on one Wi-Fi named apart on a pretend network |
| `check-linksteps` | File Transfer's link steps (src/linksteps.c): the cable, the address and the browser, each hint at its time, a replug, Wi-Fi, and the device's /proc/net/arp and UDC state |
| `check-usbdrive` | plastron's usbdrive (src/drive.c), the card as a USB drive: never exported while mounted or held; an export ends on the computer's eject, the cable out for 1.5 s, POWER, or no computer in a minute; the page's numbers (src/drivestat.c): the totals and speeds as the page shows them, each way idle at 0 two seconds after its last sector, a pause that keeps the speed, both ways at once, never redrawn while idle, a counter that goes back, the clock's wrap; and the atlas (src/atlas.c), read as untrusted: a broken one refused (a missing glyph, a glyph taller than its slots, rects that overflow or are inverted, a huge picture), a row from the left or right-aligned, cut at its slot, the slot cleared first, and a layout that skipped the parser still drawing only inside its slot; bright.txt read as untrusted (rungs 1 to 255 going up, the level on them, the bar inside the picture); the handheld's buttons (src/drive.c drv_key): FUNCTION and volume step the rungs and stop at the ends, each rung said to the side lights as `level brightness RUNG TOP`, volume alone does nothing, B ejects at once unless the computer may still be copying (then it asks: A ejects, B keeps): a write in the last 5 s, more than 1 MB dirty, or the gadget's counters unreadable, which always asks; /proc/<pid>/io and /proc/meminfo's Dirty read as the kernel writes them; across the clock's wrap; the side lights' line (src/drivestat.c dst_lights_line): each way's bytes a second over the last look alone, a way under 64 KB/s still, `move 0 0` once when both go still, a short look counted at its length, a counter that goes back |
| `check-gauge` | plastron's gauge (package/gauge/src/lights.c), the Pixel 2's side lights, as plastron's `make gauge-check` holds it: the setting's line, old and new words; the charging bar, still when full and at the charge limit; the low-battery red in two stages and through everything; the volume and brightness in eight steps; Done, the sparkle, newest on top; MENU > Side Lights' previews, each part in a loop, On or Off, over everything but L2+R2, 2.5 s past the last line or `preview off`; files moving: the wave down for files coming in and up for files going out, its pace by the speed (200, 100 and 60 ms), the bigger way chosen as each run begins and never cut, 1.5 s past the last line or `move 0 0`, over the bar and under the patterns, off with Files and Covers, lines that are not two numbers ignored; L2+R2 over all of it; and no wake of 0 ms anywhere |
| `check-start-soc` | the GKD Pixel 2's battery level at a start (plastron's `9-px2-rk817-start-soc.patch`), as plastron's `make start-soc-check` holds it: `rk817_rest_uv`, `rk817_start_from` and the charger's start limit taken out of the patches, the OCV table, the 111 mOhm internal resistance and the 300 mA charge end out of the device tree; the sample has its current's drop taken out (none without a resistance, 5 A through 1 Ohm without overflow); under 30 min off the counter is kept, from 30 min the voltage, a charger's start (over 300 mA in, the device tree's charge-term-current; `9-px2-rk817-wear3-charger-start.patch`) keeps the level kept before the power-off (`rk817_start_level`), not the counter that fell while off, and a start from POWER under any current out takes the voltage; a player's ten 1.0.6 starts as the kernel logged them, whose starts from POWER read +108 to +114 mA with no cable in, each from the voltage after 30 min off (93% where the counter said 64, 38 and 30%, and 79% where it said 0%) or from the counter under 30; the 1.0.5 case (a cable's start at 1.5 A, the path twice the device tree's resistance) still never from its sample; a battery at rest at 49% sampled at 250 mA out reads 42% alone and 49% corrected; the full charge a start measures (`9-px2-rk817-wear.patch`'s `rk817_learn_fcc`); and the empty end (`9-px2-rk817-wear2-empty.patch`'s `rk817_empty_due`, the patches checked to sort in the order they need): under 3.55 V at rest, which the table reads as 2% or less (ZyperOS's `BATT_EMPTY_PCT`, read from src/status.h), with the counter 10 points or more over, on the third 8 s update in a row the counter is set to the voltage's level; spikes of one or two updates, a sag under 1 to 1.5 A at 20 to 50% through twice the device tree's resistance, a counter that is right, the cable and the charge limit set nothing; a battery that holds 1200 mAh measures 1465 and then 1333 at its empty ends |
| `check-usbrole` | when the Pixel 2's USB port may be a host (plastron's usbrole, src/rule.c): the nine plugs of a Mac traced on 2026-11-10 never host unanswered; a DAC only on USB Accessory for its own plug; the quick swap (a Mac's cable out, an adapter in a second later) and a plug made during a game asked about and powered on the answer; Automatic after its 2 s settle, a computer on the bus ending it, and the Mac whose port drops and comes back after each feed (2026-11-10, Automatic) fed at most once, its next plugs asked about until the port rests empty 3 s; never with power in, a charger detection or a failed reading |
| `check-clients` | File Transfer's browser count (src/clients.c): browsers, not sockets; one tab with five keep-alive connections is one, two browsers two |
| `check-pace` | when a panel screen draws (src/pace.c): a still one its heartbeat (about 610 frames in 10 minutes, not 36,000), a change, the redraw clock's moment or anything moving at once, the memory's slow clock after 2 s still |
| `check-keyhints` | button hints (src/keyhints.c), the one component for what each button does: rows in order and in their room, lists packed and read back in all eight languages |
| `check-usblog` | the USB port's log for support (src/usblog.c): a plug a line, grown as it goes and closed with its time in, power in alone a line, the last 50 under 8 KB, read back from the file |
| `check-usbpower` | What did you plug in?, the launcher's half of usbrole (src/usbpower.c): usbrole's state lines, the request and the setting written whole and never through a link, asked once per plug after 2 s (never on Automatic unless a computer paused it), the replug message for a plug a computer showed up on or came back to while paused, Phone's messages kept while the port powers the phone, and Automatic offered once, at the second USB Accessory answer, on Ask, never again once answered |
| `check-phonelink` | A phone's USB tethering (src/phonelink.c): its network interface told from the gadget's own usb0 and from a Wi-Fi adapter's, an iPhone told from a Mac on the bus (as sysfs reads them, newline and all), USB File Transfer opened once a link, and a way out to the internet from /proc/net/route (Wi-Fi, or a default route that is not lo; the phone's, metered; never the cable to a computer) |
| `check-qr` | the hotspot's QR code, read back by zbarimg where the host has it |
| `check-home` | the home's choice, labels, motion, cached music, renderer state, bounded assets, worker isolation and missing art, on the launcher's own objects |
| `check-home-shots` | the home screens as the launcher draws them in both layouts and themes: footer inside the inset, the song, a flip, a move (builds the host launcher) |
| `check-np-footer` | the song's name in the top bar slides when too long and stands when it fits, and Now Playing and a video draw their keys as a row of hints between the margins, in both themes (builds the host launcher) |
| `check-vidtry` | when MP4 says a video is too big for the CPU: the pictures shown and dropped that the Pixel 2 measured with AV1 (720p60 and 1080p30 never, 1080p60 within 6 s), taken back when it turns light, a seek's or a pause's catch-up not counted, no picture left alone |
| `check-menu-rows` | every option row of more than two choices opens its list on A and every two-choice row turns over, through one table of choices a row's build gives the runner; a stepping row with no table, or a table that says something other than its row, fails (builds the host launcher) |
| `check-favorites-shots` | the Favorites shelf names each game's own system right under its name (under one line or two), a console's own shelf does not, and every line keeps its margin, in both themes (builds the host launcher) |
| `check-homesys-groups` | a system turned off the home (MENU > Display > Home Systems) keeps its games on Favorites and Recents, which look as they do with it on, pixel for pixel, in both themes; A on one starts it on the system's own core (a stand-in Diatom says what it was given), a power-off in it resumes it on Recents, Delete Game takes it off the card and the groups; with the groups off too, it is nowhere (builds the host launcher) |
| `check-recents-played` | a program of its own reaches Recents and Played (src/stats.h, "a program of its own"): launch.sh's run_port with stand-ins runs `zyperos.elf --port-played` as the program ends, before the launcher comes back, and with POWER over it before the power-off; then the real launcher on a card with your PICO-8 (stand-in files), a cart, OpenBOR, a port, PortMaster and a SNES game played yesterday: A on the cart writes the request, the note (`/tmp/zyperos_port_played`) and the cart's row open at 0 s, `--port-played` closes it as a quit (1h 2m, the note's clock moved back) and removes the note, a second finds nothing; the cart first on Recents with "PICO-8 · today HH:MM" (its start, the card's zone, 24 and 12 hours) and "Played 1h 2m" in Covers, Grid, List and Video, both themes, nothing cut; the SNES game's "SNES · yesterday 21:40"; the cart's Played on the PICO-8 shelf, with no time there; a launcher that starts with the note still there closes it as a quit before Recents is built; OpenBOR's module and a port on Recents the same way, Splore with its own system's line, PortMaster itself no note and no row; Recents' Games not counting Splore. Shots go to build-native/recents-played-shots/ (builds the host launcher) |
| `check-lateskip-launch` | a late frame left undrawn reaches Diatom on both ways a game starts: with no resident Diatom (the first game after it ended) the real host launcher, a stand-in Diatom that writes what it was given, and ZYPEROS_AUTOLAUNCH give a PSP game `--frameskip late` and one screen, a DS game `--screens 2` and `--frameskip late` as before, a Game Boy game no `--frameskip`; RUN's keys are `check-config`'s |
| `check-artbadge` | the art badge (src/artbadge.c): every badge, the heart, the album's notes, the book and the video, in both themes at every size a row draws one, sized and placed with its card, its glyph 3:1 on its disc and inside it, the shadow below, the heart symmetric (no SDL) |
| `check-cardart-shots` | a game's card with no art on the launcher's own shots: Zeta Run on Genesis, DuckTales on NES and a long PlayStation name each get the card in their system's ground; a played game shows its frame (the card frame over the Auto picture, the Auto picture until there is one); the same card in both themes; the Grid's names on one reading line; as the cover row stops on it, a sheen that lights nothing round the card and a played frame turning sharp, neither on a cover with art (builds the host launcher) |
| `check-focusfx` | the cover row stopping on a game's card with no art (src/focusfx.h): the sheen once across on its curve and never again while the focus stays, a played card turning sharp from when it is in hand, both stopped at once by a press, the redraw clock told when they end, nothing for a cover with art |
| `check-artbadge-shots` | the badge on the real shelves: a heart on a favorite's cover and on no other, smaller on a side card, on Favorites too; on MP4's All an album, a book and a video each with its own badge, and none on Music alone, in both themes (builds the host launcher) |
| `check-layout-shots` | a game page's play time sits where Favorites' system line does, and over a paused game the clock is at the top left and the battery's text at the top right, in 8 languages and in both themes (builds the host launcher) |
| `check-games` | the games row: the aspect, the wrap, tiny lists, rapid reversals and bounded jumps, on the launcher's own objects; the Video view asking a frame for every step of its covers' glide |
| `check-cardpaint` | the card for a game or an album with no art, drawn on an art worker: the frame's pixels byte for byte, for every kind of title and both themes, a game's and an album's; in the faces of the language it was asked in; never handed to the shelf after a turn of the theme; the fonts closed and opened again around a fallback game while cards are asked for, with none drawn while they are down; two drawn at once by the two workers, one held in the middle of its paint while the other is drawn; and the frame's to make with no faces or no worker, on the launcher's own objects |
| `check-putpace` | finished pictures put on screen a few a frame: the pace's rules with the costs handed in, then the workers' own cards on a software renderer, slow uploads included - every frame inside its share or one picture, every picture in its own slot in a few frames, none lost when more finish than the ring holds, a loop with nothing pressed drawing until the last is in, and the picture a screen waits for out of its turn, on the launcher's own objects |
| `checkmark` | src/zypermark.h, Zyper as triangles, is what tools/zyperdef.py generates |
| `check-installer` | ZyperOS Installer's version is ZyperOS's, and its page wears this repository's colors, fonts and Zyper (its Rust tests run in CI) |
| `check-from-rogue` | tools/from-rogue.sh's table against systems.cfg and against the folders on a ROGUE OS card |
| `check-importers` | the computer-side importers (--meta, --titles) on a scratch card: a title written over a game's details keeps them (builds the host launcher) |
| `check-diatom` | Diatom's own suite (diatom/Makefile's check) on its desktop port with the stub core: the port seam, core facts, achievements, screens, the protocol, the state plane, resident cores, save states packed and read back (old ones too), the card's frame of play written once a minute in and never again; then the launcher's own Diatom link against it (tools/gamezone-check.c): the zone chosen in Date & Time reaches a game's clock, a new zone the next game with no restart, and a Diatom started again its first game (skips without SDL2) |
| `check-desk` | desk mode (tools/desk, src/device/desk.h) in SDL's dummy window, as the Pixel 2 and as the Brick on tools/desk's sample card, over its control socket: the panel its size (a shot is 640x480 or 1024x768), a press held until let go, a tap timed to its frame, the zoom and the drawn handheld on and off, an unknown button refused, `state` naming the shelf's screen, cursor and focus, quit |
| `check-homekeys` | the home's d-pad held as a thumb holds it, in desk mode over its control socket, the cursor read after each frame: on the Grid a held Down walks a row at a time to the last row and stops, a held Up back to the first and stops, a held Left stops at the first system, on 24 systems (the window scrolls) and on 8; on Spotlight a held Down jumps forward as taps of it do. Each button is read once a frame (`in_repeat_axis`): a second read of a held one is false, which turned a held Down round |
| `check-inrepeat` | no button read twice by `in_repeat` in one step in `src/`: a repeat is true once a frame, so the second read of a held button is false, and "either is due, then which one" turned a held direction round (the home, the in-game save slots, File Transfer's and the USB questions did); each is read once (`in_repeat_axis`). A reading of the source, its rule tried on a bad and a good snippet first |
| `check-videoview` | the Video view (11.57) in desk mode over its control socket, its snap playing (the test picture): past the screensaver's delay with nothing pressed, Right still steps to the next game, the screensaver held as music holds it; it took the screen over the view and ate the press that ended it |
| `check-keymap` | one key map for a keyboard standing in for the buttons: src/device/keymap.h, the Simulator's (App.swift) and Diatom's own window's give each key the same button, and both docs point at it |
| `check-handhelds` | the simulator's handhelds (simulator/Sources/SimCore/Handhelds.swift) are what `make handhelds` writes from the device files (each src/device/<id>_model.c's flat drawing), and those files whole: every flat key a key of the model, every key a button, the screen in its glass |
| `check-simulator` | the simulator's Swift tests: its D-Bus display client (the wire format, and a fake QEMU's login, properties call, frame in shared memory and frame over the socket), its VNC client, the px2sim link, the state lines and the control socket's commands (skips without Swift) |
| `check-qemu-image` | a release's QEMU image on its way to a developer, offline: `tools/qemu-run.sh fetch` from a stand-in release folder (a release without the image says so; the image goes into image/ with FETCHED once its sha256 matches the release's SHA256SUMS; again, nothing is downloaded; a download cut short goes on; another sha256, any other file in the archive, two images in one SHA256SUMS or another `--version` change nothing; an image/ fetch did not put there stays unless --force), `tools/release.sh` putting the image and its sources tar in the release's files and SHA256SUMS, the sources tar (plastron's `scripts/qemu-sources.sh` on a stand-in legal-info lists px2sim's source, license and package and the QEMU kernel's and U-Boot's configuration, and refuses without px2sim's source; `tools/qemu-sources-check.sh` passes it beside a Pixel 2 sources tar with the same packages and names a copyleft package at another version), and the SSH key: start hands QEMU the key's public half as the fw_cfg file the image's S49sshkey reads, making the VMs' own key when there is no other (a stand-in QEMU, macOS only), S49sshkey writes it where `/root/.ssh` points before dropbear starts, and the kernel has fw_cfg in sysfs |
| `check-px2sim` | px2sim's choice of the keyboard it takes in the QEMU image: QEMU's virtio or USB one, never a uinput one such as gptokeyb's Fake Keyboard (host compiler, no VM) |
| `check-sidelights` | what the launcher tells the side lights' gauge: the lines as plastron's gauge reads them, the FIFO writer that never waits and never dies of SIGPIPE, when a File Transfer batch is over, and files moving on its page: a line at most every 500 ms with the speed over that time, a pause between two files kept, `move 0 0` a second after the last bytes and at once when the page closes, across the clock's wrap; and MENU > Side Lights' preview lines, said again each second, another row at once, `preview off` as the page closes |
| `check-building-doc` | this table has a row for every check in the Makefile's CHECKS, and none for a check that is gone |
| `check-checkrun` | make check's runner (`tools/check-run.py`) with stand-in checks: CI's parts (`--shard I/N`) hold every check once and `check-i18n-shots` in each, the same from the list in any order and within the longest check of each other; `check-i18n-shots`' own parts hold every language on every handheld, each with English on its handhelds and the first with English on both, within one Brick run of each other; never more heavy checks at once than `CHECK_HEAVY_SLOTS`, in one run or with another process holding a slot, the light ones going on meanwhile, a slot taken again once its holder dies, the wait said on the check's line |
| `check-blog` | zyperos.com's blog, built as Cloudflare Pages builds the site (mk/blog.py): title and description lengths, structured data, every link and picture reaching a file (or a path in `_redirects`, such as `/tip`), every post in the sitemap and llms.txt, and the landing page's colors |
| `check-discord-release` | The release notes as `tools/release.sh` posts them in the Discord's #releases (`tools/discord-release.py`): every `docs/releases/v*.md` within Discord's limits (4096 characters an embed, 6000 a message), with What's new, without Install, Tested and Thanks, and ending with the release's link |
| `check-release-devices` | the devices a release carries (`config/devices`, `tools/release-devices.sh`): the release notes' `Tested on <id>: <who, on what>` lines read on sample notes (every device tested passes; a device missing, a misspelled or unlisted id, or a Tested line in another form fails) and sample lists; each listed device with its `src/device/<id>_profile.c` and its `res/update/min-system-<id>`, which `tools/update-pack.py` puts in that device's lists |

They are offline and need no device. A screen's rows are a pure function of
its state precisely so the first two can exist.

`make landbench` is a measurement beside them, not one of them: the frame
thread's time while a shelf of eleven lands, a hundred times, for games with
no art, games with box art, and two dozen cards finished at once
(tools/landbench.c). `--on-frame` works the frame as it did when it drew the
cards itself and took every finished picture in one frame; `--slow MS` makes
every upload slower, as a slow GPU's are. It prints and holds nothing.

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

`--lang CODE` draws any shot in that language (`en`, `zh-Hans`, `es`,
`pt-BR`, `fr`, `de`, `ja`, `it`, or `xx`, the pseudo-locale: every string
accented, 40% longer and in brackets), whatever MENU > Language saved;
`--menu` still finds a row by its English name. `--language [ROW]` draws MENU >
Language's panel, the cursor on ROW. `--music-page [ROW [STEPS]]` draws MENU >
Menu Music's page, the cursor on ROW and then STEPS presses of Down, a frame
each, so the list goes round (`foot`: no cursor, the note pinned under the
rows).
`python3 tools/i18n-shots.py --keep DIR` writes the main screens in every
language (`make check-i18n-shots`); `--theme dark` draws them in Salt &
Pepper, and `--binary PATH` with another launcher.

`make pixel-gate` (`tools/pixel-gate.py`) holds a change that means to
change nothing on the screen to the same pixels: it builds the launcher at
`BASE` (origin/main) in a worktree of its own and here, takes every shot of
`check-i18n-shots` in every language and both themes with each, and
compares them as decoded pixels, printing "pixel gate: K of N frames
changed" and each one, with before and after sheets of what changed in
`build-native/pixel-gate/`. Some shots or one language:
`make pixel-gate GATE='--lang en usb-ask hare'`; `--allow-changes` for a
change that means to change them. Not in `make check`: it builds twice and
takes about 13,000 frames a side.

The base's frames are shared by every worktree on the computer, frame by
frame, keyed by the base's commit, by what this tree hands the shots (`res/`
but its strings, `tools/fixtures`, `config/systems.cfg`, the shot tools and
their list) and by the host's compiler and SDL (`ZYPEROS_GATE_CACHE`;
`~/Library/Caches/ZyperOS/pixel-gate` on a Mac): a run shoots only the base
frames no run has shot yet, and a second run at the same base waits for the
first and takes its frames. `make pixel-gate-warm` shoots a base and nothing
else, once a main commit, so every pull request up to date with that main
shoots only its own side. `GATE=--targeted` is a pull request's round: no
frame at all when nothing the launcher is built from changed, else a first
look in English, the pseudo-locale, German and Chinese in light, English in
dark and any language whose strings changed, then every screen that changed in it in
every language and theme. The full gate is a release candidate's. This
tree's frames are deleted when the run ends (`--keep-frames` keeps them); the
report and the sheets stay.

`make clean-scratch` (`tools/clean-scratch.py`) deletes what the tools
left and nothing touched for a day: `$TMPDIR/zyperos-*` (the shots' scratch
cards, about 250 MB a language, from a run stopped hard) and each worktree's
pixel gate frames. A folder a running process holds a file in, or sits in,
is kept (lsof); `SCRATCH=-n` only lists them.

`--tour STEP [MS [DONE]]` draws a frame of the tutorial in place of the shelf:
STEP is `lang` (the language step: its cursor steps a language a second,
and `--tour-pick N` puts it on language N), `welcome`, `move`, `a`, `b`, `y`, `x`, `menu`, `select`, `vol`,
`bright`, `power`, `finish`, or `skip` for the message a skip leaves over the
shelf; MS into it (2000), its press landed DONE ms ago (none), at a fixed
clock, so the same arguments draw the same picture. The finish includes the
power reward: `--tour finish 0` shows the corner Zyper at entry, `70` shows
him leaving, and `500` shows the large hello after his exit.
`--tour-wrong KEY [MS]`
adds a press of the wrong button KEY MS ago (300), KEY one of `tour_key_name`'s
names in any case: `up`, `down`, `left`, `right`, `a`, `b`, `x`, `y`, `l1`,
`r1`, `start`, `select`, `menu`, `volume`, `brightness`, `power`; and
`--fn-volume` draws the Pixel 2's brightness keys, MENU+VOL, on the host's
profile; the README's shots and `check-i18n-shots` wear the Pixel 2 whole
(`--device pixel2`), its keys, rows and words.

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

`--info` draws a game's details over the shelf, `--info-delete` the same with
the cursor on Delete Game, and `--delete-ask` Delete Game's question, the
cursor on Cancel (DESIGN.md 11.31). `--gm-said K` draws one of Delete Game's
and Rename's messages over the shelf, as it shows once it has arrived
(src/shots.c's GM_SAID: 0 the frame a delete runs under, 1-6 Delete Game's,
7-10 Rename's). `--delete-now` deletes the game under the
cursor as Delete Game's yes does, on the card the shot is given, and the shot
is the shelf after it; stderr lists every path that went.

`--jump <n>` applies n letter-jumps first (negative for up), so where the
d-pad lands on a real library can be checked without a hand on the device.
`--theme light|dark` draws the shot in that theme whatever
MENU > Appearance has saved, and leaves both the saved setting and
`/tmp/zyperos-theme` alone, so a shot can be run beside a launcher; a card
saved on Auto keeps Auto on its row and is drawn in that theme at any hour. `--flip`
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
`--cheevos-screen` over a paused game, as the in-game menu opens it.
`--cheevo-card N` draws an achievement's card (0 earned, 1 the longest
description, which scrolls, 2 earned offline, 3 none), with
`--cheevos-badge SET NAME` for its art and `--phase MS` for its scroll. Every
shot fails (exit 3, a `CORNER:` line) when the status corner meets the
focused card, a games title or a panel, when its clock is not 16 from its
marks, when it draws a clock no `--status` gave, or when `--status` gave it
something to show and none is drawn. In a shot, Date & Time's row shows
`--status`'s time too, or none.
Every shot names the screen and the focused item on stderr, so a sequence of
them reads back as a list of what was actually drawn.

`tools/readme-shots.py` captures the README and website gallery in both themes,
with an isolated demonstration library; `--art` gives its games their real box
art from libretro-thumbnails, cached outside the repository. See
[`res/readme/README.md`](../res/readme/README.md) for capture provenance and
the refresh steps.

## Merging several pull requests

`python3 tools/merge-train.py 281 283 284` merges those pull requests' heads
onto `origin/main`, in that order, in a scratch worktree; writes again what
`tools/i18n.py` and `tools/designdef.py` write whole (a conflict in one of
those files is settled that way, and two PRs that each added a string merge
cleanly but leave the IDs stale); and runs one `make check` (`CHECK_JOBS` as
given). A conflict in any other file stops the train at that PR. The branch
`train-281-283-284` stays here; `--push` pushes it and opens its pull
request, which merged with a merge commit (`gh pr merge N --merge`) lands
every PR's own commits in main, so GitHub marks each one merged. One check
for the lot, where merging them one by one sent every other PR back to merge
main and check again. `--no-check` for the merges alone, `--keep` keeps the
worktree.

## Devices

Each handheld is a profile (`src/device/profile.h`): plain data in
`src/device/<id>_profile.c` saying what it is, its name and folders, its
screen as glass and pixels, its controls as printed, the chords a missing or
doubled button needs, and what it has. A screen asks the profile what the
handheld has (`plat_profile()`, and the `plat_*` answers made from it) and
the platform what it is doing; it never asks which handheld it is. The
device build links its own profile (`DEVICE=` in `mk/cross.mk`); the host
build links all of them and wears `desktop` unless told:

    build-native/zyperos --shot /tmp/tour.png --device pixel2 --tour menu 2000
    build-native/zyperos --shot /tmp/tour.png --device brick --tour menu 2000

`desktop` answers as the host always has, so every shot taken before
profiles is taken the same. `make check-devices` holds each profile whole and
no device guessed in `src/` (`tools/devices-lint.py`); `make
check-device-compile` compiles every device file's Linux half, which the CI
job `devices` runs on Ubuntu.

### A handheld drawn on the computer

Desk mode's window and the [simulator](simulator.md) draw the handheld from
its own files, the same data both ways. `src/device/<id>_model.c` holds its
model (`src/device/td_model.h`): where each key is, for the tutorial and the
Controls tab, and `flat`, the handheld drawn flat for the two windows, its
front straight on with the shoulders along the top and the side keys out of
its sides. The keys' places and sizes are the model's; `flat` adds the look:
the corners, the glass and the lit picture, what is printed on a key and
under it, which side its keyboard key shows on, a well for SELECT and START,
the side lights and which battery lights light them, and the colours. A key
with nothing in `flat` is drawn blank; `y` and `len` move a side key along
its edge where the flat drawing's differs from the model's (the Pixel 2's
rocker halves meet).

So a new handheld in the two windows is:

1. its profile and its model, as for the tutorial, with `flat` in the model;
2. its profile in `src/profile.c`'s list of the host's profiles;
3. `make handhelds`, which writes the Simulator's copy,
   `simulator/Sources/SimCore/Handhelds.swift`, from the device files
   (`tools/handhelds-swift.c`, which also fails on a key the model has not,
   a key with no button, or a screen outside its glass);
4. `tools/desk run --device <id>` and `ZyperSimulator --device <id>` to see
   it; `tools/desk shot FILE window` and `simulator/simctl window-shot FILE`
   draw it to a file.

Nothing in desk mode or the simulator names a handheld; `make check-handhelds`
fails while the Swift copy is behind the device files, and the simulator's
`HandheldTests` hold every handheld whole. A button no handheld had before
needs its keyboard key in `src/device/keymap.h` and the simulator's
`Keyboard` (`make check-keymap`). The simulator runs the Pixel 2's system in
QEMU; another handheld's VM is plastron's to make ([simulator](simulator.md),
"Another handheld").

## On device

A change to ZyperOS goes onto a Pixel 2 as a payload, without a new image:

```sh
make px2-programs                                  # a change to the code: the programs alone
make px2-payload                                   # or the whole payload (res/, launch.sh, cores)
tools/px2 deploy                                   # the newer of the two, under the device's lock
tools/px2 restore                                  # when done: the card as it was before
```

`tools/px2 deploy` is `tools/px2-deploy.sh` behind `tools/px2`'s door: the
device's lock (`/tmp/px2-lock`, refused while another session holds it, or
`--wait`) and the card check (the 32 GB test card, or `--real-card`). It
sends only the files whose checksums differ from the card's, and first keeps
the card's own copy of each file it replaces, and the name of each it adds,
in `/mnt/SDCARD/.px2-deploy` (outside `ZyperOS/`, so nothing on the card
reads it). A later deploy keeps what the first kept, so `tools/px2 restore`
always puts back the card from before the session's first deploy, deletes
what the deploys added, restarts ZyperOS (or the device, for a `launch.sh`)
and forgets it; `tools/px2 restore --status` says what it would put back.
`make px2-programs` (`tools/px2-payload.sh --programs`) builds the launcher,
MP4, portkeys, zyhome, System Update's program, p2d, your PICO-8's
`pico8/p8fx.so` and Diatom into `build/px2/programs/ZyperOS` and nothing else: no cores, so no main
checkout's `vendor/`, whose patched cores' stamps a whole payload holds to
`mk/patches`. A change to `res/`, `launch.sh` or a core needs `make
px2-payload`.

The same without the lock, and to other places:

```sh
tools/px2-deploy.sh build/px2/payload/ZyperOS      # the device on the USB cable
PX2_HOST=192.168.1.23 tools/px2-deploy.sh build/px2/payload/ZyperOS   # or over Wi-Fi
tools/px2-deploy.sh --restore                      # what it kept, back
tools/qemu-run.sh deploy -n mine build/px2/payload/ZyperOS   # or a VM (docs/qemu.md)
```

Every tool that reaches the device (`tools/px2-deploy.sh`, `tools/px2` and
plastron's `scripts/px2-push.sh`) takes its address from `PX2_HOST`, 10.42.0.1 (the USB cable) when it is
not set, its port from `PX2_PORT` (22) and its key from `PX2_KEY`
(`~/.ssh/zyperos_pixel2`). Over Wi-Fi, `PX2_HOST` is the address Developer
Mode's page shows with SSH over Wi-Fi on (below), or `zyperos.local`.

`make px2-payload` (`tools/px2-payload.sh`) builds the launcher, MP4,
portkeys, System Update's program, p2d (NES Player 2's server), p8fx.so (the
library your PICO-8's script loads before SDL for its Shader and Border,
src/p8fx.h) and Diatom from this working tree in Docker, against plastron's SDK: a read-only volume
that plastron's `make sdk` packs from its last Pixel 2 build, with the
cross-compiler and the system's own SDL2, FFmpeg and ALSA, so what links is
what runs. Then `mk/payload.sh` lays
it out with the cores from the main checkout's `vendor/`. The objects stay
unstripped in `build/px2/` and `diatom/build/pixel2/`; the payload's
programs are stripped, as the image's are. The launcher is compiled one
object a source file (`build/px2/obj`, `mk/cross.mk`), each with the headers
it read, so a change to one file compiles that file and the files that
include it, then links; a build against another SDK, or with another
version or compile line, compiles them all again. The link is the one the
single command did, so the launcher is the same bytes either way. Nothing it uses can change under
it, so any number of worktrees build at once, with no lock and no plastron
build volume: about 20 seconds from clean. `PX2_SDK` picks an SDK by name
(`docker volume ls --filter label=plastron.sdk`); without it, the newest.
Make the SDK again in plastron after a change to its libraries.

A whole card image is plastron's: `make -C plastron` for the Pixel 2, or `make -C plastron brick
BRICK_TARGET=zyperos-rebuild` for the Brick. A full `make brick` also writes a
card image. For work that builds the system again and again, `DEV=1` (`make
-C plastron DEV=1`, `make -C plastron brick DEV=1`) makes the output a
development output: it compiles through ccache, builds again every input that
changed (a package's patches, the kernel's, the `.config`), and goes by
contents, so the same source from another worktree is not built twice
(`plastron/README.md`). Without `DEV=1` a build is as it always was, and a
release never is one.

On a plastron development build, SSH runs over the USB cable with the key
from `plastron/local/authorized_keys` (ignored by git): `ssh root@10.55.0.2` reaches the
Brick and `ssh root@10.42.0.1` the Pixel 2. Copy files with `scp -O`, since no
SFTP server runs, and into `/tmp` first; check that the checksums match on
both sides before you move a file into `/mnt/SDCARD/ZyperOS/`. Release builds
carry no key and run no SSH server until Developer Mode is on (MENU > About).

### Developer mode

A release image starts no SSH and holds no key, so a shell on it is yours to ask for. Developer mode is how, and it stays out of the way until you look for it. On **MENU > About**, on the first page (the one with the version), hold **L2 and R2 together for five seconds**. A **Developer Mode** row appears with a bar that fills as the seconds pass, and letting go of either button, or leaving the page, starts again. When it is full, a message says **Developer Mode is on**, and the row stays on the first page as *on*. **MENU > Developer Mode** then opens its page, where **A** turns developer mode off again.

With it on, the Pixel 2 behaves like a development build for as long as a computer is on the cable. The USB link stays up with no question, and SSH runs on the cable's address, `ssh root@zyperos.local` or `ssh root@10.42.0.1`, never over Wi-Fi. Closing a page or starting a game does not end it, and when the launcher restarts (a `tools/px2-deploy.sh` run does that) the sessions drop and SSH is back a few seconds later, so a key logs in again. Pulling the cable out, or turning developer mode off, ends it. There are two ways in, and both are yours:

- **A key from the card.** Copy your public key, `id_ed25519.pub` or any `.pub` file (or an `authorized_keys` file), into the `Developer` folder at the card's root; the Pixel makes the folder the first time. Plain public keys only: a line with options in front, such as a forced command, is skipped. A key taken is kept with the ones `ssh-copy-id` saves. The folder is outside `ZyperOS/` because an update swaps that one for the new version's.
- **A password for this plug.** The Pixel makes eight random characters each time a computer is plugged in. Only **MENU > Developer Mode** shows them, and nothing keeps them. Five wrong ones lock SSH until the next plug, as File Transfer's PIN does. This is also how the first key gets in without taking the card out: sign in with the password, then run `ssh-copy-id root@10.42.0.1`.

**MENU > Developer Mode** opens the page: the command, the password, where the keys go, how many it read, and SSH's status. **A** there turns developer mode off; **B** closes the page and leaves everything running. Nothing is shipped in the image: no password, no key. A development image is unchanged: its SSH starts at boot with the key in `plastron/local/authorized_keys`, and `tools/px2-deploy.sh` works as before.

### SSH over Wi-Fi

With Developer Mode on, its page (MENU > Connections > Developer Mode) has
an **SSH over Wi-Fi** switch, Off as it ships (`src/sshlink.h`,
`sshlink_want`). On, SSH
keeps running while Wi-Fi is up: dropbear listens on port 22 of every address
(Wi-Fi's, the hotspot's and the cable's) and takes **keys only**. It runs with
`-s`, so no password is offered at all, and root's password in its namespace
is `*`; the Developer page's password, made for each plug, signs in only over
the cable and only while no Wi-Fi interface exists. That is the whole guard:
Linux answers for every one of its addresses on every interface and the
kernel has no netfilter, so the password must never be up while a radio is
there, whichever address it was meant for. Off, Wi-Fi stops SSH as it always
has. Turning Developer Mode off turns SSH over Wi-Fi off too.

So the key goes on first, over the cable (`ssh-copy-id` with the Developer
page's password) or as a `.pub` file in the card's `Developer` folder; with
none, the page says "SSH needs a key". The page then gives the commands,
`ssh root@<address>` and `ssh root@zyperos.local` (the name is probed for on
the Wi-Fi network first, as Wi-Fi File Transfer's is). A
signed-in session holds Auto Off as a File Transfer browser does; sleep ends
it as before. Wi-Fi File Transfer leaves it running.

```sh
PX2_HOST=192.168.1.23 tools/px2 status
ssh -i ~/.ssh/zyperos_pixel2 root@zyperos.local
```

`tools/px2` takes `/tmp/px2-lock` for any device on port 22 whatever its
address, so a session over Wi-Fi and one on the cable wait for each other.

### SSH while a port runs

A port (PortMaster, a port, a standalone emulator) runs from `launch.sh`
after the launcher exits, and Developer SSH was the launcher's child, so
every port cut the sessions off. Now portkeys, which runs for the whole port,
keeps Developer SSH up meanwhile (`tools/pkssh.c`, `sshlink_want_port`):
keys only on every link, since the password lives on the Developer page and
no port shows it; on the cable's address while its link is up; over Wi-Fi
with SSH over Wi-Fi on, as under the launcher; nothing with Developer Mode
off. The in-game menu over a port (`zyperos.elf --port-menu`) leaves it
alone. Sessions drop for about a second as a port starts and as it ends (a
dropbear cannot change parents), and a key signs in again at once. Auto Off
does not run over a port, so nothing there holds it.


Big copies go over the cable (or Flash Drive), not Wi-Fi, until the radio is
faster. The Pixel 2's Wi-Fi is a USB 802.11n adapter with one small antenna,
and SSH goes only as fast as its link: on 2026-11-15, at -73 dBm (72 Mbps
link), pings lost 30 to 55% and took up to 2 s, and an `scp` moved 14 MB in
12 minutes, about 20 KB/s. That is the radio, not a setting: the image
already keeps the adapter's power saving off (`rtw_power_mgnt=0
rtw_enusbss=0` in `modprobe.d/rtl8188eu.conf`), and `wifi-watch` only logs
the signal. A payload deploy (a few MB that changed) is fine over Wi-Fi; a
PSP or PS1 game is not. A sign-in on such a link can time out before
dropbear's banner arrives; it works on the next try.

Every image has `px2-shot`, `px2-input`, `px2-open`, `px2-perf` and
`px2-snap` in `/usr/bin`, so over SSH a screenshot is `px2-shot /tmp/s.ppm`,
a press is `px2-input a`, a game is
`px2-open "/mnt/SDCARD/Roms/NES/ZyperOS Test.nes"`, a performance report is
`px2-perf 10` and a game's saves set aside are `px2-snap save ROM`, with
nothing to build or copy. `tools/px2` drives them from the Mac (below).

`px2-open` talks to the launcher's command channel (`src/ctl.h`): a Unix
socket, `/run/zyperos/ctl.sock`, in a folder only root can open, with no
network side, so on a release image it is reachable only through Developer
Mode's SSH. The launcher opens the game as A on it in its shelf does: the
system its `Roms` folder is, the core that shelf runs, the auto state, the
in-game menu, the logs, and a port or a `run:` program through `launch.sh`. A
path that is not a game under `/mnt/SDCARD/Roms` (`Ports` included) is
refused, a link that leads out of it too.

- `px2-open PATH` opens it; `--fresh` without its auto state this once (the
  auto state the game writes when it ends replaces the old one), `--core NAME`
  on another core its shelf offers this once (`--core Flycast` for the
  Dreamcast's standalone Flycast), `--wait-running SECONDS` waits for its
  first frame and fails if it ends or never starts. A game already running
  is quit first, as the in-game menu's Quit quits it, and a port too (USR2 to
  `launch.sh`, the port menu's Quit). On any other screen over the shelf (a
  menu, MP4, a message) it is refused as `busy`: `px2-input b` back first.
- `px2-open --quit` quits the running game as the in-game menu's Quit does,
  its menu open or not, and waits for the shelf.
- `px2-open --status` prints one `key=value` a line: `state` (`shelf`,
  `screen`, `starting`, `running`, `paused`, `stopping`, `port`), and for a
  game its `system`, `path`, `core`, `since` (Unix time) and `up` (seconds).
  `px2-open --wait-running 20` waits for whatever is opened.

Its errors say a code and why: `bad-path`, `no-file`, `unknown-system`,
`not-a-game`, `unknown-core`, `needs-bios`, `bad-bios` (a BIOS file of the
wrong size), `not-running`, `busy`, `bad-command`. It exits 0 when done, 1
for a refusal, 2 when no launcher answers or a wait runs out. The launcher
reads the channel on the shelf, on every screen that runs a loop, and ten
times a second in a game; a game started without the resident emulator (its
fallback) has no launcher to answer until it ends. A game copied onto the
card after the launcher read the shelves is found all the same: an open from
the shelf reads them again.

For the Pixel 2 with nobody holding it, `tools/` has more small programs to build
with plastron's toolchain (`aarch64-buildroot-linux-gnu-gcc -O2`, `-pthread`
for the memory test; the SDK has it as `/sdk/bin/aarch64-buildroot-linux-gnu-gcc`
in `docker run --rm -v plastron-sdk-px2-<id>:/sdk:ro plastron-builder`) and run
from `/tmp` as root:

- `px2-shot` (in the image) saves what the panel shows as a 640x480 PPM, from any program,
  and `px2-shot --fps` counts the frames a second it shows.
- `px2-open` (in the image) opens a game, quits it and says what runs (above).
- `px2-input` (in the image): `px2-input down down a` presses the buttons, as the joypad would, and
  `px2-input select+start` presses them together, for an emulator's quit chord.
  `volup` and `voldown` are the volume keys, sent on their own device, and
  `power` the power key on the PMIC's: it turns the handheld off, but in the
  tutorial's power step.
- `px2-perf [SECONDS] [--json]` (in the image) reports on whatever runs, over
  10 seconds by default: the panel's frames a second and the spread of the time
  between them (min, median, 95th percentile, max, from the new framebuffer
  seen at each vertical blank, as `px2-shot --fps` counts), the game's speed
  against the clock and the audio frames Diatom dropped, each core's load,
  clock and governor, each devfreq device's clock (the GPU's, the memory's),
  each thermal zone, Diatom's and the launcher's CPU and RSS and the busiest
  other processes, and the free memory. The game's speed comes from Diatom's
  now file, `/tmp/diatom-now` (diatom/src/now.h): there while a game is
  loaded, naming it, and written four times a second while px2-perf asks
  (`/tmp/diatom-now.ask`), so it costs nothing when nobody does. With a Diatom
  from before it, the last "x of the clock" line in the log stands in.
  `--json` is the same for a script.
- `px2-snap` (in the image, a busybox script) sets one game's saves aside on
  the card and puts them back byte for byte, so a test can play it and leave
  it as it was: `px2-snap save ROM [NAME]`, `px2-snap restore NAME`,
  `px2-snap list`, `px2-snap delete NAME`. A snapshot, in
  `.userdata/px2-snap/NAME`, has the game's battery save (`Saves/<stem>.srm`,
  `.sav`, `.dsv`, and the files fbneo, MAME 2003-Plus, Flycast's NAOMI and
  Atomiswave games and OpenBOR keep in their own folders), its Auto state, the
  six slots (the quick slot is one) and their pictures and a DS game's
  `.auto.lang` (`.userdata/shared/.zyperos/<folder>/`), and its rows in the
  library database: its play sessions (its play time and when it was last
  played), its favorite and its N64 Pak, through `zyperos.elf --game-rows`
  (src/gamerows.h), since there is no sqlite3 on the device. Restore puts back
  each file and removes the game's files the snapshot did not have, then the
  rows, and restarts the launcher if they changed (`--no-restart` leaves it).
  Both refuse a game that is open: Diatom's now file names it, `px2-open
  --status` does, or a process was started with it.
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

## Documenting a feature

The README stays short. A significant feature gets one line in its Features list,
and, when the other systems can be compared on it, a row in the comparison on
zyperos.com and in the guide's full table. Its details go in `docs/guide.md`,
under the heading that fits, and its screenshots are in the light theme
(`tools/readme-shots.py`, `res/readme/README.md`). `README.md` keeps the Credits
and the License sections whole, since `check-bgm-tracks` reads the menu music's
credits there.

## Work on the UI without a device

It needs SDL2 with SDL_image and SDL_ttf, and xz: `brew install sdl2 sdl2_image
sdl2_ttf xz` on a Mac, and on Debian or Ubuntu `sudo apt install pkg-config
libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev libsqlite3-dev liblzma-dev
zlib1g-dev libasound2-dev libcurl4-openssl-dev libssl-dev` (or
`tools/ubuntu-setup.sh`, which installs those and the rest the checks use).

`make desk` opens ZyperOS's launcher in a window on your computer as the GKD
Pixel 2 itself, drawn to its measurements around its 640x480 panel, one panel
pixel to a pixel of your display: on a Mac's Retina display, about the
panel's real size. `make desk-brick` is the TrimUI Brick and its 1024x768. It is the code the handheld
runs, built for your computer, so the menus, the settings, the themes and the
languages behave as they do there, at the panel's 60 frames a second, and a
press shows on the next frame. A game runs in the same window, and MENU opens
the in-game menu over it, so a menu such as Core Settings can be tried where
a player meets it.

```sh
make desk                         # the Pixel 2
make desk-brick                   # the Brick
tools/desk --zoom 2 --lang de     # twice the size, in German (xx: every word longer)
tools/desk --no-frame             # the screen alone, no handheld around it
tools/desk --roms ~/Games/Roms    # your own Roms folder, laid out as on a card
tools/desk --fresh                # a new sample card
```

The keyboard is the buttons. The keys go by their place on a US keyboard, so
on an AZERTY one Z is the key marked W, and A the one marked Q:

| Key | Button |
|---|---|
| arrows | the d-pad |
| X, Z | A, B |
| S, A | X, Y |
| Q, W | L1, R1 |
| E, R | L2, R2 |
| Return | START |
| Right Shift or Delete | SELECT |
| Esc or M | MENU |
| - and = | volume down, up |
| [ and ] | brightness down, up (the Brick's own keys) |
| P | POWER |

It is one map, `src/device/keymap.h`, for desk mode, the
[simulator](simulator.md) and QEMU's own window (`make check-keymap` holds
them to it). With the mouse, click and hold any key of the drawn handheld; it
goes down while held, from wherever. The pointer over a key shows its
keyboard key, and holding Option shows them all. The mouse wheel is Up and
Down. A game controller is the buttons too, by their place: the right-hand
button is A and the lower one B, as on the handheld, whatever is printed on
them. In the window, F1 lists all this; Cmd 1, 2 and 3 show the panel pixel
for pixel, twice and three times (Control on Linux), Cmd 0 at its real size
on your display, Cmd B the screen alone or the handheld, and Cmd S saves the
screen to the Desktop.

The first run makes a sample card in `build-native/desk/pixel2` (or `brick`):
the README's library of games as empty files, a game on each system, and
`ZyperOS Test.nes`, ZyperOS's own NES test cartridge (`tools/qemu/make-test-nes.py`:
a box lights for each button held). The card skips the tutorial (`--tour` starts
in it), and what you change in the menus stays on it for the next run.

Games need Diatom's desktop build (`make -C diatom`, which `make desk` runs)
and a libretro core built for your computer. The first run builds FCEUmm, the
NES core, into `build-native/desk/cores`: it fetches the source at the commit
the handheld's own core is from (about 1 MB from GitHub, checked by its
sha256) and compiles it, in about 30 seconds (`tools/desk cores` does it
alone; `--no-cores` never does). So `ZyperOS Test.nes` runs; the empty files
do not, and another system's games need its core in `DESK_CORES` in
`tools/desk`.

A running window takes commands from a shell, one JSON line back, as the
simulator's `simctl` does:

```sh
tools/desk open "NES/ZyperOS Test.nes"   # a game, as A on it in its shelf opens it
tools/desk tap menu                      # buttons: up down left right a b x y l1 r1
                                         # l2 r2 select start menu volup voldown power
tools/desk shot shot.png                 # the panel, at its own pixels
tools/desk shot desk.png window          # the whole window, the drawn handheld too
tools/desk latency down                  # a tap, and ms from it to its frame
tools/desk hints 1                       # every key's name on its key, as Option
tools/desk state | stats | close | quit   # state: the shelf's screen, cursor and focus too,
                                         # and the Screen Colors on the panel (colors)
```

What desk mode cannot show you:

- **Speed and the GPU.** Your computer's processor and graphics draw it. How
  fast a menu moves, how long a frame takes and whether a core keeps up say
  nothing about the RK3326 or the Brick's A133, nor about Panfrost's or the
  PowerVR's quirks. Measure those on the device.
- **The hardware.** The battery, the charger and the side lights, rumble, the
  backlight, the volume and brightness levels, sleep, Wi-Fi, Bluetooth, USB
  and the headphone jack are stand-ins or absent on a computer. The system's
  side of them runs in [QEMU](qemu.md), and the [simulator](simulator.md)
  shows them on a drawn Pixel 2; the rest is the device's.
- **The panel.** Your display's pixels and colors, at the size you choose
  (Cmd 0 is the panel's real size): a word that is easy to read at your desk
  may not be on the Pixel 2's 2.4-inch panel. Screen Colors and Night Mode have no panel to set:
  `tools/desk state` says the colors the display's table would hold.
- **The emulator's device side.** Games run on Diatom's desktop port and
  SDL's renderer: its old screens (CRT, LCD) are not drawn, and the scaling
  filters are near, not the same.
- **The system.** No boot, first-boot setup, System Update, ports or plastron:
  those run in QEMU.

So: desk mode for the menus, the flows, the words and how many presses a
thing takes; QEMU and the simulator for the system; the device for speed,
the GPU and the hardware.

## Releasing, and working without the device

[Releasing](releases/README.md): `make release PUBLISH=1` on the Mac builds the card image and makes the draft release; the tag it pushes has GitHub build ZyperOS Installer for macOS, Windows and Linux, add it to the same release and publish it. Every version's release has the image and the installer together. A release also carries System Update's pack and its list, signed with ZyperOS's release key (`tools/update-key.sh`) ([how it works](updates.md)).

Test kits: a kit (`tools/release.sh --kit`, a pre-release such as v1.0.7-kit.1) is main with its version and its notes, nothing else. What only a kit's tester should run (a device's hardware check, a feature main keeps off until a tester passes it on the device) is in main, behind `ZYPEROS_KIT_BUILD`: the device build (`mk/cross.mk`) defines it, and links the device's kit file `src/device/<id>_kit.c`, only when VERSION has `-kit` in it (`src/kit.h`). A release's version never has `-kit`, so a release compiles the same objects with the same line as if the kit's code were not there, and `make check-kitbuild` holds that; the host's build is never a kit, so every other check holds what a release does. A kit's pages are English only, with no strings in `res/strings`, so they stay out of the translations and their checks. When a kit has passed, its code comes out from behind the switch, or out of main.

Without the device: `make desk` runs the launcher in a window, games and their in-game menu included (above); ZyperOS's whole system runs on a Mac in [QEMU](qemu.md) (`tools/qemu-run.sh`, its image a release's from `tools/qemu-run.sh fetch`, or plastron's `make qemu`), and the [simulator](simulator.md) shows it as a Pixel 2 in a window, its buttons, battery, cable and side lights included. Speed, the GPU and the hardware itself still need the device.

## Debugging on the device

A session on a Pixel 2 nobody is holding goes through `tools/px2`, on the Mac:

```sh
tools/px2 status                          # px2-open --status: what runs
tools/px2 snap save "Roms/NES/Contra (USA).nes" contra
tools/px2 open "Roms/NES/Contra (USA).nes" --fresh
tools/px2 press start:300 wait:2000 a     # px2-input
tools/px2 shot contra.png                 # px2-shot, as a PNG here
tools/px2 perf 10                         # px2-perf; --json for a script
tools/px2 log                             # this boot's zyperos.log; -f follows it
tools/px2 quit
tools/px2 snap restore contra             # the saves as they were
tools/px2 ssh 'ls /tmp'                   # anything else
```

Each command first takes the device's lock on the Mac, `/tmp/px2-lock` (a
folder made with mkdir, so two sessions never both have it, with an `owner`
file naming the session and its command), and lets it go when it ends, on
Ctrl-C and on an error too. A lock someone holds is a refusal that names them
(exit 75), or a wait with `--wait SECONDS`. Remove a lock by hand only when
its owner is gone. Then it reads the card's size, `/sys/block/mmcblk0/size`:
the 32 GB test card (62333952 sectors) goes ahead, and the owner's own 128 GB
card (249737216) or any other only with `--real-card`. A wrong use of a
command is refused before either.

The device is `PX2_HOST` and `PX2_PORT`, as for `tools/px2-deploy.sh`
(10.42.0.1 on the USB cable, or the Wi-Fi address with SSH over Wi-Fi on),
with `PX2_KEY` and `PX2_DEVICE` (the device's id, `pixel2`, for its
`.userdata` folder and log); `--vm NAME` is a QEMU VM of `tools/qemu-run.sh`'s,
with a lock of its own (`/tmp/px2-lock-HOST-PORT`, as for anything on this
computer or on another port, or `PX2_LOCK`). On an image
from before px2-perf and px2-snap, `tools/px2` copies them to `/tmp/px2-bin` on
the device first: px2-snap from `tools/px2-snap.sh`, px2-perf from
`build/px2/px2-perf`, which `make px2-payload` builds beside the payload.

`open`, `quit` and `status` are `px2-open`'s, which opens a ROM through the
running launcher; until an image carries it, `tools/px2` copies it from
`build/px2/px2-open` or `tools/px2-open.sh` where the tree has one.

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
                mk/fetch-vendor.sh from ZyperOS's cores release (a
                pre-release whose files never change; mk/release-cores.sh
                makes a new one when a core changes), the rest built by
                mk/build-*.sh (the small cores' mk/build-small-cores.sh
                --missing runs before every card build, so a new one there
                builds itself on the first card); a core
                ZyperOS patches has a stamp beside it naming its patches
                (mk/core-stamp.sh), which mk/payload.sh and tools/release.sh
                hold it to: run its build script, or `sh mk/core-stamp.sh
                adopt NAME` for one that script already made
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
