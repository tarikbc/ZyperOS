# The ZyperOS tour video

A 105-second tour of ZyperOS for the README, zyperos.com and anyone who wants to show
it. Every screen in it is the launcher's own: the host build of ZyperOS
(`make native`) draws each frame, as the Pixel 2 would, on a sample library
with the games' real box art. [Remotion](https://www.remotion.dev) puts those
frames in a drawn GKD Pixel 2 beside the site's own words, on the beat of one of
ZyperOS's menu music tracks (`src/music.ts`; LICENSES.md has every track's
tempo and sections).

As of 1.0.5 it shows, in order: the shelf, the 2-second start against stock
firmware, every system in ZyperOS's own pixel art and the four new ones (PSP,
Doom, ColecoVision, TIC-80), folders and the list, a game started, turned off and
back, video snaps, MENU > Scraper's run with the covers landing, the in-game
menu in its tabs (a disc swapped, a shader and its Strength, buttons remapped
beside the handheld leaning toward each one), NES Player 2 on a friend's phone,
File Transfer over Wi-Fi (its way in, then a browser) and over the cable (a
computer, a phone and its internet, Flash Drive), an empty card filling up,
File Transfer while a game plays, System Update, battery care, MENU's tabs and
a theme of your own, the Achievements tab, MP4, PortMaster, Nintendo DS and the
eight languages.

## Making it

```sh
make native                          # the launcher, for the host
python3 video/footage/takes.py       # the footage: video/public/footage/*.mp4
sh video/scripts/prepare-assets.sh   # fonts, music, sounds, the device render (Blender, once)
cd video && npm install
npm run stills                       # out/contact-sheet.jpg: three stills a scene, to look at first
npx remotion render ZyperOSTour out/zyperos-tour-raw.mp4
sh scripts/finish.sh                 # the master, site/zyperos-tour.mp4 and both posters
```

`npm run preview` opens Remotion Studio to scrub the timeline.

## The footage

`footage/takes.py` makes two kinds of take, both from `build-native/zyperos`
with no window and no change to the launcher:

- **Live** takes press buttons. `rec/rec.c` is a small library loaded into the
  launcher with `DYLD_INSERT_LIBRARIES`. It answers SDL's clock with the frames
  the launcher has drawn, one display refresh each, writes every frame to
  ffmpeg at 60 a second, and delivers a script of presses at their times. So the
  motion in the clips is the launcher's own, frame for frame, however long the
  software renderer took. It also gives the take a fixed time of day (10:08), a
  battery level for the corner, and the menu music set up (silent), so MENU
  shows the card's track and Zyper names it on the home.
- **Shots** are the launcher's `--shot` pictures, for screens the host cannot
  reach by pressing buttons: the in-game menu over a game, NES Player 2's
  panel, the cable's questions, the Scraper page's run, Now Playing, the
  charging screen, the first start, Storage on a full card. Where a shot takes a
  time (`--tour`, `--charging`, `--nowplaying`, `--scraper-page 7`, and
  `--shot-at` after an in-game menu's tab or Controls press), there is a
  picture every sixtieth of a second, so it moves as on the handheld; otherwise
  each picture is held, as a cut from one row to the next. A segment of shots
  can set what it needs on the card first (a shader, its Strength), and clears
  the button remaps, since a shot's presses are kept as the handheld keeps them.

`footage/card.py` builds the card the takes run on: the README's demonstration
library (`tools/readme-shots.py`), well-known games on thirteen systems as empty
files, folders of them on the Game Boy Advance, each with its cover from the
[libretro-thumbnails](https://github.com/libretro-thumbnails) sets, the same as
the Scraper would fetch. The covers are cached in
`~/Library/Caches/zyperos-readme-art` and never committed: the games and their
art belong to their owners.

## A real video snap

The video snap beat plays one input file. By default it is Spyro's libretro
snap clip from the art cache, which is a still picture. To use a real
ScreenScraper snap (`Roms/<system>/.media/<game>.mp4` on a card), name it and
its game on the demonstration card, then render again:

```sh
SNAP_SOURCE=/path/to/snap.mp4 SNAP_GAME='PS1|Castlevania - Symphony of the Night (USA).chd' \
  SNAP_START=2 python3 video/footage/takes.py snap
cd video && npx remotion render ZyperOSTour out/zyperos-tour-raw.mp4 --crf=6 && sh scripts/finish.sh
```

`SNAP_GAME` is the system's tag and the file on the card (`video/footage/card.py`
lists them), `SNAP_START` where in the clip to start, in seconds. The snap plays
from the moment the cover turns, at its own rate. The tour as published uses
Symphony of the Night's ScreenScraper snap from Tarik's card, from 2 s in; the
clip itself stays outside the repository.

## Layout

- `src/music.ts`: the track, its tempo and the downbeat the tour starts on.
- `src/timeline.ts`: the one place for timing. A list of scenes, each whole
  bars of the music, laid on its sections: which takes its screen
  plays and how fast, the device's pose and any punch-in, how it arrives (a whip
  or a spring), its label, and the browser windows of File Transfer. Change a
  scene's beats or its takes there and render again.
