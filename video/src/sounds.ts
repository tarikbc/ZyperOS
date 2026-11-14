/**
 * The launcher's own interface sounds (res/sounds/wood, the default UI Sounds)
 * where a press is seen: a live take's presses from its index entry, and for a
 * take of shots the moments one picture gives way to the next, both at the
 * speed the clip plays its take.
 */
import { FPS, Layout, POWER_MS, TOUR } from "./timeline";
import type { Footage } from "./parts";

export type Cue = { frame: number; sound: string; volume?: number };

const SOUND: Record<string, string> = {
  up: "move", down: "move", left: "move", right: "move", l1: "move", r1: "move",
  a: "accept", x: "accept", y: "accept", start: "accept", menu: "accept", select: "accept", b: "back",
};

/** Shot takes: [ms into the take, sound], as video/footage/takes.py lays them out */
const SHOT_PRESSES: Record<string, [number, string][]> = {
  ingame: [[0, "accept"], [330, "move"], [660, "move"], [990, "move"], [1320, "move"], [1650, "move"]],
  media: [[0, "accept"], [420, "move"], [840, "move"], [1260, "move"], [1680, "move"], [2100, "accept"]],
  storage: [[2200, "accept"], [4200, "back"]],
  cheevos: [[990, "move"], [1320, "reward"]],
  mp4: [[2500, "accept"]],
  lock: [[0, "accept"]],
  ports: [[2000, "accept"]],
  portmenu: [[0, "accept"]],
  scraper: [[600, "accept"], [3600, "accept"]],
  quicksave: [[0, "accept"], [1400, "accept"]],
  cable: [[450, "move"], [900, "accept"], [3400, "move"], [3800, "move"], [4300, "accept"]],
  phone: [[350, "move"], [700, "move"], [1400, "accept"]],
  "scraper-page": [[1000, "accept"], [1900, "accept"]],
  // The in-game menu's tabs (footage/takes.py: a motion is a sixtieth of a second a frame)
  "gm-disc": [[0, "accept"], [450, "move"], [650, "move"], [850, "move"], [1050, "move"], [2050, "move"]],
  "gm-strength": [[0, "move"], ...[516.7, 756.7, 996.7, 1236.7, 1736.7, 2186.7, 2386.7, 2586.7, 2786.7, 2986.7, 3186.7]
    .map((ms): [number, string] => [ms, "move"])],
  "gm-remap": [[0, "move"], ...[716.7, 1066.7, 1416.7, 1766.7, 2116.7, 2466.7, 3133.3, 3900].map((ms): [number, string] => [ms, "move"])],
  "gm-cheevos": [[0, "move"], [616.7, "move"], [916.7, "move"], [1216.7, "move"], [1516.7, "move"]],
  player2: [[0, "accept"], [200, "move"], [400, "move"], [600, "move"], [800, "move"], [1000, "accept"]],
  ds: [[500, "move"], [1000, "move"], [1500, "accept"]],
  cheer: [[300, "reward"]],
  controls_ps1: [],
  rename: [[500, "accept"], [1200, "accept"], [4304 - 500, "accept"]],
  flashdrive: [[400, "move"], [800, "move"], [1400, "accept"]],
  "update-bg": [[900, "accept"]],
  "charge-full": [[1400, "accept"]],
};

/** Scenes' own moments, in seconds into the scene: POWER pressed, and Zyper's
 * reward sound when ZyperOS is on (src/beats.tsx) */
const SCENE_PRESSES: Record<string, [number, string][]> = {
  boot: [[2.3, "reward"]],
  power: [[POWER_MS.off / 1000, "back"], [POWER_MS.on / 1000, "accept"], [POWER_MS.back / 1000, "reward"]],
};

export const SOUND_CUES = (footage: Footage, lay: Layout = TOUR): Cue[] => {
  const out: Cue[] = [];
  lay.scenes.forEach((sc, i) => {
    for (const [s, sound] of SCENE_PRESSES[sc.id] ?? []) out.push({ frame: lay.starts[i] + Math.round(s * FPS), sound, volume: sound === "reward" ? 0.7 : 1 });
  });
  for (const c of lay.clips) {
    if (c.kind !== "take" || c.scene === "end") continue;
    const presses: [number, string][] =
      footage[c.take]?.presses?.map(([ms, key]) => [ms, SOUND[key] ?? "move"] as [number, string]) ?? SHOT_PRESSES[c.take] ?? [];
    const spanMs = (c.len / FPS) * 1000 * c.rate;
    for (const [ms, sound] of presses) {
      // A press at the clip's very first frame belongs to the cut, not the screen
      if (ms < c.fromMs || ms >= c.fromMs + spanMs || (ms === c.fromMs && ms !== 0)) continue;
      out.push({ frame: c.at + Math.round(((ms - c.fromMs) / 1000 / c.rate) * FPS), sound, volume: sound === "reward" ? 0.7 : 1 });
    }
  }
  return out;
};
