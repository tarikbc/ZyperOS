# Licenses of what the tour is made of

None of the tour's audio is new to the repository. `scripts/prepare-assets.sh`
copies it into `public/audio/` from `res/`, where ZyperOS already ships it, so the licenses are
the ones those files carry there.

## Music

The tour plays one of ZyperOS's own menu music tracks, `res/bgm/*.mp3`, all of
them CC0 1.0 (`res/bgm/LICENSE-CC0-1.0.txt`; each one's author and page are in
`res/bgm/tracks.json`). `scripts/prepare-assets.sh` copies them to
`public/music/`, and `src/music.ts` picks one: its file, title, artist, tempo
and the downbeat the tour starts on. The end card credits the track named
there: "Music: <title> by <artist> (CC0), from ZyperOS's menu music."

The tour plays **Leap by nene**, picked by Tarik. It is exactly 156 BPM, a
104-bar loop of 160.0 s whose downbeats start 0.015 s in. Its sections, in bars
of 1.54 s (a fill on every fourth bar): the full band 0 to 15, a break 16 to 19,
the band 20 to 23, a break 24 to 27, the band 28 to 31, a bridge 32 and 33, a
steadier part 34 to 47 and a build 48 to 51, the opening theme again 52 to 67,
and a break from 68. The tour runs from bar 0 to two beats past bar 68, fading
over those two beats; `src/timeline.ts` lays its scenes on these sections.

The tracks, measured with librosa (the tempo from a line through every beat;
the full band's first downbeat is approximate until a track is picked and its
start is checked by ear and on the beat grid):

| Track | Artist | BPM | Beat grid from | Full band from | Length |
|---|---|---|---|---|---|
| Apple Cider | Zane Little Music | 100.0 | 0.034 s | ~18.6 s | 197 s |
| Bossa Nova | Joth | 129.0 | 0.278 s | the start | 60 s |
| Hush Hamlet | Zane Little Music | 75.0 | 0.041 s | ~19.2 s | 134 s |
| Ice Cave | TAD | 120.0 | 0.045 s | ~1.0 s | 292 s |
| Leap | nene | 156.0 | 0.015 s | the start | 160 s, a 104-bar loop |
| Napping on a Cloud | congusbongus | 162.0 | 0.212 s | ~23.2 s | 248 s |
| Scheatt | Spring Spring | 170.0 | 0.035 s | ~11.3 s | 282 s |
| Starfall Island | John Oestmann | 99.4 | 0.084 s | ~12.8 s | 139 s |
| The Arplands | LordZintick | 120.0 | 0.273 s | 16.273 s (its drums) | 144 s |
| Truth | HydroGene | 156.0 | 0.031 s | the start | 111 s |

The 1.0.6 video (`src/v106`) has its own track, Tarik's pick: **Level 1** from
Juhani Junkala's "5 Chiptunes (Action)", **CC0 1.0**
(<https://opengameart.org/content/5-chiptunes-action>). It is not in the
repository: `scripts/prepare-assets.sh` fetches the file
(<https://opengameart.org/sites/default/files/level1.mp3>, sha256
`559ad4eb...6b19da`) into `~/Library/Caches/zyperos-video-music`. Measured from
an onset envelope of ffmpeg's decode (5 ms hops): exactly 181 BPM (232 of its 236
strong onsets on the half-beat grid, 2 ms off it), the first downbeat 0.0259 s
in. Of its 60 bars, the last 4 are bars 0 to 3 again, fading (their samples
correlate 0.94 to 0.99 with the opening's): the loop is 56 bars, 74.25 s. The
video plays the loop once from the first downbeat, then from the top again,
joined with a 12 ms crossfade on the bar line, and it is mastered 8.5 dB louder
than Leap, so the video plays it at 0.34 (`src/v106/music.ts`).

The 1.1 videos have their own tracks, both by **Zane Little Music**, both
**CC0 1.0** on OpenGameArt, where they were picked from the list of checked
candidates. His Bandcamp album gives CC BY 4.0 for the same music, so the
videos credit him as CC BY 4.0 asks, the stricter of the two. Neither is in
the repository: `scripts/prepare-assets.sh` fetches each into
`~/Library/Caches/zyperos-video-music`, checks its sha256, and writes it to
`public/music/` from its first downbeat. Measured with the release-video
skill's `measure_track.py` (onsets of ffmpeg's decode, 5 ms hops, a beat grid
fitted for every tempo):

- **It Takes A Hero** (<https://opengameart.org/content/it-takes-a-hero>,
  <https://opengameart.org/sites/default/files/it_takes_a_hero.mp3>, sha256
  `d733e5a4...0a209`), the Discord video's (`src/v110/music.ts`): exactly 140
  BPM, the first downbeat 0.0241 s in, 90 bars of 1.714 s; a quiet break at
  bars 49 to 54, where the video's pair and end card stand. -13.1 LUFS, so it
  plays at 0.64.
- **Space Cadet Training Montage**
  (<https://opengameart.org/content/space-cadet-training-montage>,
  <https://opengameart.org/sites/default/files/space_cadet_training_montage.mp3>,
  sha256 `894d706b...7ee548`), the Reddit cut's (`src/reddit110/music.ts`):
  exactly 130 BPM, the first downbeat 0.0263 s in, 39 bars of 1.846 s; three
  quiet bars of build, the band from bar 3. -12.9 LUFS, so it plays at 0.62.

## Sound effects

`public/audio/ui/*.wav` are the launcher's own interface sounds, `res/sounds/wood/`, the
default UI Sounds. `tools/gensounds.py` synthesizes them from the tones in
`tools/designdef.py`; they are ZyperOS's, under its license (GPL-3.0). The tour
plays them where a press is seen on the screen (`src/sounds.ts`).

## Pictures

- The footage is the launcher's own frames (`footage/takes.py`), and the device
  is `tools/build-pixel2-model.py`'s model (`device/render-device.py`), and the
  1.0.6 video's TrimUI Brick `tools/build-brick-model.py`'s (a reconstruction
  from its published outline, none of TrimUI's marks on it); the fonts
  are `res/fonts` (Lexend, Noto Sans JP and SC: OFL), the lockups `res/readme`.
- File Transfer's page in the 1.0.6 video is `res/web`, ZyperOS's own, shot in
  a browser (`footage/portal-pages.py`) over freeware and homebrew names.
- The games' box art and screenshots on the sample card come from the
  [libretro-thumbnails](https://github.com/libretro-thumbnails) sets, cached
  outside the repository and never committed. The games and their art belong to
  their owners; only the composed video and its posters are in the repository.