- `src/Tour.tsx`, `src/parts.tsx`: the composition, the device and its camera,
  the screen, the labels, the laptop and phone windows (Player 2's phone shows
  the game, Super Mario Bros. 3's libretro screenshot, in its play page's
  picture box), the language grid, and a light and dark pair (the "duo" pose)
  that the 1.0.5 cut leaves out. `src/sounds.ts` plays the launcher's interface sounds
  where a press is seen.
- `src/beats.tsx`: the boot race against stock firmware (a plain loading panel,
  nothing of its interface) and the power off and on, with their timers.
- `rec/snapsub.c`: for the video snap beat, gives the host build's `--snap` turn a
  frame of the game's snap in place of its test picture (no MP4 on the host).
- `src/Poster.tsx`: the site's poster, and the README's with a play button.
- `device/render-device.py`: the Pixel 2 straight on, from
  `tools/build-pixel2-model.py`'s scene, with its screen cut out.
- `public/`: made by the scripts above, and ignored. `LICENSES.md` says where
  each part comes from and under which license.

The labels are short, from the site's own words (`site/index.html`). Cuts,
whips and labels land on the beat; the device does not pulse to it.

To check the pace before a full render: `npm run stills` for the contact sheet,
and `npx remotion render ZyperOSTour out/excerpt.mp4 --frames=300-599` for ten
seconds.

## The 1.0.5 cut for Reddit

