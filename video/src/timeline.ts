/**
 * The tour's one source of timing: scenes, what the screen shows, the captions,
 * the camera and the theme, in frames at 30 fps.
 *
 * Music: "The Arplands" by LordZintick (CC0), one of ZyperOS's own menu music
 * tracks (res/bgm). It is 120 BPM, so a beat is exactly 15 frames and a bar 60.
 * Its beat grid starts 0.273 s into the file, so the music starts there and
 * frame 0 is a beat. Its drums come in at bar 8. Every cut and every caption
 * starts on a beat; the device itself never moves to the beat.
 *
 * Footage: video/footage/takes.py records the launcher's own frames into
 * public/footage/<take>.mp4. A clip plays a take from `fromMs` for `len`
 * frames, at its own speed. A take's presses (public/footage/index.json) play
 * the launcher's own interface sounds (res/sounds/wood) where they are seen.
 */
export const FPS = 30;
export const BPM = 120;
export const BEAT = 15;
export const BAR = 60;
export const MUSIC_START_S = 0.273;
export const BARS = 56;
export const TOTAL = BARS * BAR;

const bar = (n: number) => Math.round(n * BAR);
const s = (seconds: number) => Math.round(seconds * FPS);

export type Clip =
  | { kind: "take"; take: string; at: number; len: number; fromMs: number; sounds?: boolean }
  | { kind: "video"; src: string; at: number; len: number; fromMs: number }
  | { kind: "image"; src: string; at: number; len: number };

export type Caption = { at: number; len: number; eyebrow?: string; head: string; sub?: string };

/** Where the device is: "hero" whole and to the left, "close" its screen large */
export type Pose = "hero" | "close";
export type CameraKey = { at: number; pose: Pose; dur?: number };

/** Clips laid end to end from `start`: a take from one ms to another, a video, or an image for `len` frames */
type Step =
  | { take: string; from: number; to: number }
  | { video: string; from: number; to: number }
  | { image: string; len: number };
const run = (start: number, steps: Step[], sounds = true): Clip[] => {
  let at = start;
  return steps.map((st) => {
    let c: Clip;
    if ("image" in st) c = { kind: "image", src: st.image, at, len: st.len };
    else if ("video" in st) c = { kind: "video", src: st.video, at, len: s((st.to - st.from) / 1000), fromMs: st.from };
    else c = { kind: "take", take: st.take, at, len: s((st.to - st.from) / 1000), fromMs: st.from, sounds };
    at += c.len;
    return c;
  });
};
const tk = (take: string, from: number, to: number): Step => ({ take, from, to });

export const SCENES = {
  intro: bar(0),
  home: bar(4),
  shelf: bar(8),
  search: bar(12),
  ingame: bar(16),
  details: bar(20),
  storage: bar(24),
  cheevos: bar(27),
  mp4: bar(30),
  ports: bar(33),
  dark: bar(37),
  language: bar(42),
  firststart: bar(45),
  battery: bar(49),
  outro: bar(52),
} as const;
const S = SCENES;

const SONIC2 = "img/sonic2.png";

export const CLIPS: Clip[] = [
  // The handheld turning on: its boot animation, then the home
  ...run(S.intro + 15, [{ video: "boot/zyperos-fly.mp4", from: 0, to: 3000 }]),
  ...run(S.intro + 105, [tk("home", 150, 9983)]),
  ...run(bar(6.667), [tk("gallery", 3300, 5967)]),
  ...run(S.shelf, [tk("shelf", 3000, 11000)]),
  // MENU > Search Games: "s", a cut, "n", the three Sonics, A on the second
  ...run(S.search, [tk("search", 3200, 6400), tk("search", 9300, 13930)]),
  // ...its frame, and MENU over it
  ...run(bar(15.917), [{ image: SONIC2, len: 20 }, tk("ingame", 0, 5380), tk("controls-ps1", 0, 2134)]),
  // X on Metal Gear Solid, down to Cover and Video, and the page
  ...run(S.details, [tk("details", 4600, 9300), tk("media", 300, 3600)]),
  ...run(S.storage, [tk("storage", 0, 6000)]),
  ...run(S.cheevos, [tk("cheevos", 0, 6000)]),
  ...run(S.mp4, [tk("mp4", 0, 6000)]),
  ...run(S.ports, [tk("ports", 0, 4000), tk("portmenu", 0, 4000)]),
  // MENU near Appearance, A to Dark, the home and a shelf in the dark
  ...run(S.dark, [tk("dark", 6600, 16600)]),
  // MENU > Language, down to 日本語, A
  ...run(S.language, [tk("language", 6300, 12300)]),
  ...run(S.firststart, [tk("firststart", 400, 8400)]),
  ...run(S.battery, [tk("battery", 0, 6000)]),
  ...run(S.outro, [tk("shelf", 5000, 13000)], false),
];

