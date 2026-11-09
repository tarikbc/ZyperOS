/**
 * The tour's one source of timing, in frames at 30 fps.
 *
 * Music: src/music.ts, one of ZyperOS's own menu music tracks, started on a
 * downbeat. A scene lasts whole bars of it, so every cut is on a downbeat, and
 * its moments (a label, a punch-in, a phone arriving) are counted in its beats.
 * Moving to another track means laying the scenes on its sections again.
 *
 * A scene is a short beat of one feature: what the screen plays (takes of
 * video/footage/takes.py, cut and sped up), where the device is (a pose, and a
 * punch-in on the part that matters), how it arrives (a whip or a spring), and a
 * label of a few words, the site's own.
 */
import { MUSIC } from "./music";

export const FPS = 30;
export const BPM = MUSIC.bpm;
/** A beat in frames: 15 at 120 BPM, a fraction at most tempos */
export const BEAT = (60 * FPS) / BPM;
export const MUSIC_START_S = MUSIC.startS;

/** A piece of a take, from one ms to another, at `rate` times its speed (to: "fill" runs to the scene's end) */
export type Step =
  | { take: string; from: number; to?: number | "fill"; rate?: number }
  | { image: string; beats?: number; ms?: number }
  | { video: string; from: number; to: number }
  | { gap: number };

/** Where the device is. "main": its screen large on the left; "split": smaller,
 * room for a computer and a phone; "hero": whole, for the start and the end;
 * "duo": two of them, light and dark; "away": off screen, for the language grid */
export type Pose = "main" | "split" | "hero" | "duo" | "away";

/** A punch-in: the screen point (0 to 1) brought to the middle of the screen's place, `zoom` times larger */
export type Punch = { at: number; x: number; y: number; zoom: number; until?: number };

export type Scene = {
  id: string;
  /** bars of the music, four beats each */
  bars: number;
  pose: Pose;
  enter?: "whip" | "spring" | "cut";
  screen: Step[];
  label?: string;
  sub?: string;
  /** beats into the scene the label waits */
  labelAt?: number;
  punch?: Punch[];
  /** the computer and the phone of File Transfer */
  browser?: { laptop?: string; phone?: string; phoneAt?: number; laptopAt?: number };
  dark?: boolean;
};

/** A beat in ms, for the scenes whose moments are counted in beats */
const B = 60000 / BPM;
/** The power scene's moments: POWER at beat 1, back in the game on the downbeat of
 * its fourth bar, the second POWER 2 s before that (src/beats.tsx shows them) */
export const POWER_MS = { off: B, on: 12 * B - 2000, back: 12 * B };

/* The order follows Leap's sections (LICENSES.md): the full band for bars 0 to
 * 15, the break at 16 to 19 for the power off and on, back on the downbeat
 * of bar 19 and the band at 20, the video snap turning on bar 21, the calm of
 * 24 to 27 for the in-game menu, File Transfer from 28, light and dark whipping in
 * on bar 52 where the theme returns, and the end on bar 68, the phrase end
 * before the next break. */
