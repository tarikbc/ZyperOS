/**
 * The launcher's own interface sounds (res/sounds/wood, the default UI Sounds)
 * where a press is seen: a live take's presses from its index entry, and for a
 * take of shots the moments one picture gives way to the next, both at the
 * speed the clip plays its take.
 */
import { CLIPS, FPS, POWER_MS, SCENES, STARTS } from "./timeline";
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
  cable: [[900, "move"], [1800, "accept"]],
  ds: [[500, "move"], [1000, "move"], [1500, "accept"]],
  cheer: [[300, "reward"]],
  controls_ps1: [],
};

/** Scenes' own moments, in seconds into the scene: POWER pressed, and Zyper's
 * reward sound when ZyperOS is on (src/beats.tsx) */
const SCENE_PRESSES: Record<string, [number, string][]> = {
  boot: [[2.3, "reward"]],
  power: [[POWER_MS.off / 1000, "back"], [POWER_MS.on / 1000, "accept"], [POWER_MS.back / 1000, "reward"]],
};

export const SOUND_CUES = (footage: Footage): Cue[] => {
  const out: Cue[] = [];
  SCENES.forEach((sc, i) => {
    for (const [s, sound] of SCENE_PRESSES[sc.id] ?? []) out.push({ frame: STARTS[i] + Math.round(s * FPS), sound, volume: sound === "reward" ? 0.7 : 1 });
  });
  for (const c of CLIPS) {
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
