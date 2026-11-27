/**
 * The 1.1 Reddit cut's music: "Space Cadet Training Montage" by Zane Little
 * Music, CC0 on OpenGameArt, credited as CC BY 4.0 asks (src/v110/music.ts
 * says why and how both tracks were measured). Exactly 130 BPM, the first
 * downbeat 0.0263 s into the file, which scripts/prepare-assets.sh's copy
 * starts on; three quiet bars of build, the band from bar 3.
 */
export const TRACK_REDDIT110 = {
  file: "space-cadet-training-montage.wav",
  title: "Space Cadet Training Montage",
  artist: "Zane Little Music",
  license: "CC BY 4.0",
  page: "https://opengameart.org/content/space-cadet-training-montage",
  bpm: 130,
  startS: 0,
  /** -12.9 LUFS: 3.2 dB under the tour's 0.9 */
  gain: 0.62,
};

/** 13.85 frames at 130 BPM */
export const BEAT_REDDIT110 = (60 * 30) / TRACK_REDDIT110.bpm;
