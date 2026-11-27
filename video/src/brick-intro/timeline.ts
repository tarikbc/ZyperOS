/**
 * ZyperOS on the TrimUI Brick: the introduction for r/trimui, 4:5 for a
 * phone's feed, where it plays muted, so every feature is named in large
 * words over the screen. The Brick alone, its screens the launcher's own on
 * the Brick's profile (video/footage/takes.py's Brick introduction section,
 * the bi- takes), every scene whole bars of the 1.1 videos' track, "It Takes
 * A Hero" (src/v110/music.ts): 70 bars at 140 BPM, exactly 2:00.
 *
 * It opens on the Brick standing whole under "ZyperOS for the TrimUI Brick"
 * (frame 0 is the poster), then a feature every two bars, the big ones
 * three; MP4, the boost and rumble sit on the track's quiet break (bars 49 to
 * 54), and the end card comes back to the Brick standing whole.
 *
 * A scene whose feature is not on the Brick yet has `wait`: it plays a
 * stand-in take under a WAITING band, and scripts/check-waits.mjs keeps the
 * finish script from making a file with one in it. When the feature lands,
 * record its take, point the scene at it, and delete the `wait`.
 */
import { layout, Scene, Step } from "../timeline";
import { BEAT110 } from "../v110/music";

export type SceneBI = Scene & {
  /** what the scene still waits for on the Brick; drawn over it, and the finish refuses it */
  wait?: string;
};

/** The friend's phone on Player 2: the play page as it opens, then a finger on its d-pad and A
 * (video/footage/phone-presses.py's pictures, the 1.0.5 Reddit cut's) */
const PRESSES = [
  { src: "img/play-phone.png", beats: 1 }, { src: "img/play-phone-right.png", beats: 1 },
  { src: "img/play-phone-right-a.png", beats: 0.5 }, { src: "img/play-phone-right.png", beats: 0.5 },
  { src: "img/play-phone.png", beats: 0.5 }, { src: "img/play-phone-a.png", beats: 0.5 },
  { src: "img/play-phone.png", beats: 1 },
];

const fit = (take: string, from: number, to: number): Step[] => [{ take, from, to, fit: true }];
/** A take at a fixed rate, then the last one fitted to what is left */
const then = (take: string, from: number, to: number, rate: number, last: Step[]): Step[] => [{ take, from, to, rate }, ...last];