`src/reddit/` is a second composition, `Reddit105`: what 1.0.5 added, then
1.0.3's and 1.0.4's USB File Transfer and Flash Drive, in 58 seconds at 4:5
(1080x1350), the tallest a phone's feed shows at full width. It plays muted
there, so every feature is named in large words over the screen, with the
release it came in; "ZyperOS 1.0.5" is on screen from the first frame. It
opens on NES Player 2 as a small story of six bars: MENU > Player 2 and its
code on the handheld, the friend's phone coming in, its d-pad and A pressed
while the handheld plays on; the end card comes back to the two of them. The
rest follows the order the Reddit post lists it. It uses the tour's takes and
music, a few takes of its own (`phone`, `flashdrive`, `usbsteps` in
`footage/takes.py`; `saver`, `rename`, `renamed`, `update-bg` and
`charge-full` are its first cut's), and the phone's presses:
`footage/phone-presses.py` opens the play page on the real p2d in Chrome, as
`tools/readme-shots.py` takes play-phone.png, and takes a picture while a
finger holds the d-pad or A, so the page lights its own keys. The shaders scene
shows the README's picture of the six looks on Balloon Fight, made in the QEMU
machine with Diatom's own shaders, one look at a time, then the Display tab's
Strength row.

```sh
python3 video/footage/takes.py      # with the tour's SNAP_* settings, for the screensaver's snap
sh video/scripts/prepare-assets.sh
python3 video/footage/phone-presses.py   # Player 2's phone, pressed
cd video && npm run stills:reddit   # out/reddit/contact-sheet.jpg
npx remotion render Reddit105 out/reddit/zyperos-1.0.5-reddit-raw.mp4 --crf=6
sh scripts/finish-reddit.sh         # the master, outside the repository
```

`src/reddit/timeline.ts` lays its scenes on the music's sections, as the
tour's `src/timeline.ts` does (both use its `layout`).

## The 1.0.6 video for the Discord

`src/v106/` is a third composition, `Release106`: what 1.0.6 added, in 96
seconds at 16:9, for the ZyperOS Discord, where most of it was asked for. It
opens on the release's name beside both handhelds, true to size: 1.0.6 is the
first release for the TrimUI Brick, so the Brick stands beside the Pixel 2
(`device/render-device.py` renders either; `scripts/prepare-assets.sh` builds
the Brick from `tools/build-brick-model.py`), and its screen is the host
launcher wearing the Brick's own profile (`--device brick`: its 1024x768
screen, its keys, its home without the systems it lacks yet). Then a scene for
each of 21 features at the tour's pace, on a track of its own (`src/v106/music.ts`:
Juhani Junkala's "Level 1", CC0, 181 BPM, which `prepare-assets.sh` fetches and
loops on its 56 bars; LICENSES.md has how it was measured), each a feature the Discord
asked for wearing a small "Asked for on Discord" at its foot; the features only
the Pixel 2 has say so. It ends on both handhelds with the same Grid, and the
end card thanks the Discord.

Its takes are `footage/takes.py`'s 1.0.6 section, on a fuller card
(`card.py`'s `MORE`: sixteen SNES and NES games, so a Grid has rows to page,
and the SG-1000's shelf, whose games have no art, for the name cards). Every
take names its handheld, the Pixel 2's unless it says the Brick's: the host
build wears the desktop's own profile without `--device`. File Transfer's page
is `footage/portal-pages.py`'s: the real page in headless Chrome, as
`tools/readme-shots.py` takes it, with three games picked and a console's own
folder's menu.

```sh
make native
python3 video/footage/takes.py      # or only the 1.0.6 takes, by name
python3 video/footage/portal-pages.py
sh video/scripts/prepare-assets.sh
cd video && npm run stills:106      # out/v106/contact-sheet.jpg
npx remotion render Release106 out/v106/zyperos-1.0.6-raw.mp4 --crf=6 --concurrency=4
sh scripts/finish-106.sh            # the master, and the Discord file at 10 MB or less, outside the repository
```

Each scene's last piece of footage is a `fit` step (`src/timeline.ts`): from
one moment of a take to another, stretched over the scene, so the scenes can be
laid on another track's bars and still show the same thing.

One thing the host cannot draw: a port's loading screen shows the last line the
port prints only on the handheld, where plastron's splash draws it, so its scene
shows the screen a minute in, which says how to go back.

### Frame 0

Discord, Reddit, most players and a page without a poster show a video's first
frame until someone presses play, so frame 0 is a designed frame: `Release106`
opens with its name, its version and both handhelds already drawn, with no fade
in (the first copy posted opened on the bare ground colour), and its opening
takes start after Zyper's song pop-up. `scripts/finish-106.sh` ends with
`scripts/check-first-frame.py` on both files: it saves each frame 0 beside its
video as `NAME-poster.jpg` and fails if it is blank. The tour and the Reddit cut
still fade in from the ground colour; give them the same opening before
rendering them again.

## The 1.1 videos: the Discord's and the Reddit cut

Two more compositions, each with its own track and scenes, so the older
videos stay as they were:

- `src/v110/`, `Release110`: what 1.1 adds next to 1.0.6, in 95 seconds at
  16:9, for the ZyperOS Discord, on both handhelds; a feature the Discord
  asked for wears "Asked for on Discord", and a feature only one handheld has
  says so. The PSP and Saturn scene shows the release notes' speeds, measured
  on my Pixel 2. Its track is Zane Little Music's "It Takes A Hero"
  (`src/v110/music.ts`).
- `src/reddit110/`, `Reddit110`: everything new since 1.0.5 (1.0.6 and 1.1)
  in 60 seconds at 4:5, the 1.0.5 cut's way: large captions in a top band,
  each feature tagged with the release it came in, "ZyperOS 1.1" and both
  handhelds whole from frame 0. Its track is Zane Little Music's "Space Cadet
  Training Montage" (`src/reddit110/music.ts`).

Their takes are `footage/takes.py`'s 1.1 section, on `card.py`'s `v110`
card: the fuller card with your PICO-8's place (two empty stand-in files
where the player's own go, never Lexaloffle's), carts named on its shelf,
three Collections, Screenshots and Recents' last screens (each the game's
libretro snap, as the hotkey would write it), and a game's stand-in cheats.
The Video view plays Symphony of the Night's ScreenScraper snap, as the
tour's video snap does:

