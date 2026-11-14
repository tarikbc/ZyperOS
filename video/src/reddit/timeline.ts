/**
 * The 1.0.5 cut for Reddit: what 1.0.5, 1.0.4 and 1.0.3 added, in 56 seconds,
 * 4:5 for a phone's feed, where it plays muted. Every feature is named in large words
 * over the screen, so nothing depends on the sound; the music is the tour's
 * (src/music.ts) from its first downbeat, and every scene is whole bars of it.
 *
 * The scenes follow Leap's sections (LICENSES.md): the full band for bars 0 to
 * 15, from "ZyperOS 1.0.5" over the four new systems to the theme; the break
 * at 16 to 19 for the screensaver and Rename; the band at 20 to 23 for the
 * shaders and achievements; the break at 24 to 27 for File Transfer's way in
 * and USB File Transfer's steps; the band at 28 for Flash Drive and System
 * Update; the bridge for the charge; and the end, two beats past bar 36, the
 * phrase end, while the music fades. Each scene's tag is the release it came in. The takes are the tour's
 * (video/footage/takes.py), and the screens are the launcher's own.
 */
import { BPM, layout, Scene, Step } from "../timeline";

/** A beat in ms */
const B = 60000 / BPM;

/** The in-game menu's Shader values, as the README's picture of them shows
 * Balloon Fight (made in the QEMU machine with Diatom's own shaders; scripts/
 * prepare-assets.sh cuts it into one picture each), and the beats each holds */
export const SHADERS = [
  { name: "Off", file: "off", beats: 1 },
  { name: "Sharp-Shimmerless", file: "sharp", beats: 2 },
  { name: "CRT", file: "crt", beats: 2 },
  { name: "CRT Curved", file: "crt-curved", beats: 1 },
  { name: "Scanlines", file: "scanlines", beats: 1 },
  { name: "LCD Grid", file: "lcd", beats: 1 },
];
const shaderSteps: Step[] = SHADERS.map((s) => ({ image: `img/shader-${s.file}.png`, beats: s.beats }));

export const REDDIT_SCENES: Scene[] = [
  // "ZyperOS 1.0.5" for the first four beats, over the new systems' own starts
  { id: "hook", tag: "1.0.5", bars: 3, pose: "main", screen: [{ take: "newsys", from: 0, to: "fill", rate: 1.34 }],
    label: "Four new systems", sub: "PSP, Doom, ColecoVision and TIC-80", labelAt: 4 },
  { id: "art", tag: "1.0.5", bars: 2, pose: "main", screen: [{ take: "home", from: 2100, to: "fill", rate: 1.28 }],
    label: "New pixel art for every system", sub: "Drawn for ZyperOS, and it moves" },
  // The phone comes in as the panel says connected (beat 6.6)
  { id: "player2", tag: "1.0.5", bars: 3, pose: "split", screen: [{ image: "img/smb3.png", beats: 1 }, { take: "player2", from: 600, to: "fill", rate: 1.39 }],
    label: "Player 2 on a friend's phone", sub: "NES in a browser. No app, no account.",
    browser: { phone: "img/play-phone.png", phonePic: "img/smb3.png", phoneAt: 6.5 } },
  { id: "folders", tag: "1.0.5", bars: 2, pose: "main", screen: [{ take: "folders", from: 2400, to: "fill", rate: 1.5 }],
    label: "Your folders, kept", sub: "As deep as you like" },
  { id: "list", tag: "1.0.5", bars: 2, pose: "main", screen: [{ take: "folders", from: 7050, to: "fill", rate: 1.6 }],
    label: "Covers or a list", sub: "START turns any shelf" },
  { id: "tabs", tag: "1.0.5", bars: 2, pose: "main", screen: [{ take: "look", from: 2450, to: "fill", rate: 1.4 }],
    label: "MENU in six tabs", sub: "L1 and R1 go round them" },
  { id: "theme", tag: "1.0.5", bars: 2, pose: "main", screen: [{ take: "look", from: 6760, to: "fill", rate: 1.5 }],
    label: "A theme of your own", sub: "Light or dark, in the accent you pick" },
  { id: "saver", tag: "1.0.5", bars: 2, pose: "main", screen: [{ take: "saver", from: 300, to: "fill" }],
    label: "A screensaver of your games", sub: "Their video snaps, after a minute" },
  // X > Rename, the name typed, and the shelf after
  { id: "rename", tag: "1.0.5", bars: 2, pose: "main", screen: [{ take: "rename", from: 500, to: 4304, rate: 1.6 }, { take: "renamed", from: 0, to: "fill" }],
    label: "Rename a game", sub: "Its saves and art follow it" },
  { id: "shaders", tag: "1.0.5", bars: 2, pose: "main", screen: shaderSteps,
    label: "Sharp pixels, or an old TV", sub: "Shaders in the in-game menu" },
  // The list with two unlocks waiting, a beat closer on its heading, then an unlock
  { id: "cheevos", tag: "1.0.5", bars: 2, pose: "main", screen: [{ take: "cheevos", from: 0, to: 1320, rate: 0.6 }, { take: "cheevos", from: 1400, to: "fill" }],
    label: "Achievements offline", sub: "They wait, then go to your account", punch: [{ at: 1, x: 0.5, y: 0.17, zoom: 1.45, until: 5.5 }] },
  { id: "xfer", tag: "1.0.5", bars: 2, pose: "main", screen: [{ take: "wifi-way", from: 0, to: "fill", rate: 1.2 }],
    label: "File Transfer, easier to join", sub: "The address and PIN at full size" },
  // 1.0.3: USB File Transfer at zyperos.local, each step ticked as it connects
  { id: "usbsteps", tag: "1.0.3", bars: 2, pose: "main", screen: [{ take: "usbsteps", from: 0, to: "fill", rate: 1.28 }],
    label: "USB File Transfer at zyperos.local", sub: "Each step ticks off as it connects" },
  // 1.0.4: Flash Drive, System Update in the background, a full charge once
  { id: "flashdrive", tag: "1.0.4", bars: 2, pose: "main", screen: [{ take: "flashdrive", from: 0, to: "fill", rate: 1.28 }],
    label: "A plain flash drive", sub: "Any computer, no driver" },
  { id: "update", tag: "1.0.4", bars: 2, pose: "main", screen: [{ take: "update-bg", from: 0, to: 900 }, { take: "update-bg", from: 900, to: "fill", rate: 0.89 }],
    label: "System Update in the background", sub: "Keep playing while it downloads", punch: [{ at: 3, x: 0.87, y: 0.083, zoom: 3.2 }] },
  { id: "charge", tag: "1.0.4", bars: 2, pose: "main", screen: [{ take: "charge-full", from: 0, to: "fill", rate: 1.03 }],
    label: "Charge to 100% this once", sub: "Y on the charging screen" },
  { id: "end", bars: 2.5, pose: "hero", screen: [{ take: "home", from: 4000, to: "fill" }] },
];

export const REDDIT = layout(REDDIT_SCENES);
/** The hook's "ZyperOS 1.0.5" gives way to the first label on this beat */
export const HOOK_BEATS = 4;
export const BEAT_MS = B;
