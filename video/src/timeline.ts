/**
 * The tour's one source of timing, in frames at 30 fps.
 *
 * Music: "The Arplands" by LordZintick (CC0), one of ZyperOS's own menu music
 * tracks (res/bgm), 120 BPM, so a beat is exactly 15 frames. The tour starts it
 * at 16.273 s, the beat its drums come in on, so frame 0 is a downbeat with the
 * whole band playing. Every scene starts on a beat and lasts whole beats.
 *
 * A scene is a short beat of one feature: what the screen plays (takes of
 * video/footage/takes.py, cut and sped up), where the device is (a pose, and a
 * punch-in on the part that matters), how it arrives (a whip or a spring), and a
 * label of a few words, the site's own.
 */
export const FPS = 30;
export const BPM = 120;
export const BEAT = 15;
export const MUSIC_START_S = 16.273;

/** A piece of a take, from one ms to another, at `rate` times its speed (to: "fill" runs to the scene's end) */
export type Step =
  | { take: string; from: number; to?: number | "fill"; rate?: number }
  | { image: string; beats?: number };

/** Where the device is. "main": its screen large on the left; "split": smaller,
 * room for a computer and a phone; "hero": whole, for the start and the end;
 * "duo": two of them, light and dark; "away": off screen, for the language grid */
export type Pose = "main" | "split" | "hero" | "duo" | "away";

/** A punch-in: the screen point (0 to 1) brought to the middle of the screen's place, `zoom` times larger */
export type Punch = { at: number; x: number; y: number; zoom: number; until?: number };

