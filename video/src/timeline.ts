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
  /** a short tag beside the label (the Reddit cut's release a feature came in) */
  tag?: string;
  /** the computer and the phone of File Transfer */
  browser?: { laptop?: string; phone?: string; phoneAt?: number; laptopAt?: number; phonePic?: string };
  dark?: boolean;
};

/** A beat in ms, for the scenes whose moments are counted in beats */
const B = 60000 / BPM;
/** The power scene's moments: POWER at beat 1, back in the game on the downbeat of
 * its fourth bar, the second POWER 2 s before that (src/beats.tsx shows them) */
export const POWER_MS = { off: B, on: 12 * B - 2000, back: 12 * B };

/* The order follows Leap's sections (LICENSES.md): the full band for bars 0 to
 * 15, from the start to the new systems and the folders, the break at 16 to 19
 * for the power off and on, back on the downbeat of bar 19 and the band at 20,
 * the video snap turning on bar 21 and the Scraper's run, the calm of 24 to 27
 * for the in-game menu's tabs (a disc swapped, a shader's strength), the
 * buttons remapped as the band comes back at 28, Player 2 to the bridge, File
 * Transfer from 34, MENU's tabs and the theme whipping in on bar 52 where the
 * theme returns, and the end on bar 68, the phrase end before the next break. */
export const SCENES: Scene[] = [
  { id: "open", bars: 3, pose: "main", enter: "whip", screen: [{ take: "fast-shelf", from: 3400, to: "fill", rate: 1.2 }],
    label: "Your whole retro library.", sub: "Fast, light firmware for the GKD Pixel 2", labelAt: 1 },
  // ZyperOS on in about 2 s against stock firmware's 30 to 40 (Tarik's own timing)
  { id: "boot", bars: 4, pose: "split", enter: "whip",
    screen: [{ gap: 300 }, { video: "boot/zyperos-fly.mp4", from: 0, to: 2000 }, { take: "home", from: 150, to: "fill" }],
    label: "On in 2 seconds", sub: "Stock firmware: 30 to 40" },
  { id: "home", bars: 2, pose: "main", screen: [{ take: "home", from: 2100, to: "fill", rate: 1.28 }],
    label: "Every system in pixel art", sub: "Each console drawn for ZyperOS" },
  // The four new systems, two beats each, each console's own start
  { id: "newsys", bars: 2, pose: "main", enter: "cut", screen: [{ take: "newsys", from: 0, to: "fill", rate: 2 }],
    label: "Four new systems", sub: "PSP, Doom, ColecoVision and TIC-80" },
  // One take: into a folder and back, then START for the list and back
  { id: "folders", bars: 2, pose: "main", enter: "whip", screen: [{ take: "folders", from: 2400, to: "fill", rate: 1.5 }],
    label: "Your folders, kept", sub: "As deep as you like" },
  { id: "list", bars: 2, pose: "main", enter: "cut", screen: [{ take: "folders", from: 7050, to: "fill", rate: 1.6 }],
    label: "Covers or a list", sub: "START turns any shelf" },
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
  // MENU > Scraper: the plan, then a run, the covers landing as it finds them
  { id: "scraper", bars: 2, pose: "main", screen: [{ take: "scraper-page", from: 0, to: 900, rate: 1.5 }, { take: "scraper-page", from: 1900, to: "fill", rate: 2.3 }],
    label: "Box art it finds itself", sub: "MENU > Scraper, for the whole library" },
  // The in-game menu over Metal Gear Solid, in its tabs: the Game tab down to
  // Disc and the second disc in; R1 to Display, Scanlines and its Strength
  { id: "disc", bars: 2, pose: "main", enter: "whip", screen: [{ take: "gm-disc", from: 0, to: "fill", rate: 1.15 }],
    label: "Swap discs in the game", sub: "The in-game menu, in four tabs" },
  { id: "strength", bars: 2, pose: "main", enter: "cut", screen: [{ take: "gm-strength", from: 0, to: "fill", rate: 1.4 }],
    label: "Sharp pixels, or an old TV", sub: "As strong as you like, 10% to 100%" },
  // R1 to Controls, the handheld coming in beside its rows and leaning toward
  // each button, Y given another button, then in on the handheld as L1 tilts it back
  { id: "remap", bars: 3, pose: "main", enter: "cut", screen: [{ take: "gm-remap", from: 0, to: "fill", rate: 1.25 }],
    label: "Change a button, in the game", sub: "For one game or a whole system", punch: [{ at: 7.5, x: 0.22, y: 0.62, zoom: 1.5 }] },
  // NES: MENU down to Player 2, its code; the friend's phone comes in with the
  // game over its touch pad as the panel says connected (beat 5.8)
  { id: "player2", bars: 3, pose: "split", enter: "whip",
    screen: [{ image: "img/smb3.png", beats: 1 }, { take: "player2", from: 600, to: "fill", rate: 1.39 }],
    label: "Player 2 on a friend's phone", sub: "NES in a browser. No app, no account.",
    browser: { phone: "img/play-phone.png", phonePic: "img/smb3.png", phoneAt: 5.8 } },
  // The way in (the address, zyperos.local, the PIN) for six beats, then a
  // browser signed in and the files moving
  { id: "wifi", bars: 4, pose: "split", enter: "whip", screen: [{ take: "wifi-way", from: 0, to: "fill" }],
    label: "Wi-Fi File Transfer", sub: "Any browser, behind a PIN", browser: { laptop: "img/portal.png", phone: "img/portal-phone.png", laptopAt: 6, phoneAt: 10 } },
  { id: "cable", bars: 4, pose: "main", screen: [{ take: "cable", from: 0, to: "fill" }],
    label: "Or over the USB cable", sub: "A computer, a flash drive, or a phone and its internet" },
  // An empty card, the way in, the games arriving: a little story after File Transfer
  { id: "empty", bars: 2, pose: "main", enter: "whip", screen: [{ take: "empty", from: 0, to: "fill" }],
    label: "An empty card shows you how", punch: [{ at: 2, x: 0.5, y: 0.6, zoom: 1.08 }] },
  { id: "cheer", bars: 2, pose: "main", screen: [{ take: "cheer", from: 0, to: "fill" }],
    label: "And they're on the shelf", punch: [{ at: 1, x: 0.48, y: 0.3, zoom: 1.22 }] },
  { id: "ring", bars: 2, pose: "main", enter: "whip", screen: [{ take: "xfer-ring", from: 0, to: "fill", rate: 0.8 }],
    label: "Keeps going while you play", punch: [{ at: 1, x: 0.87, y: 0.083, zoom: 3.4 }] },
  { id: "update", bars: 2, pose: "main", screen: [{ take: "update", from: 0, to: "fill", rate: 0.75 }],
    label: "System Update", sub: "Over Wi-Fi or the cable" },
  { id: "battery", bars: 2, pose: "main", screen: [{ take: "charge", from: 0, to: 1600 }, { take: "battery", from: 3200, to: "fill" }],
    label: "Battery care", sub: "A charge limit, and the time left" },
  // One take: MENU's tabs round and back, Appearance to Dark, then Display >
  // Theme's accent stepped round
  { id: "tabs", bars: 2, pose: "main", enter: "whip", screen: [{ take: "look", from: 2450, to: "fill", rate: 1.4 }],
    label: "A menu in six tabs", sub: "L1 and R1 go round them" },
  { id: "look", bars: 2, pose: "main", enter: "cut", screen: [{ take: "look", from: 6760, to: "fill", rate: 1.5 }],
    label: "A theme of your own", sub: "Light or dark, in the accent you pick" },
  // The in-game menu's Achievements tab: two unlocks waiting to be sent
  { id: "cheevos", bars: 2, pose: "main", screen: [{ take: "gm-cheevos", from: 0, to: "fill", rate: 1.08 }],
    label: "Achievements, right in the game", sub: "Earned offline. PSP and PS1 .chd too." },
  { id: "mp4", bars: 2, pose: "main", screen: [{ take: "mp4", from: 1500, to: 3900, rate: 1.2 }, { take: "lock", from: 0, to: "fill", rate: 0.85 }],
    label: "MP4", sub: "Music, books and videos. X locks the screen." },
  { id: "ports", bars: 2, pose: "main", enter: "whip", screen: [{ take: "ports", from: 0, to: 3200, rate: 1.6 }, { take: "portmenu", from: 0, to: "fill" }],
    label: "PortMaster, one press" },
  { id: "ds", bars: 2, pose: "main", screen: [{ take: "ds", from: 0, to: "fill", rate: 0.72 }],
    label: "Nintendo DS your way", sub: "Screen layouts, and a Game Language" },
  { id: "languages", bars: 2, pose: "away", screen: [], label: "Eight languages" },
  // Two bars and two beats: the downbeat of bar 68 rings while the music fades
  { id: "end", bars: 2.5, pose: "hero", screen: [{ take: "fast-shelf", from: 3600, to: "fill", rate: 1 }] },
];