export const CAPTIONS: Caption[] = [
  { at: S.home + BEAT * 2, len: bar(4) - BEAT * 2, head: "Every system in pixel art",
    sub: "Animated consoles on a Spotlight or Gallery home, A to Z or by release date." },
  { at: S.shelf + BEAT * 2, len: bar(4) - BEAT * 2, head: "Box art it finds itself",
    sub: "Your games as a row of covers, a heart before each favorite's name." },
  { at: S.search, len: bar(3) + BEAT, eyebrow: "MENU > Search Games", head: "Search and Random Game",
    sub: "Find any game by a few letters of its name, or let ZyperOS pick one and start it." },
  { at: bar(15.25), len: bar(1.75), head: "Starts the moment you press A",
    sub: "The emulator stays running, so a game is up as soon as you choose it." },
  { at: S.ingame + BEAT * 3, len: bar(2.5) - BEAT * 3, eyebrow: "MENU in a game", head: "Quick save, quick load",
    sub: "Hold SELECT and press R1 to save a state, L1 to load it, into the slot you pick for each system." },
  { at: bar(18.5), len: bar(1.5), head: "Every control, in the game",
    sub: "Controls in the in-game menu shows what each button does in that game, on its system." },
  { at: S.details + BEAT * 2, len: bar(4) - BEAT * 2, eyebrow: "X > Cover and Video", head: "Pick a game's cover",
    sub: "Every box, 3D box, title screen and screenshot the three sources offer, named with where it comes from." },
  { at: S.storage, len: bar(3), eyebrow: "MENU > Storage", head: "Storage, as a phone shows it",
    sub: "What fills the card in one bar, then every system by size and each system's games largest first." },
  { at: S.cheevos, len: bar(3), head: "RetroAchievements",
    sub: "Achievements download once and then work offline, with each one's badge and a pop-up with Zyper when you unlock one." },
  { at: S.mp4, len: bar(3), eyebrow: "SELECT", head: "MP4",
    sub: "Music, audiobooks and videos on one shelf of their own covers, each marked album, book or video." },
  { at: S.ports, len: bar(2), head: "Ports",
    sub: "The Ports shelf installs the official PortMaster with one press, then runs its games." },
  { at: bar(35), len: bar(2), head: "The in-game menu over every port",
    sub: "MENU pauses any port, and PortMaster itself: Continue, Controls and Quit." },
  { at: S.dark, len: bar(5), eyebrow: "MENU > Appearance", head: "Light and dark",
    sub: "Beard in light, Salt & Pepper in dark. Light, Dark or Auto, and the first start asks." },
  { at: S.language, len: bar(3), eyebrow: "MENU > Language", head: "Eight languages",
    sub: "English, 简体中文, Español, Português (Brasil), Français, Deutsch, 日本語 and Italiano." },
  { at: S.firststart, len: bar(4), head: "A first start that asks",
    sub: "Zyper says hello in each language until you pick yours, then teaches each button." },
  { at: S.battery, len: bar(3), head: "Battery care",
    sub: "A charging screen while the handheld is off, and a corner that can show the time left." },
];

export const CAMERA: CameraKey[] = [
  { at: 0, pose: "hero" },
  { at: S.home, pose: "close", dur: 45 },
  { at: S.outro, pose: "hero", dur: 45 },
];

/** The ground: dark from the moment Appearance turns Dark until the next scene */
export const DARK_AT = S.dark + s(0.65) + 1;
export const LIGHT_AT = S.language;

/** The hero's words: at the start, and again at the end */
export const INTRO_TEXT = { at: BEAT * 2, len: bar(4) - BEAT * 2 };
export const OUTRO_TEXT = { at: S.outro + BEAT * 3, len: bar(4) - BEAT * 3 };
