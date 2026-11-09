# Licenses of what the tour is made of

None of the tour's audio is new to the repository. `scripts/prepare-assets.sh`
copies it into `public/audio/` from `res/`, where ZyperOS already ships it, so the licenses are
the ones those files carry there.

## Music

| File | Title | Artist | Source | License |
|---|---|---|---|---|
| `public/audio/music.mp3` | The Arplands (120 BPM, 2:24) | LordZintick | `res/bgm/The Arplands.mp3`, one of ZyperOS's own menu music tracks; its page is listed in `res/bgm/tracks.json` | CC0 1.0 (`res/bgm/LICENSE-CC0-1.0.txt`) |

The tour plays it from 16.273 s, the downbeat its drums come in on, so frame 0
has the whole band (`MUSIC_START_S` in `src/timeline.ts`), and fades it over the
last 1.5 s. It is the track the handheld plays in the video's own footage.

## Sound effects

`public/audio/ui/*.wav` are the launcher's own interface sounds, `res/sounds/wood/`, the
default UI Sounds. `tools/gensounds.py` synthesizes them from the tones in
`tools/designdef.py`; they are ZyperOS's, under its license (GPL-3.0). The tour
plays them where a press is seen on the screen (`src/sounds.ts`).

The end card credits the music: "Music: The Arplands by LordZintick (CC0), from
ZyperOS's menu music."

## Pictures

- The footage is the launcher's own frames (`footage/takes.py`), and the device
  is `tools/build-pixel2-model.py`'s model (`device/render-device.py`); the fonts
  are `res/fonts` (Lexend, Noto Sans JP and SC: OFL), the lockups `res/readme`.
- The games' box art and screenshots on the sample card come from the
  [libretro-thumbnails](https://github.com/libretro-thumbnails) sets, cached
  outside the repository and never committed. The games and their art belong to
  their owners; only the composed video and its posters are in the repository.