export const SCENES: Scene[] = [
  { id: "open", bars: 3, pose: "main", enter: "whip", screen: [{ take: "fast-shelf", from: 3400, to: "fill", rate: 1.2 }],
    label: "Your whole retro library.", sub: "Fast, light firmware for the GKD Pixel 2", labelAt: 1 },
  // ZyperOS on in about 2 s against stock firmware's 30 to 40 (Tarik's own timing)
  { id: "boot", bars: 4, pose: "split", enter: "whip",
    screen: [{ gap: 300 }, { video: "boot/zyperos-fly.mp4", from: 0, to: 2000 }, { take: "home", from: 150, to: "fill" }],
    label: "On in 2 seconds", sub: "Stock firmware: 30 to 40" },
  { id: "home", bars: 2, pose: "main", screen: [{ take: "home", from: 2100, to: "fill", rate: 1.28 }],
    label: "Every system in pixel art" },
  { id: "art", bars: 3, pose: "main", screen: [{ take: "n64", from: 0, to: 1000 }, { take: "scraper", from: 0, to: "fill", rate: 1.35 }],
    label: "Box art it finds itself", sub: "libretro, ScreenScraper, SteamGridDB" },
  { id: "search", bars: 3, pose: "main", screen: [{ take: "search", from: 3200, to: 6400, rate: 1.6 }, { take: "search", from: 9600, to: "fill", rate: 1.6 }],
    label: "Search any game", sub: "A few letters, every system" },
  { id: "launch", bars: 1, pose: "main", enter: "cut", screen: [{ image: "img/sonic2.png" }],
    label: "Starts the moment you press A" },
  // POWER in a game, the power-off, dark; POWER, and the same frame 2 s later
  { id: "power", bars: 4, pose: "main",
    screen: [{ image: "img/sonic2.png", ms: POWER_MS.off }, { take: "poweroff", from: 0, to: 1400 },
             { gap: POWER_MS.on - POWER_MS.off - 1400 + 300 },
             { video: "boot/zyperos-fly.mp4", from: 0, to: 1700 }, { image: "img/sonic2.png" }],
    label: "Turn it off.", sub: "Back in the game in 2 seconds." },
  // Rest on a game: the bar fills for a second, and the cover turns into its
  // video snap on the next downbeat (the take holds 600 ms before the bar)
  { id: "snap", bars: 2, pose: "main", enter: "whip", screen: [{ take: "snap", from: Math.max(0, 600 - (4 * B - 1000)), to: "fill" }],
    label: "Video snaps", sub: "Rest on a game, and its cover turns into the game", punch: [{ at: 0, x: 0.5, y: 0.42, zoom: 1.45 }] },
  { id: "quicksave", bars: 2, pose: "main", screen: [{ take: "quicksave", from: 0, to: "fill", rate: 0.9 }],
    label: "Quick save, quick load", sub: "SELECT + R1, SELECT + L1", punch: [{ at: 0, x: 0.5, y: 0.86, zoom: 1.9 }] },
  { id: "controls", bars: 2, pose: "main", screen: [{ take: "ingame", from: 0, to: 2000, rate: 1.6 }, { take: "controls-ps1", from: 0, to: "fill", rate: 0.75 }],
    label: "Every control, in the game" },
  { id: "cover", bars: 2, pose: "main", screen: [{ take: "details", from: 4700, to: 7100, rate: 1.6 }, { take: "media", from: 300, to: "fill", rate: 1.2 }],
    label: "Pick a game's cover" },
  { id: "wifi", bars: 5, pose: "split", enter: "whip", screen: [{ take: "wifi-xfer", from: 0, to: "fill", rate: 0.66 }],
    label: "Wi-Fi File Transfer", sub: "Any browser, behind a PIN", browser: { laptop: "img/portal.png", phone: "img/portal-phone.png", laptopAt: 2, phoneAt: 8 } },
  { id: "cable", bars: 3, pose: "split", enter: "cut", screen: [{ take: "cable", from: 0, to: "fill", rate: 0.6 }],
    label: "Or over the USB cable", sub: "Charge only, or File Transfer", browser: { laptop: "img/portal.png", laptopAt: -1 } },
  { id: "hotspot", bars: 2, pose: "split", enter: "cut", screen: [{ take: "hotspot", from: 0, to: "fill", rate: 0.9 }],
    label: "No Wi-Fi around?", sub: "It makes its own network for your phone", browser: { phone: "img/portal-pin.png", phoneAt: 1 } },
  { id: "ring", bars: 2, pose: "main", enter: "whip", screen: [{ take: "xfer-ring", from: 0, to: "fill", rate: 0.8 }],
    label: "Keeps going while you play", punch: [{ at: 1, x: 0.87, y: 0.083, zoom: 3.4 }] },
  { id: "update", bars: 2, pose: "main", screen: [{ take: "update", from: 0, to: "fill", rate: 0.75 }],
    label: "System Update", sub: "Over Wi-Fi or the cable" },
  { id: "battery", bars: 3, pose: "main", screen: [{ take: "charge", from: 0, to: 2800 }, { take: "battery", from: 3200, to: "fill" }],
    label: "Battery care", sub: "A charge limit, and the time left" },
  { id: "storage", bars: 2, pose: "main", screen: [{ take: "storage", from: 0, to: "fill", rate: 1.6 }],
    label: "Storage, as a phone shows it" },
  { id: "cheevos", bars: 2, pose: "main", screen: [{ take: "cheevos", from: 700, to: "fill", rate: 0.96 }],
    label: "RetroAchievements", punch: [{ at: 2, x: 0.5, y: 0.86, zoom: 1.7 }] },
  { id: "mp4", bars: 3, pose: "main", screen: [{ take: "mp4", from: 1500, to: 4500, rate: 1.2 }, { take: "lock", from: 0, to: "fill", rate: 0.85 }],
    label: "MP4", sub: "Music, books and videos. X locks the screen." },
  { id: "themes", bars: 2, pose: "duo", enter: "whip", screen: [], label: "Light and dark", sub: "Beard and Salt & Pepper" },
  { id: "ports", bars: 3, pose: "main", enter: "whip", screen: [{ take: "ports", from: 0, to: 4000, rate: 1.6 }, { take: "portmenu", from: 0, to: "fill" }],
    label: "PortMaster, one press" },
  { id: "ds", bars: 2, pose: "main", screen: [{ take: "ds", from: 0, to: "fill", rate: 0.72 }],
    label: "Nintendo DS your way", sub: "Screen layouts, and a Game Language" },
  { id: "languages", bars: 2, pose: "away", screen: [], label: "Eight languages" },
  { id: "empty", bars: 2, pose: "main", enter: "whip", screen: [{ take: "empty", from: 0, to: 900 }, { take: "cheer", from: 300, to: "fill" }],
    label: "No games yet? It shows how" },
  { id: "firststart", bars: 3, pose: "main", screen: [{ take: "firststart", from: 400, to: "fill", rate: 1.6 }],
    label: "A first start that asks" },
  // Two bars and two beats: the downbeat of bar 68 rings while the music fades
  { id: "end", bars: 2.5, pose: "hero", screen: [{ take: "fast-shelf", from: 3600, to: "fill", rate: 1 }] },
];

