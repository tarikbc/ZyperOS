/**
 * The tour's music: one of ZyperOS's own menu music tracks (res/bgm, all of
 * them CC0; scripts/prepare-assets.sh copies them to public/music/). To change
 * the track, change this object, lay src/timeline.ts's scenes on the new
 * track's sections and render again; the end card credits whatever is here. LICENSES.md lists every track's tempo and the
 * downbeat its full band starts on.
 */
export const MUSIC = {
  file: "Leap.mp3",
  title: "Leap",
  artist: "nene",
  license: "CC0",
  /** beats a minute, measured: a 104-bar loop of exactly 160 s */
  bpm: 156,
  /** the downbeat the tour starts on, in seconds into the file: bar 0, the full band */
  startS: 0.0154,
};
