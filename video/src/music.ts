/**
 * The tour's music: one of ZyperOS's own menu music tracks (res/bgm, all of
 * them CC0; scripts/prepare-assets.sh copies them to public/music/). To change
 * the track, change this object and render again: the scenes keep their pace,
 * snapped to whole beats of the new tempo (src/timeline.ts), and the end card
 * credits whatever is here. LICENSES.md lists every track's tempo and the
 * downbeat its full band starts on.
 */
export const MUSIC = {
  file: "The Arplands.mp3",
  title: "The Arplands",
  artist: "LordZintick",
  license: "CC0",
  /** beats a minute, measured */
  bpm: 120,
  /** the downbeat the tour starts on, in seconds into the file: frame 0 lands on it */
  startS: 16.273,
};