export type Scene = {
  id: string;
  beats: number;
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

export const SCENES: Scene[] = [
  { id: "open", beats: 10, pose: "main", enter: "whip", screen: [{ take: "fast-shelf", from: 3400, to: "fill", rate: 1.2 }],
    label: "Your whole retro library.", sub: "Fast, light firmware for the GKD Pixel 2", labelAt: 1 },
  { id: "home", beats: 8, pose: "main", screen: [{ take: "home", from: 2100, to: "fill", rate: 1.28 }],
    label: "Every system in pixel art" },
  { id: "art", beats: 8, pose: "main", screen: [{ take: "n64", from: 0, to: 1000 }, { take: "scraper", from: 0, to: "fill", rate: 1.6 }],
    label: "Box art it finds itself", sub: "libretro, ScreenScraper, SteamGridDB" },
  { id: "search", beats: 10, pose: "main", screen: [{ take: "search", from: 3200, to: 6400, rate: 1.6 }, { take: "search", from: 9100, to: "fill", rate: 1.6 }],
    label: "Search any game", sub: "A few letters, every system" },
  { id: "launch", beats: 4, pose: "main", enter: "cut", screen: [{ image: "img/sonic2.png" }],
    label: "Starts the moment you press A" },
  { id: "quicksave", beats: 5, pose: "main", screen: [{ take: "quicksave", from: 0, to: "fill" }],
    label: "Quick save, quick load", sub: "SELECT + R1, SELECT + L1", punch: [{ at: 0, x: 0.5, y: 0.86, zoom: 1.9 }] },
  { id: "controls", beats: 8, pose: "main", screen: [{ take: "ingame", from: 0, to: 2000, rate: 1.6 }, { take: "controls-ps1", from: 0, to: "fill", rate: 0.75 }],
    label: "Every control, in the game" },
  { id: "cover", beats: 8, pose: "main", screen: [{ take: "details", from: 4700, to: 7100, rate: 1.6 }, { take: "media", from: 300, to: "fill", rate: 1.2 }],
    label: "Pick a game's cover" },
  { id: "wifi", beats: 15, pose: "split", enter: "whip", screen: [{ take: "wifi-xfer", from: 0, to: "fill", rate: 0.68 }],
    label: "Wi-Fi File Transfer", sub: "Any browser, behind a PIN", browser: { laptop: "img/portal.png", phone: "img/portal-phone.png", laptopAt: 1, phoneAt: 4 } },
  { id: "cable", beats: 13, pose: "split", enter: "cut", screen: [{ take: "cable", from: 0, to: "fill", rate: 0.6 }],
    label: "Or over the USB cable", sub: "Charge only, or File Transfer", browser: { laptop: "img/portal.png", laptopAt: -1 } },
  { id: "hotspot", beats: 6, pose: "split", enter: "cut", screen: [{ take: "hotspot", from: 0, to: "fill", rate: 0.9 }],
    label: "No Wi-Fi around?", sub: "It makes its own network for your phone", browser: { phone: "img/portal-pin.png", phoneAt: 1 } },
  { id: "ring", beats: 5, pose: "main", enter: "whip", screen: [{ take: "xfer-ring", from: 0, to: "fill", rate: 0.96 }],
    label: "Keeps going while you play", punch: [{ at: 0.5, x: 0.87, y: 0.083, zoom: 3.4 }] },
  { id: "update", beats: 5, pose: "main", screen: [{ take: "update", from: 0, to: "fill", rate: 0.9 }],
    label: "System Update", sub: "Over Wi-Fi or the cable" },
  { id: "battery", beats: 10, pose: "main", screen: [{ take: "charge", from: 0, to: 2800 }, { take: "battery", from: 3200, to: "fill" }],
    label: "Battery care", sub: "A charge limit, and the time left" },
  { id: "storage", beats: 6, pose: "main", screen: [{ take: "storage", from: 0, to: "fill", rate: 1.6 }],
    label: "Storage, as a phone shows it" },
  { id: "mp4", beats: 8, pose: "main", screen: [{ take: "mp4", from: 1500, to: 4500, rate: 1.2 }, { take: "lock", from: 0, to: "fill" }],
    label: "MP4", sub: "Music, books and videos. X locks the screen." },
  { id: "ports", beats: 8, pose: "main", screen: [{ take: "ports", from: 0, to: 4000, rate: 1.6 }, { take: "portmenu", from: 0, to: "fill" }],
    label: "PortMaster, one press" },
  { id: "ds", beats: 8, pose: "main", screen: [{ take: "ds", from: 0, to: "fill", rate: 0.72 }],
    label: "Nintendo DS your way", sub: "Screen layouts, and a Game Language" },
  { id: "cheevos", beats: 6, pose: "main", screen: [{ take: "cheevos", from: 700, to: "fill", rate: 0.96 }],
    label: "RetroAchievements", punch: [{ at: 2, x: 0.5, y: 0.86, zoom: 1.7 }] },
  { id: "empty", beats: 6, pose: "main", screen: [{ take: "empty", from: 0, to: 900 }, { take: "cheer", from: 300, to: "fill" }],
    label: "No games yet? It shows how" },
  { id: "themes", beats: 10, pose: "duo", enter: "whip", screen: [], label: "Light and dark", sub: "Beard and Salt & Pepper" },
  { id: "languages", beats: 10, pose: "away", screen: [], label: "Eight languages" },
  { id: "firststart", beats: 8, pose: "main", enter: "whip", screen: [{ take: "firststart", from: 400, to: "fill", rate: 1.6 }],
    label: "A first start that asks" },
  { id: "end", beats: 6, pose: "hero", screen: [{ take: "fast-shelf", from: 3600, to: "fill", rate: 1 }] },
];

/** Scene starts, in frames */
export const STARTS: number[] = SCENES.reduce<number[]>((acc, s, i) => [...acc, i ? acc[i - 1] + SCENES[i - 1].beats * BEAT : 0], []);
export const TOTAL = STARTS[STARTS.length - 1] + SCENES[SCENES.length - 1].beats * BEAT;

export type Clip =
  | { kind: "take"; take: string; at: number; len: number; fromMs: number; rate: number; scene: string }
  | { kind: "image"; src: string; at: number; len: number; scene: string };

/** The scenes' steps laid end to end: a clip each, the last one running to its scene's end */
export const CLIPS: Clip[] = SCENES.flatMap((sc, i) => {
  const start = STARTS[i];
  const end = start + sc.beats * BEAT;
  let at = start;
  return sc.screen.map((st, k): Clip => {
    const last = k === sc.screen.length - 1;
    if ("image" in st) {
      const len = last ? end - at : (st.beats ?? 1) * BEAT;
      const c: Clip = { kind: "image", src: st.image, at, len, scene: sc.id };
      at += len;
      return c;
    }
    const rate = st.rate ?? 1;
    const len = st.to === "fill" || st.to === undefined || last ? end - at : Math.round(((st.to - st.from) / 1000 / rate) * FPS);
    const c: Clip = { kind: "take", take: st.take, at, len, fromMs: st.from, rate, scene: sc.id };
    at += len;
    return c;
  });
});

export const sceneAt = (frame: number) => {
  let i = 0;
  while (i + 1 < STARTS.length && STARTS[i + 1] <= frame) i++;
  return i;
};