/** A scene's length in beats of the music */
export const beatsOf = (sc: Scene) => Math.round(sc.bars * 4);

export type Clip =
  | { kind: "take"; take: string; at: number; len: number; fromMs: number; rate: number; scene: string }
  | { kind: "video"; src: string; at: number; len: number; fromMs: number; rate: number; scene: string }
  | { kind: "image"; src: string; at: number; len: number; scene: string };

/** Scenes laid on the music: where each starts (in frames, each on a beat),
 * the whole length, and their steps laid end to end as clips, the last one
 * running to its scene's end. The tour is one; src/reddit has another. */
export type Layout = {
  scenes: Scene[]; starts: number[]; total: number; clips: Clip[];
  sceneEnd: (i: number) => number; sceneAt: (frame: number) => number;
};

export const layout = (scenes: Scene[]): Layout => {
  const cum = scenes.reduce<number[]>((acc, sc) => [...acc, acc[acc.length - 1] + beatsOf(sc)], [0]);
  const starts = cum.slice(0, -1).map((b) => Math.round(b * BEAT));
  const total = Math.round(cum[cum.length - 1] * BEAT);
  const sceneEnd = (i: number) => (i + 1 < starts.length ? starts[i + 1] : total);
  const clips = scenes.flatMap((sc, i) => {
    const end = sceneEnd(i);
    let at = starts[i];
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
  const sceneAt = (frame: number) => {
    let i = 0;
    while (i + 1 < starts.length && starts[i + 1] <= frame) i++;
    return i;
  };
  return { scenes, starts, total, clips, sceneEnd, sceneAt };
};

/** The tour's own */
export const TOUR = layout(SCENES);
export const STARTS = TOUR.starts;
export const TOTAL = TOUR.total;
export const CLIPS = TOUR.clips;
export const sceneEnd = TOUR.sceneEnd;
export const sceneAt = TOUR.sceneAt;