export const SCENES_BI: SceneBI[] = [
  { id: "open", bars: 3, pose: "hero", screen: fit("v11-brick-home", 2700, 7300) },
  { id: "boot", bars: 2, pose: "main",
    screen: [{ image: "img/sonic2.png", ms: 500 }, { take: "bi-poweroff", from: 0, to: 1400 }, { gap: 250 },
             { video: "boot/zyperos-fly.mp4", from: 0, to: 1150 }, { image: "img/sonic2.png" }],
    label: "On in seconds", sub: "And back where you stopped" },
  { id: "home", bars: 2, pose: "main", enter: "cut", screen: fit("v11-brick-home", 5200, 12000),
    label: "Every system in pixel art", sub: "Each console its own, animated" },
  { id: "systems", bars: 2, pose: "main", enter: "cut", screen: fit("bi-homegrid", 4300, 10300),
    label: "45 systems", sub: "From the Atari 2600 to the Saturn" },
  { id: "art", bars: 2, pose: "main", enter: "whip", screen: fit("bi-snap", 0, 4650),
    label: "Real box art and video", sub: "Found by the Brick itself" },
  { id: "views", bars: 2, pose: "main", enter: "cut",
    screen: then("v11-brick-grid", 3800, 6600, 1.6, fit("bi-vview", 1200, 4400)),
    label: "Covers, Grid, Video view", sub: "Your library, the way you like it" },
  { id: "downloads", bars: 2, pose: "main", enter: "cut", screen: fit("bi-dl", 0, 5200),
    label: "Every download in one place", sub: "Covers, videos, achievements, cheats" },
  { id: "gpu", bars: 3, pose: "main", enter: "whip", screen: fit("v11-brick-gpu", 0, 5840),
    label: "N64, Dreamcast, PSP, Saturn", sub: "On the Brick's own GPU" },
  { id: "ingame", bars: 2, pose: "main", enter: "whip",
    screen: then("bi-quicksave", 0, 2800, 1.4, fit("bi-undo", 400, 2860)),
    label: "Six save slots", sub: "Quick save, and Undo Load" },
  { id: "looks", bars: 2, pose: "main", enter: "cut", screen: then("bi-lshader", 0, 2400, 1.6, fit("bi-border", 1200, 4700)),
    label: "Shaders and borders", sub: "An old screen round any game" },
  { id: "colors", bars: 2, pose: "main", enter: "cut", screen: fit("v11-brick", 0, 3800),
    label: "Screen Colors, Night Mode", sub: "Set the screen the way you like" },
  { id: "buttons", bars: 2, pose: "main", enter: "cut", screen: fit("bi-gm-remap", 0, 5817),
    label: "Change any button", sub: "For one game or a whole system" },
  { id: "hotkeys", bars: 2, pose: "main", enter: "cut",
    screen: then("bi-hotkeys", 0, 2600, 1.5, fit("bi-shots", 4100, 7100)),
    label: "Hotkeys and screenshots", sub: "Your shots kept on the home" },
  { id: "cheats", bars: 2, pose: "main", enter: "cut", screen: fit("bi-cheats", 0, 3900),
    label: "Cheats", sub: "From libretro's list, or your own" },
  { id: "achievements", bars: 2, pose: "main", enter: "cut", screen: fit("bi-gm-cheevos", 0, 3317),
    label: "RetroAchievements", sub: "Offline too, sent when Wi-Fi is back" },
  { id: "collections", bars: 2, pose: "main", enter: "cut", screen: fit("bi-coll", 3800, 9800),
    label: "Collections of your own", sub: "Lists that mix systems" },
  { id: "search", bars: 2, pose: "main", enter: "cut", screen: fit("bi-search", 6000, 12600),
    label: "Search Games", sub: "Every system at once" },
  { id: "ports", bars: 2, pose: "main", enter: "whip", screen: then("bi-ports", 0, 3000, 1.8, fit("bi-portline", 0, 3200)),
    label: "PortMaster's ports", sub: "Installed with one press" },
  { id: "pico8", bars: 2, pose: "main", enter: "cut", screen: fit("bi-p8", 2500, 9000),
    label: "Your own PICO-8", sub: "With Splore, to find carts" },
  // The Brick's page, and a phone's browser on File Transfer: its PIN, then the page
  { id: "transfer", bars: 3, pose: "split", enter: "whip", screen: fit("bi-wifi-xfer", 0, 5200),
    label: "Add games from a browser", sub: "Over Wi-Fi, behind a PIN",
    browser: { phone: "img/portal-pin.png", phoneAt: 1.5, phoneSeq: [{ src: "img/portal-pin.png", beats: 4 }, { src: "img/portal-phone.png", beats: 6 }] } },
  { id: "flash", bars: 2, pose: "main", enter: "cut", screen: fit("v11-brick", 3800, 6920),
    label: "Or as a flash drive", sub: "The card shows up on your computer" },
  // The download's mark filling in the top bar (the update's page names versions, so it stays out)
  { id: "update", bars: 2, pose: "main", enter: "cut", screen: fit("bi-update-bg", 0, 1980),
    label: "Updates over Wi-Fi", sub: "Downloading while you play",
    punch: [{ at: 0, x: 0.72, y: 0.22, zoom: 1.35 }] },
  // Player 2's code on the Brick until it says connected, then the game; the
  // friend's phone comes in as it connects, and plays
  { id: "player2", bars: 2, pose: "split", enter: "whip",
    screen: [{ take: "bi-player2", from: 600, to: 4700, rate: 1.8 }, { image: "img/smb3.png" }],
    label: "Player 2 on a phone", sub: "A friend joins, no app or account",
    browser: { phone: "img/play-phone.png", phonePic: "img/smb3.png", phoneAt: 3.5, phoneSeq: PRESSES } },
  // The track's quiet break, bars 49 to 54
  { id: "mp4", bars: 2, pose: "main", enter: "whip", screen: fit("bi-looks", 0, 5367),
    label: "Music and videos", sub: "A cassette, a vinyl, a spectrum" },
  { id: "boost", bars: 2, pose: "main", enter: "cut",
    screen: then("bi-boost", 1700, 4700, 1.5, fit("bi-eq", 1500, 3700)),
    label: "Louder than the top", sub: "Up to +18 dB, and an equalizer" },
  { id: "rumble", bars: 2, pose: "main", enter: "cut", screen: fit("bi-rumble", 0, 3500),
    label: "Rumble in games", sub: "As strong as you like" },
  // The band back
  { id: "battery", bars: 2, pose: "main", enter: "whip", screen: fit("bi-charge", 0, 2833),
    label: "A charge limit", sub: "Kinder to the battery" },
  { id: "theme", bars: 2, pose: "main", enter: "cut", screen: fit("bi-look", 4800, 10900),
    label: "Light, dark, your colors", sub: "Ten accents, three homes" },
  { id: "recents", bars: 2, pose: "main", enter: "cut", screen: fit("bi-recents", 0, 3100),
    label: "Recents where you stopped", sub: "Its last screen, and when you played" },
  { id: "firstrun", bars: 3, pose: "main", enter: "whip", screen: fit("bi-firststart", 3000, 8833),
    label: "Eight languages", sub: "Zyper shows you every button" },
  { id: "coming", bars: 2, pose: "main", enter: "cut", screen: fit("bi-folders", 3000, 9500),
    label: "Coming from another firmware?", sub: "Copy your roms folder as it is" },
  { id: "end", bars: 4, pose: "hero", screen: fit("v11-brick-grid", 3800, 11000) },
];

export const BRICK_INTRO = layout(SCENES_BI, BEAT110);

/** The opening's large name gives way to the first label on this beat */
export const HOOK_BEATS_BI = 12;