/** A scene's length in beats of the music */
export const beatsOf = (sc: Scene) => Math.round(sc.bars * 4);
const CUM = SCENES.reduce<number[]>((acc, sc) => [...acc, acc[acc.length - 1] + beatsOf(sc)], [0]);
/** Scene starts, in frames, each on a beat */
export const STARTS: number[] = CUM.slice(0, -1).map((b) => Math.round(b * BEAT));
export const TOTAL = Math.round(CUM[CUM.length - 1] * BEAT);
export const sceneEnd = (i: number) => (i + 1 < STARTS.length ? STARTS[i + 1] : TOTAL);

export type Clip =
  | { kind: "take"; take: string; at: number; len: number; fromMs: number; rate: number; scene: string }
  | { kind: "video"; src: string; at: number; len: number; fromMs: number; rate: number; scene: string }
  | { kind: "image"; src: string; at: number; len: number; scene: string };

/** The scenes' steps laid end to end: a clip each, the last one running to its scene's end */
export const CLIPS: Clip[] = SCENES.flatMap((sc, i) => {
  const start = STARTS[i];
  const end = sceneEnd(i);
  let at = start;
  return sc.screen.flatMap((st, k): Clip[] => {
    const last = k === sc.screen.length - 1;
    if ("gap" in st) {
      at += Math.round((st.gap / 1000) * FPS);
      return [];
    }
    if ("video" in st) {
      const len = last ? end - at : Math.round(((st.to - st.from) / 1000) * FPS);
      const c: Clip = { kind: "video", src: st.video, at, len, fromMs: st.from, rate: 1, scene: sc.id };
      at += len;
      return [c];
    }
    if ("image" in st) {
      const len = last ? end - at : st.ms !== undefined ? Math.round((st.ms / 1000) * FPS) : Math.round((st.beats ?? 1) * BEAT);
      const c: Clip = { kind: "image", src: st.image, at, len, scene: sc.id };
      at += len;
      return [c];
    }
    const rate = st.rate ?? 1;
    const len = st.to === "fill" || st.to === undefined || last ? end - at : Math.round(((st.to - st.from) / 1000 / rate) * FPS);
    const c: Clip = { kind: "take", take: st.take, at, len, fromMs: st.from, rate, scene: sc.id };
    at += len;
    return [c];
  });
});

export const sceneAt = (frame: number) => {
  let i = 0;
  while (i + 1 < STARTS.length && STARTS[i + 1] <= frame) i++;
  return i;
};
