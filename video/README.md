# The ZyperOS tour video

A 1:52 tour of ZyperOS for the README, zyperos.com and anyone who wants to show
it. Every screen in it is the launcher's own: the host build of ZyperOS
(`make native`) draws each frame, as the Pixel 2 would, on a sample library
with the games' real box art. [Remotion](https://www.remotion.dev) puts those
frames in a drawn GKD Pixel 2 beside the site's own words, on the beat of one of
ZyperOS's menu music tracks.

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
  reach by pressing buttons: the in-game menu over a game, Now Playing, the
  charging screen, the first start, Storage on a full card. Where a shot takes a
  time (`--tour`, `--charging`, `--nowplaying`), there is a picture every
  sixtieth of a second, so it moves as on the handheld; otherwise each picture
  is held, as a cut from one row to the next.

`footage/card.py` builds the card the takes run on: the README's demonstration
library (`tools/readme-shots.py`), eight systems of well-known games as empty
files, each with its cover from the
[libretro-thumbnails](https://github.com/libretro-thumbnails) sets, the same as
the Scraper would fetch. The covers are cached in
`~/Library/Caches/zyperos-readme-art` and never committed: the games and their
art belong to their owners.

## Layout

- `src/timeline.ts`: the one place for timing. Scenes in bars of the music
  (120 BPM, so a beat is 15 frames), which clip of which take each scene plays,
  the captions, the camera and the theme.
- `src/Tour.tsx`, `src/parts.tsx`: the composition, the device, the screen and
  the captions. `src/sounds.ts` plays the launcher's interface sounds where a
  press is seen.
- `src/Poster.tsx`: the site's poster, and the README's with a play button.
- `device/render-device.py`: the Pixel 2 straight on, from
  `tools/build-pixel2-model.py`'s scene, with its screen cut out.
- `public/`: made by the scripts above, and ignored. `LICENSES.md` says where
  each part comes from and under which license.

The captions are the site's own words (`site/index.html`), and the device never
moves to the beat: cuts and captions do.
