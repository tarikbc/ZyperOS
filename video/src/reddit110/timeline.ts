/**
 * The 1.1 cut for Reddit: what is new since 1.0.5, that is 1.0.6 and 1.1,
 * on the GKD Pixel 2 and the TrimUI Brick, in under a minute, 4:5 for a
 * phone's feed, where it plays muted. Every feature is named in large words
 * over the screen, with the release it came in, so nothing depends on the
 * sound; the music is its own (src/reddit110/music.ts), every scene whole
 * bars of it.
 *
 * It opens on both handhelds under "ZyperOS 1.1" over the track's three bars
 * of build, then 1.0.6's big news, the TrimUI Brick, as the band comes in on
 * bar 3, and its new GPU consoles, then the rest, two bars each: 1.1's biggest first, 1.0.6's Grid
 * among them. The end comes back to both handhelds. The takes are
 * video/footage/takes.py's 1.1 takes, the screens the launcher's own.
 */
import { layout, Scene, Step } from "../timeline";
import { BEAT_REDDIT110 } from "./music";

export type SceneR110 = Scene & {
  device?: "pixel2" | "brick";
  /** The pair's screens: the Pixel 2's and the Brick's */
  pair?: { pixel2: Step[]; brick: Step[] };
};

const fit = (take: string, from: number, to: number): Step[] => [{ take, from, to, fit: true }];

/* On Space Cadet Training Montage's 130 BPM bars (1.85 s each): the hook
 * over its three bars of build, scenes of two bars from the band's entry on
 * bar 3, the end card from bar 29 to two beats past bar 32, while the music
 * fades. */
export const SCENES_R110: SceneR110[] = [
  { id: "hook", bars: 3, pose: "hero", screen: [],
    pair: { pixel2: fit("v11-home", 2700, 7300), brick: fit("v11-brick-home", 2700, 7300) } },
  { id: "brick", tag: "1.0.6", bars: 2, pose: "main", device: "brick", screen: fit("v11-brick-home", 5000, 12000),
    label: "Now on the TrimUI Brick", sub: "From its own microSD card" },
  { id: "brickgpu", tag: "1.1", bars: 2, pose: "main", device: "brick", enter: "cut", screen: fit("v11-brick-gpu", 0, 5840),
    label: "N64, Dreamcast and PSP", sub: "And the Saturn, on the Brick's own GPU" },
  { id: "pico8", tag: "1.1", bars: 2, pose: "main", enter: "whip", screen: fit("v11-p8", 2500, 10000),
    label: "Your own PICO-8", sub: "Bring the copy you bought, Splore and all. On the Pixel 2." },
  { id: "downloads", tag: "1.1", bars: 2, pose: "main", enter: "cut", screen: fit("v11-dl", 0, 5200),
    label: "Every download in one place", sub: "Covers, videos, achievements, cheats" },
  { id: "grid", tag: "1.0.6", bars: 2, pose: "main", enter: "cut", screen: fit("v11-grid", 3800, 9800),
    label: "Grid view", sub: "Covers in their shelf's own shape" },
  { id: "vview", tag: "1.1", bars: 2, pose: "main", enter: "cut", screen: fit("v11-vview", 600, 5100),
    label: "Video view", sub: "The game in focus plays its video snap" },
  { id: "collections", tag: "1.1", bars: 2, pose: "main", enter: "cut", screen: fit("v11-coll", 2500, 9800),
    label: "Collections of your own", sub: "Lists that mix systems" },
  { id: "screenshots", tag: "1.1", bars: 2, pose: "main", enter: "cut", screen: fit("v11-shots", 2500, 10000),
    label: "Your screenshots on the home", sub: "A hotkey takes one, the home keeps them" },
  { id: "looks", tag: "1.1", bars: 2, pose: "main", enter: "cut", screen: fit("v11-looks", 0, 5300),
    label: "MP4's new looks", sub: "Cassette, Vinyl, Classic and Spectrum" },
  { id: "border", tag: "1.1", bars: 2, pose: "main", enter: "cut", screen: fit("v11-border", 900, 4700),
    label: "Borders round handheld games", sub: "Even the real Game Boy round the game",
    punch: [{ at: 3, x: 0.2, y: 0.45, zoom: 1.6 }] },
  { id: "boost", tag: "1.1", bars: 2, pose: "main", enter: "cut", screen: fit("v11-boost", 1700, 5560),
    label: "Louder than the top", sub: "Hold VOL+ for up to +18 dB",
    punch: [{ at: 0, x: 0.5, y: 0.06, zoom: 1.9 }] },
  { id: "colors", tag: "1.0.6", bars: 2, pose: "main", device: "brick", enter: "whip", screen: fit("v11-brick", 0, 3800),
    label: "Screen Colors and Night Mode", sub: "On the Brick too since 1.1, and in a game" },
  { id: "psp", tag: "1.1", bars: 2, pose: "main", enter: "whip", screen: fit("v11-psp", 0, 4000),
    label: "PSP, much faster", sub: "Tekken 6 from 0.63x to 0.96x its speed, on my Pixel 2" },
  // Three bars and two beats: both handhelds again while the music fades
  { id: "end", bars: 3.5, pose: "hero", screen: [],
    pair: { pixel2: fit("v11-grid", 3800, 11000), brick: fit("v11-brick-grid", 3800, 11000) } },
];

export const REDDIT110 = layout(SCENES_R110, BEAT_REDDIT110);

/** The pair's clips: one layout a handheld over the same scenes */
const pairOf = (which: "pixel2" | "brick") =>
  layout(SCENES_R110.map((sc) => ({ ...sc, screen: sc.pair ? sc.pair[which] : [] })), BEAT_REDDIT110);
export const PAIR_R110 = { pixel2: pairOf("pixel2"), brick: pairOf("brick") };

/** The hook's large "ZyperOS 1.1" gives way to the first label on this beat */
export const HOOK_BEATS_R110 = 12;
