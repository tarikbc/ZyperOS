/**
 * The 1.1 video's scenes, for the ZyperOS Discord: what 1.1 adds next to
 * 1.0.6, much of it asked for there, on the Pixel 2 and the TrimUI Brick.
 * 1.0.6's way (src/v106): the launcher's own frames (video/footage/takes.py's
 * 1.1 takes), scenes in whole bars of the music, with the tour's `layout`, on
 * the video's own track (src/v110/music.ts).
 *
 * Each scene's last piece of footage is a `fit` step: from one moment of its
 * take to another, played over the scene's length, so the scenes can be laid
 * on another track's bars and still show the same thing.
 *
 * A scene's `device` is the handheld its screen is in, the Pixel 2's unless
 * it says the Brick's; `pair` scenes stand both side by side, true to size,
 * each with its own take (`pair`).
 */
import { layout, Scene, Step } from "../timeline";
import { BEAT110 } from "./music";

export type Scene110 = Scene & {
  device?: "pixel2" | "brick";
  /** The pair's screens: the Pixel 2's and the Brick's */
  pair?: { pixel2: Step[]; brick: Step[] };
  /** A word beside the label: where the feature came from */
  ask?: "asked" | "reported";
};

const fit = (take: string, from: number, to: number): Step[] => [{ take, from, to, fit: true }];

/* On It Takes A Hero's 140 BPM bars (1.71 s each): scenes of two to four
 * bars. The band grows at bar 16 (Screenshots and its shutter), calms at 31
 * for MP4's looks, grows again at 39, and breaks at bar 49 for the pair and
 * the end card; the end runs two beats past bar 55, where the band comes
 * back, while the music fades. */
