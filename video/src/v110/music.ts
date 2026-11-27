/**
 * The 1.1 videos' music, their own (the older videos keep theirs; the Reddit
 * cut's is src/reddit110/music.ts). Picked for the preview from
 * ~/Documents/ZyperOS/video/song-candidates.md, both by Zane Little Music,
 * CC0 on OpenGameArt (his Bandcamp album says CC BY 4.0, so the credit is
 * given as CC BY asks). scripts/prepare-assets.sh fetches
 * each into a cache outside the repository, checks its sha256, and writes it
 * to public/music/ from its first downbeat, so the file's first sample is bar 0.
 *
 * Measured with the release-video skill's measure_track.py (LICENSES.md says
 * how):
 *   It Takes A Hero: exactly 140 BPM, the first downbeat 0.0241 s into the
 *     file, 90 bars; a quiet break at bars 49 to 54, the band back on 55.
 *   Space Cadet Training Montage: exactly 130 BPM, the first downbeat
 *     0.0263 s in, 39 bars; three quiet bars of build, the band from bar 3.
 *
 * To change a track: change its object (and prepare-assets.sh's fetch), then
 * lay its video's scenes on the new track's bars again.
 */
export const TRACK110 = {
  /** in public/music/, made by scripts/prepare-assets.sh, its first sample on bar 0 */
  file: "it-takes-a-hero.wav",
  title: "It Takes A Hero",
  artist: "Zane Little Music",
  license: "CC BY 4.0",
  page: "https://opengameart.org/content/it-takes-a-hero",
  bpm: 140,
  /** the downbeat the video starts on, in seconds into `file` */
  startS: 0,
  /** its level in the mix: mastered 3 dB louder than Leap (-13.1 LUFS
   * against -16.1), so 0.9, the tour's, less 3 dB */
  gain: 0.64,
};

/** A beat in frames at 30 fps: 12.86 at 140 BPM */
export const BEAT110 = (60 * 30) / TRACK110.bpm;