```sh
make native
SNAP_SOURCE=/path/to/sotn.mp4 SNAP_START=2 python3 video/footage/takes.py v11-home v11-brick-home ...
sh video/scripts/prepare-assets.sh
cd video && npm run stills:110 && npm run stills:reddit110
npx remotion render Release110 out/v110/zyperos-1.1-raw.mp4 --crf=6 --concurrency=4
npx remotion render Reddit110 out/reddit110/zyperos-1.1-reddit-raw.mp4 --crf=6 --concurrency=4
sh scripts/finish-110.sh            # the master, the Discord file at 10 MB or less, the Reddit cut, outside the repository
```

## The Brick introduction: ZyperOS on the TrimUI Brick, for r/trimui

`src/brick-intro/`, `BrickIntro`: ZyperOS introduced to Brick owners, 4:5,
exactly 2:00 on the 1.1 videos' "It Takes A Hero" (70 bars at 140 BPM), the
Brick alone, every feature named in a caption, no version anywhere (a frame
with one would date the video). Its takes are `footage/takes.py`'s Brick
introduction section: `brick(NAME)` copies a take onto the Brick's profile as
`bi-NAME`, and the `bi` card (`card.py`'s `brick_intro`) is the 1.1 card on
1.0.6. The Rumble list and the Side Lights page are drawn on the Pixel 2's
profile until the Brick's own land. A scene may carry `wait` (a WAITING band
over its stand-in); `scripts/check-waits.mjs` lists them, and the finish
script refuses to make a file while one is left.

```sh
make native
python3 video/footage/takes.py bi-poweroff bi-homegrid ...   # every bi- take but bi-vview,
                                     # and v11-brick-home v11-brick-grid v11-brick-gpu v11-brick
SNAP_SOURCE=~/Documents/ZyperOS/video/snaps/sotn.mp4 SNAP_START=2 python3 video/footage/takes.py bi-vview   # its own run:
                                     # the snap file is made once a run, from SNAP_SOURCE
cd video && npm run stills:brick-intro
npx remotion render BrickIntro out/brick-intro/zyperos-brick-raw.mp4 --crf=6 --concurrency=4
sh scripts/finish-brick-intro.sh     # the master and the Reddit file, outside the repository
```

Since desk mode (1.1), the host build rests in `SDL_WaitEventTimeout` and
reads its keys by scancode, with A on X and B on Z: `rec/rec.c` answers both,
so a still screen's clock still reaches the next press. Its key names gained
`volup`, `voldown`, `brightup`, `brightdown` and `power`.

What the host cannot draw: the volume boost opens over MP4, games and ports,
not over the menus alone, so its scene shows it over MP4 playing, from the
launcher's own `--osd` pictures; a Border round a game is drawn by Diatom, so
its scene shows the Border list's own picture of it.

