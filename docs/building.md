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
| `check-corner` | the widest status corner clear of the focused card in each direction, with a long title and every tall panel a grid step under it, both themes, as each shot checks its own frame; the widest Time Left text in every language fits its room; and MENU > Clock and MENU > Battery Icon turn over and are saved (builds the host launcher; skips without SDL2) |
| `check-status` | the shelf's status corner: the battery's warnings at 10% and 5% and none on the charger, the signal's steps and `wpa_cli signal_poll` read for them, and the marks drawn as the status lab drew them, probed in both themes (no SDL) |
| `check-audioout` | where sound goes given a cable, a headset and a setting - all eight combinations |
| `check-idle` | the Auto Off clock, including the charger case and the counter wrapping |
| `check-cheevos` | the achievement half: parsing and filtering |
| `check-rahash` | the C and Python ROM hashers agree, over the whole library |
| `check-raset` | the C and Python set converters agree (needs `RA_USER`/`RA_PASS`) |
| `check-artscrape` | two name normalizers agree on every candidate |
| `check-artrun` | whole Box Art runs against a fake network: Replace keeps a cover it cannot better, and a checksum names the ROM in a zip, not the readme beside it |
| `check-hare` | nothing on the file server is reachable without the PIN; the page is named after its link (`/link.js`: the cable's or Wi-Fi's, never kept by a browser), and a web folder from before that still serves its page; an upload or an update's file of an exact multiple of 128 KiB sent with the page's trailer is stored to the byte, one sent plainly as sent, and one with a trailer it cannot make sense of refused, the file of its name kept and no part-file left |
| `check-httpd` | request parsing, including the malformed ones |
| `check-xfer` | upload paths cannot escape the directory they were aimed at |
| `check-db` | a shipped default never overwrites a choice, and the two scopes stay apart; fast forward's frameskip on mGBA, Snes9x 2010 and Genesis Plus GX at each core's own automatic value on a new card, and a value the player set kept |
| `check-gamedel` | Delete Game on a scratch card holding a cartridge, a multi-disc set in its folder, a `.cue` with its tracks and an arcade set beside `neogeo.zip`, each with saves, states, a cover, a video and a set: exactly the game's files go, never a neighbour's, never `neogeo.zip`, never a file another game shares; its rows in the library database go and the neighbours' stay; a sheet or a folder that leads out of its folders is refused with nothing touched. Then the real launcher, headless (`--delete-now`): the shelf after, in place, on the next game or the one before, from Favorites, down to the empty home (builds the host launcher; that part skips without SDL2) |
| `check-media` | A game's Cover and Video page, what a device cannot show: where a cover came from as its record's line in the library database; Use as Cover and Put Back on a scratch card, the cover there was to `.media/.before` and the records with it, both ways, a copy that fails changing nothing; an MP4's length from its `mvhd`, and crafted MP4s (a box of size 0, a 64-bit size past the file, under its header or negative, a box past its parent or the file, an empty or short `mvhd`, 5000 boxes) that all give none and read nothing out of bounds, built with AddressSanitizer and UBSan where the compiler has them |
| `check-snapsize` | A video snap of any shape (640x480, a Nintendo DS game's 320x480, a Game Boy game's, 534x480, a width of 1, odd ones) published as Muse publishes it, read into its texture and drawn on the shelf and on a game's Cover and Video page: its own shape where it is drawn, a texture never under 640x480 (the Pixel 2's GPU faulted on narrower ones), nothing out of bounds under UBSan and, on a Mac, Guard Malloc (builds the host launcher's objects; skips without SDL2) |
| `check-storage` | MENU > Storage's numbers on a scratch card: each part, each game's bytes (what Delete Game gives back) and the bar held to the clusters the files take; a second start reads the cache and lists no folder again, a game added lists its own folder alone, a cache it cannot read is measured again; the walk on its thread ends and stops when asked; Unused Art and Old Save States delete exactly their files. Then the real launcher, headless (`--storage delete-open`): Delete Game's question opens on the page from nothing, never as a swap from the panel before it (builds the host launcher; that part skips without SDL2) |
| `check-stats` | play time is recorded, and a LAUNCH still writes nothing |
| `check-sort` | a shelf sorts the way it says it does, and the same shelf twice the same way |
| `check-bt` | a device name from the air is only ever data; an address is validated |
| `check-pkkeys` | the volume keys over a port: MENU+VOL is the backlight whichever device is read first, the kernel's autorepeat is no step, a hold repeats on the launcher's clock, and the levels saved are the ones pressed |
| `check-systemart` | bounded sprite compilation, original frame colors/alpha/timing, disposal, deduplication, source attribution, catalogue coverage and generated output consistency |
| `check-design` | DESIGN.md's tables, `src/design.h`, the mock kit's `tokens.js` and Wi-Fi File Transfer's colors are what `tools/designdef.py` writes, and every text pair the launcher and the page draw meets its contrast |
| `check-strings` | every string in `res/strings/` in all five languages with English's printf conversions, every ID used and defined, every character in the font that draws it, and `src/i18n_ids.h`, `src/i18n_tables.c` and `res/web/i18n/` what `tools/i18n.py` writes |
| `check-i18n-shots` | the main screens in all five languages and the pseudo-locale, from the host build: no string from `res/strings` cut, broken past its lines or off the screen in any language, and a list of what the pseudo-locale's longer words do not fit |
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
| `check-zyhome` | MENU+START's chord (tools/zychord.h): held a second in either order it fires once, again only after both are let go, and a tap of both does nothing |
| `check-portkeys-flow` | The actual portkeys reader and loop with fake syscalls: poll races, pending release, overflow, removal, noise, and clocks; and over a port, the power key and an empty battery (two readings 5 s apart, off the charger), each one USR1 to launch.sh and never one to init. |
| `check-portal` | the File Transfer page in headless Chrome over the real routes and a scratch card: its folders' art, the hidden dot folders, a console's `.media` dropped or picked going up with its covers and nothing else hidden, files of an exact multiple of 128 KiB (which Safari over the cable never hears the answer to) sent with the trailer and landing to the byte, the room, the time left, the handheld's stop, the update over the cable, and its name after the link it came over, USB File Transfer or Wi-Fi File Transfer, opened over one and then the other (needs Chrome) |
| `check-rasubmit` | what a refused unlock means for the queue: settled, the account's, or that one row's; no request is made |
| `check-signin` | RetroAchievements sign-in against a stand-in for curl: a token signs in; a 401 says the user or password is wrong; a 403, a 503 or a redirect says retroachievements.org would not sign you in, with the number; a reply cut short, too long, or nothing at all is never read as a token; and as curl, the check holds that the password is never on its command line, every file the request makes is new and 0600 from the start and gone after, and the log names the failure and no credential |
| `check-noticeq` | the pop-ups over a game, one after another (src/noticeq.h) |
| `check-badges` | achievement badges (src/rabadge.h) through the real fetch with a stand-in for curl: kept beside the set; one curl for what is missing and nothing else; starting it and drawing 4,000 icons while it runs never wait for it; a badge that has not come is a plain one of the same 64 pixels, never a hole; a 404 page is never kept as a badge; then the plain badge and Zyper as pixels (src/noticeart.h), in both themes, his shadow on Beard only. No network (needs SDL2 and SDL2_image; skips without them) |
| `check-badge-shots` | the unlock notice with Zyper at its right end and its badge, or the plain one where the card has none; a count's notice with the plain one and no Zyper; the set's arrival with neither; the achievements list with a badge in every row, plain until the art is there, and the rows' words not moving when it comes; in both themes (builds the host launcher; skips without SDL2) |
| `check-sshlink` | USB File Transfer's SSH: what it counts in dropbear's log (sign-ins, wrong PINs, a port it cannot have), names that try to say otherwise, and the shadow it gives dropbear |
| `check-usblink` | USB charges only until asked: a computer is asked about once per plug and a charger never, every choice ends with its screen or its cable, and Developer Mode keeps the link |
| `check-devmode` | developer mode on a release image: the unlock hold (L2 and R2 for five seconds, a bar that only rises, and letting go early, another page or holding on starting again), the setting kept in the card's database, a random one-time password in its alphabet, and only public keys without options taken from the card's key file, once each |
| `check-ctl` | the launcher's command channel, the one `px2-open` talks to: the command lines, good and bad; a game's path split into its system's folder and the file, and anything else turned away (outside `Roms`, relative, `..`, a link that leads out); the system found by its folder in either case and the game by its launch path or its disc folder; a core by its name or its Core row's; the socket in a folder in `/tmp`, closed to others, a command in and an answer out, and the port record |
| `check-sgdb` | SteamGridDB on the device: the real client and a whole run against a fake API (the key in a header and never a URL, the match, the formats, the refusals, a key refused with a 401 or 403 when it is entered said so and the one before it kept), its title and ratio held to tools/sgdb-check.py |
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
| `check-battery-eta` | the time left on the battery: the estimate from how long a percent takes to fall, against a simulated gauge at several loads, across the clock's wrap, a change of load, a stall, the cable and a gauge that corrects itself; and the corner's rounding |
| `check-dslang` | the Nintendo DS's language: each language MENU > Language has maps, in Auto, to the DS's own or to English (Português (Brasil) and 简体中文 have none), the Game Language row's eight choices round trip through the database and go round both ways, the core's option and its values are the patch's, and the launcher sends it first with every DS game and with no other; the row is on the DS shelf's menu in every language with nothing cut, and Left, Right and A go round it, saved (builds the host launcher for the second half; skips without SDL2) |
| `check-corestate` | a game's resume when its auto state was left by the shelf's other core (the N64's ParallelN64 and Mupen64Plus-Next): Diatom's state header read back, and the start fresh or resume (src/corestate.h) |
| `check-dsresume` | a Nintendo DS game's resume by its language: the firmware's language as DSperate's direct boot takes it, the language a game starts in for each option, the `.lang` beside its auto state, and a game left in another language started fresh (src/dsresume.h) |
| `check-core-stamp` | the patched cores' stamps (mk/core-stamp.sh): a stamped core passes; a core built before a patch changed, one changed after its stamp, one with no stamp and a patch no core claims each fail, with what to run |
| `check-px2-perf` | px2-perf's arithmetic: percentiles as a spreadsheet gives them, the frame-time spread from the times new frames were seen, a process's CPU ticks from a /proc stat line whose name has spaces and parentheses, and the speed against the clock from two of Diatom's now files, refused across two games or a Diatom started again |
| `check-px2-snap` | px2-snap on a scratch card: a game's .srm, states, slots and pictures, fbneo's, a NAOMI game's and a ScummVM game's own files, never another game's that starts with its name; its play sessions, favorite and Pak through the host launcher's `--game-rows`, and no other game's; a test's changes undone byte for byte, the files it made removed; an open game, a name that is a path, a name taken, another game's rows and a launcher with no `--game-rows` refused or said (builds the host launcher; skips without SDL2 or sqlite3) |
| `check-px2` | tools/px2 with a stand-in for ssh: wrong uses refused before the lock or the device, the test card and a VM's card go ahead and any other only with `--real-card`, the lock's owner file, a lock held refused or waited for, the lock let go after a failure, a TERM and a Ctrl-C, and each argument one word on the device |
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
| `check-pminstall` | what the Install PortMaster page says in each state (src/pminstall.c), and the Ports shelf's PortMaster entry added to a listing that lacks it, once |
| `check-pmhelper` | the PortMaster installer (res/portmaster/pm-install.py) against a made-up release in a folder: an install, a second run, a lost PortMaster.sh put back, an MD5, SHA-256 or size that does not match, members outside PortMaster/, a release list that makes no sense, runtimes kept, a second run at once turned away |
| `check-pmart` | PortMaster's covers in both themes and its page's icon (res/portmaster/*.png) are what tools/genportmaster.py draws, pixel for pixel (needs Pillow) |
| `check-portdoc` | what a port's own files say for the Controls pages of the in-game menu over it (src/portdoc.c: port.json, its gptokeyb keymap, its README's controls) and the pages they make, on the made-up ports in tools/fixtures/ports and the forms porters write by hand; PortMaster's own page; the menu over every port |
| `check-boottheme` | the boot screen's theme and language words (DESIGN.md 11.21) |
| `check-px2-push` | plastron's `make push` (scripts/px2-slot.sh): it writes the system slot the Pixel 2 is not running and never the running one, a card with one system or a command line that does not add up gets nothing, bootstate.txt edited as System Update does, and the device's script parses and writes no partition by number |
| `check-captive` | Wi-Fi File Transfer's hotspot, its pure half: the DNS answer for every name, the Host check that tells a phone's internet check from the page, the QR code's text and the password |
| `check-mdns` | zyperos.local's responder (src/mdns.c): the queries a Mac sent, read off the wire, each answered as RFC 6762 says; every piece of them and random bytes under the address sanitizer; two handhelds on one Wi-Fi named apart on a pretend network |
| `check-linksteps` | File Transfer's link steps (src/linksteps.c): the cable, the address and the browser, each hint at its time, a replug, Wi-Fi, and the device's /proc/net/arp and UDC state |
| `check-usbdrive` | plastron's usbdrive (src/drive.c), the card as a USB drive: never exported while mounted or held; an export ends on the computer's eject, the cable out for 1.5 s, POWER, or no computer in a minute; the page's numbers (src/drivestat.c): the totals and speeds as the page shows them, each way idle at 0 two seconds after its last sector, a pause that keeps the speed, both ways at once, never redrawn while idle, a counter that goes back, the clock's wrap; and the atlas (src/atlas.c), read as untrusted: a broken one refused (a missing glyph, a glyph taller than its slots, rects that overflow or are inverted, a huge picture), a row from the left or right-aligned, cut at its slot, the slot cleared first, and a layout that skipped the parser still drawing only inside its slot |
| `check-usbrole` | when the Pixel 2's USB port may be a host (plastron's usbrole, src/rule.c): the nine plugs of a Mac traced on 2026-11-10 never host unanswered; a DAC only on USB Accessory for its own plug; the quick swap (a Mac's cable out, an adapter in a second later) and a plug made during a game asked about and powered on the answer; Automatic after its 2 s settle, a computer on the bus ending it, and the Mac whose port drops and comes back after each feed (2026-11-10, Automatic) fed at most once, its next plugs asked about until the port rests empty 3 s; never with power in, a charger detection or a failed reading |
| `check-clients` | File Transfer's browser count (src/clients.c): browsers, not sockets; one tab with five keep-alive connections is one, two browsers two |
| `check-pace` | when a panel screen draws (src/pace.c): a still one its heartbeat (about 610 frames in 10 minutes, not 36,000), a change, the redraw clock's moment or anything moving at once, the memory's slow clock after 2 s still |
| `check-keyhints` | button hints (src/keyhints.c), the one component for what each button does: rows in order and in their room, lists packed and read back in all eight languages |
| `check-usbpower` | What did you plug in?, the launcher's half of usbrole (src/usbpower.c): usbrole's state lines, the request and the setting written whole and never through a link, asked once per plug after 2 s (never on Automatic unless a computer paused it), the replug message for a plug a computer showed up on or came back to while paused |
| `check-qr` | the hotspot's QR code, read back by zbarimg where the host has it |
| `check-home` | the home's choice, labels, motion, cached music, renderer state, bounded assets, worker isolation and missing art, on the launcher's own objects |
| `check-home-shots` | the home screens as the launcher draws them in both layouts and themes: footer inside the inset, the song, a flip, a move (builds the host launcher) |
| `check-np-footer` | the song's name in the top bar slides when too long and stands when it fits, and Now Playing and a video draw their keys as a row of hints between the margins, in both themes (builds the host launcher) |
| `check-favorites-shots` | the Favorites shelf names each game's own system right under its name (under one line or two), a console's own shelf does not, and every line keeps its margin, in both themes (builds the host launcher) |
| `check-artbadge` | the art badge (src/artbadge.c): every badge, the heart, the album's notes, the book and the video, in both themes at every size a row draws one, sized and placed with its card, its glyph 3:1 on its disc and inside it, the shadow below, the heart symmetric (no SDL) |
| `check-artbadge-shots` | the badge on the real shelves: a heart on a favorite's cover and on no other, smaller on a side card, on Favorites too; on MP4's All an album, a book and a video each with its own badge, and none on Music alone, in both themes (builds the host launcher) |
| `check-layout-shots` | a game page's play time sits where Favorites' system line does, and over a paused game the clock is at the top left and the battery's text at the top right, in 8 languages and in both themes (builds the host launcher) |
| `check-games` | the games row: the aspect, the wrap, tiny lists, rapid reversals and bounded jumps, on the launcher's own objects |
| `check-cardpaint` | the card for a game or an album with no art, drawn on an art worker: the frame's pixels byte for byte, for every kind of title and both themes, a game's and an album's; in the faces of the language it was asked in; never handed to the shelf after a turn of the theme; the fonts closed and opened again around a fallback game while cards are asked for, with none drawn while they are down; and the frame's to make with no faces or no worker, on the launcher's own objects |
| `check-putpace` | finished pictures put on screen a few a frame: the pace's rules with the costs handed in, then the workers' own cards on a software renderer, slow uploads included - every frame inside its share or one picture, every picture in its own slot in a few frames, none lost when more finish than the ring holds, a loop with nothing pressed drawing until the last is in, and the picture a screen waits for out of its turn, on the launcher's own objects |
| `checkmark` | src/zypermark.h, Zyper as triangles, is what tools/zyperdef.py generates |
| `check-installer` | ZyperOS Installer's version is ZyperOS's, and its page wears this repository's colors, fonts and Zyper (its Rust tests run in CI) |
| `check-from-rogue` | tools/from-rogue.sh's table against systems.cfg and against the folders on a ROGUE OS card |
| `check-importers` | the computer-side importers (--meta, --titles) on a scratch card: a title written over a game's details keeps them (builds the host launcher) |
| `check-diatom` | Diatom's own suite (diatom/Makefile's check) on its desktop port with the stub core: the port seam, core facts, achievements, screens, the protocol, the state plane, resident cores (skips without SDL2) |
| `check-simulator` | the simulator's Swift tests: its VNC client, the px2sim link, the state lines and the control socket's commands (skips without Swift) |
| `check-px2sim` | px2sim's choice of the keyboard it takes in the QEMU image: QEMU's virtio or USB one, never a uinput one such as gptokeyb's Fake Keyboard (host compiler, no VM) |
| `check-sidelights` | what the launcher tells the side lights' gauge: the lines as plastron's gauge reads them, the FIFO writer that never waits and never dies of SIGPIPE, and when a File Transfer batch is over |
| `check-building-doc` | this table has a row for every check in the Makefile's CHECKS, and none for a check that is gone |
| `check-blog` | zyperos.com's blog, built as Cloudflare Pages builds the site (mk/blog.py): title and description lengths, structured data, every link and picture reaching a file (or a path in `_redirects`, such as `/tip`), every post in the sitemap and llms.txt, and the landing page's colors |

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
Language's panel, the cursor on ROW. `python3 tools/i18n-shots.py --keep DIR`
writes the main screens in every language (`make check-i18n-shots`).

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

`--info` draws a game's details over the shelf, `--info-delete` the same with
the cursor on Delete Game, and `--delete-ask` Delete Game's question, the
cursor on Cancel (DESIGN.md 11.31). `--delete-now` deletes the game under the
cursor as Delete Game's yes does, on the card the shot is given, and the shot
is the shelf after it; stderr lists every path that went.

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
with an isolated demonstration library; `--art` gives its games their real box
art from libretro-thumbnails, cached outside the repository. See
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

A whole card image is plastron's: `make -C plastron` for the Pixel 2, or `make -C plastron brick
BRICK_TARGET=zyperos-rebuild` for the Brick. A full `make brick` also writes a
card image.

On a plastron development build, SSH runs over the USB cable with the key
from `plastron/local/authorized_keys` (ignored by git): `ssh root@10.55.0.2` reaches the
Brick and `ssh root@10.42.0.1` the Pixel 2. Copy files with `scp -O`, since no
SFTP server runs, and into `/tmp` first; check that the checksums match on
both sides before you move a file into `/mnt/SDCARD/ZyperOS/`. Release builds
carry no key and run no SSH server until Developer Mode is on (MENU > About).

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
  menu, Muse, a message) it is refused as `busy`: `px2-input b` back first.
- `px2-open --quit` quits the running game as the in-game menu's Quit does,
  its menu open or not, and waits for the shelf.
- `px2-open --status` prints one `key=value` a line: `state` (`shelf`,
  `screen`, `starting`, `running`, `paused`, `stopping`, `port`), and for a
  game its `system`, `path`, `core`, `since` (Unix time) and `up` (seconds).
  `px2-open --wait-running 20` waits for whatever is opened.

Its errors say a code and why: `bad-path`, `no-file`, `unknown-system`,
`not-a-game`, `unknown-core`, `needs-bios`, `not-running`, `busy`,
`bad-command`. It exits 0 when done, 1 for a refusal, 2 when no launcher
answers or a wait runs out. The launcher reads the channel on the shelf, on
every screen that runs a loop, and ten times a second in a game; a game
started without the resident emulator (its fallback) has no launcher to
answer until it ends. A game copied onto the card after the launcher read the
shelves is found all the same: an open from the shelf reads them again.

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
(10.42.0.1 on the USB cable), with `PX2_KEY`; `--vm NAME` is a QEMU VM of
`tools/qemu-run.sh`'s, with a lock of its own (`/tmp/px2-lock-HOST-PORT`, as
for any device but the Pixel 2 at 10.42.0.1:22, or `PX2_LOCK`). On an image
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
                mk/fetch-vendor.sh, the rest built by mk/build-*.sh; a core
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
