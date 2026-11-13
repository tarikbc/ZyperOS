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
back, video snaps, box art, quick save, the in-game controls, NES Player 2 on a
friend's phone, File Transfer over Wi-Fi (its way in, then a browser) and over
the cable (a phone, a computer, Flash Drive), the hotspot, an empty card filling
up, File Transfer while a game plays, System Update, battery care, MENU's tabs
and a theme of your own, achievements earned offline, MP4, PortMaster, Nintendo
DS and the eight languages.

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
  panel, the cable's questions, Now Playing, the charging screen, the first
  start, Storage on a full card. Where a shot takes a
  time (`--tour`, `--charging`, `--nowplaying`), there is a picture every
  sixtieth of a second, so it moves as on the handheld; otherwise each picture
  is held, as a cut from one row to the next.

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
  frame of the game's snap in place of its test picture (no Muse on the host).
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

`src/reddit/` is a second composition, `Reddit105`: what 1.0.5, 1.0.4 and
1.0.3 added, in 56 seconds at 4:5 (1080x1350), the tallest a phone's feed
shows at full width. It plays muted there, so every feature is named in large
words over the screen, with the release it came in; "ZyperOS 1.0.5" is on
screen from the first frame. It uses the tour's takes and music, and a few of
its own (`saver`, `rename`, `renamed`, `flashdrive`, `usbsteps`, `update-bg`,
`charge-full` in `footage/takes.py`). The shaders scene shows the README's
picture of the six looks on Balloon Fight, made in the QEMU machine with
Diatom's own shaders, one look at a time.

```sh
python3 video/footage/takes.py      # with the tour's SNAP_* settings, for the screensaver's snap
sh video/scripts/prepare-assets.sh
cd video && npm run stills:reddit   # out/reddit/contact-sheet.jpg
npx remotion render Reddit105 out/reddit/zyperos-1.0.5-reddit-raw.mp4 --crf=6
sh scripts/finish-reddit.sh         # the master, outside the repository
```

`src/reddit/timeline.ts` lays its scenes on the music's sections, as the
tour's `src/timeline.ts` does (both use its `layout`).