export const SCENES110: Scene110[] = [
  // Both handhelds, each on its own home, the release's name beside them,
  // all drawn at frame 0 (the poster before play), from just after Zyper's
  // pop-up of the menu's song (2.6 s into a live take)
  { id: "open", bars: 3, pose: "hero", enter: "cut", screen: [],
    pair: { pixel2: fit("v11-home", 2700, 8600), brick: fit("v11-brick-home", 2700, 8600) } },
  // Your PICO-8: the home on its shelf, in, Splore first, along the carts
  { id: "pico8", bars: 3, pose: "main", enter: "whip", screen: fit("v11-p8", 2500, 10400),
    label: "Your own PICO-8", sub: "Bring the copy you bought. Splore is first on the shelf. On the Pixel 2.", ask: "asked" },
  { id: "p8menu", bars: 2, pose: "main", enter: "cut", screen: fit("v11-p8menu", 0, 3150),
    label: "A game's menu over it", sub: "Display modes, shaders, borders and your hotkeys" },
  { id: "downloads", bars: 3, pose: "main", enter: "whip", screen: fit("v11-dl", 0, 5200),
    label: "Every download in one place", sub: "Covers, videos, names, achievements and cheats" },
  { id: "cheats", bars: 2, pose: "main", enter: "cut", screen: fit("v11-cheats", 0, 3900),
    label: "Cheats", sub: "From libretro's list, or a .cht file of your own" },
  { id: "collections", bars: 3, pose: "main", enter: "whip", screen: fit("v11-coll", 2500, 11700),
    label: "Collections of your own", sub: "Lists that mix systems. MinUI's files work too." },
  { id: "screenshots", bars: 3, pose: "main", enter: "whip", screen: fit("v11-shots", 2500, 10000),
    label: "Your screenshots on the home", sub: "Each with its game. A opens it on the whole screen." },
  { id: "hotkeys", bars: 2, pose: "main", enter: "cut", screen: fit("v11-hotkeys", 0, 3950),
    label: "Hotkeys you choose", sub: "Screenshot, Next Shader, Reset and more, on SELECT" },
  { id: "vview", bars: 3, pose: "main", enter: "whip", screen: fit("v11-vview", 0, 5100),
    label: "Video view", sub: "The game in focus plays its video snap", ask: "asked" },
  { id: "recents", bars: 2, pose: "main", enter: "cut", screen: fit("v11-recents", 0, 3100),
    label: "Recents, where you stopped", sub: "Each game's last screen, and when you played it", ask: "asked" },
  { id: "boost", bars: 3, pose: "main", enter: "whip", screen: fit("v11-boost", 600, 5560),
    label: "Louder than the top", sub: "Hold VOL+ at the top: up to six more cells, +18 dB",
    punch: [{ at: 1, x: 0.5, y: 0.06, zoom: 1.9 }] },
  { id: "eq", bars: 2, pose: "main", enter: "cut", screen: fit("v11-eq", 0, 3700),
    label: "An equalizer", sub: "Bass, Voice, Treble, or five bands of your own" },
  { id: "looks", bars: 4, pose: "main", enter: "whip", screen: fit("v11-looks", 0, 5300),
    label: "MP4's new looks", sub: "Cassette, Vinyl, Classic and Spectrum" },
  { id: "border", bars: 3, pose: "main", enter: "whip", screen: fit("v11-border", 0, 4700),
    label: "Borders round handheld games", sub: "Your own picture, or my drawing of the real Game Boy",
    punch: [{ at: 6, x: 0.2, y: 0.45, zoom: 1.6 }] },
  { id: "gmcolors", bars: 2, pose: "main", enter: "whip", screen: fit("v11-gmcolors", 0, 4680),
    label: "Screen Colors in a game", sub: "Fix a game's colors while you see it", ask: "asked" },
  { id: "details", bars: 2, pose: "main", enter: "cut", screen: fit("v11-details", 0, 3000),
    label: "The game's whole file name", sub: "In its details, brackets and all", ask: "asked" },
  // The PSP's and Saturn's numbers, from the release notes, beside the PSP's home
  { id: "psp", bars: 4, pose: "split", enter: "whip", screen: fit("v11-psp", 0, 6300),
    label: "PSP and Saturn, faster", sub: "Measured on my Pixel 2" },
  // The Brick alone: Screen Colors, Night Mode, then a computer's cable and Flash Drive
  { id: "brick", bars: 3, pose: "main", device: "brick", enter: "whip", screen: fit("v11-brick", 0, 6700),
    label: "New on the Brick", sub: "Screen Colors, Night Mode and Flash Drive" },
  // The break: both again, the same Grid on each
  { id: "pair", bars: 3, pose: "hero", enter: "cut", screen: [],
    pair: { pixel2: fit("v11-grid", 3800, 11000), brick: fit("v11-brick-grid", 3800, 11000) },
    label: "Same ZyperOS on both", sub: "The GKD Pixel 2 and the TrimUI Brick" },
  // Three bars and two beats: the downbeat of bar 55 rings while the music fades
  { id: "end", bars: 3.5, pose: "hero", enter: "cut", screen: [],
    pair: { pixel2: fit("v11-home", 2700, 10000), brick: fit("v11-brick-home", 2700, 10000) } },
];

export const R110 = layout(SCENES110, BEAT110);

/** The pair's clips, laid as the main screen's are: one layout a handheld,
 * over the same scenes, with the pair's steps as each scene's screen */
const pairOf = (which: "pixel2" | "brick") =>
  layout(SCENES110.map((sc) => ({ ...sc, screen: sc.pair ? sc.pair[which] : [] })), BEAT110);
export const PAIR110 = { pixel2: pairOf("pixel2"), brick: pairOf("brick") };

/** The release notes' numbers (docs/releases/v1.1.0.md), each game's speed
 * against its full speed, before and after, on my Pixel 2 */
export const SPEED_ROWS = [
  { name: "Tekken 6", system: "PSP", from: 0.63, to: 0.96 },
  { name: "WipEout Pure", system: "PSP", from: 0.47, to: 0.67 },
  { name: "Virtua Fighter 2", system: "Saturn", from: 0.30, to: 0.35 },
];
