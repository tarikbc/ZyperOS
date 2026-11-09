/**
 * The launcher's own interface sounds (res/sounds/wood, the default UI Sounds)
 * where a press is seen: a live take's presses from its index entry, and for
 * a take of shots the moments one picture gives way to the next.
 */
import { Clip, CLIPS, FPS } from "./timeline";
import type { Footage } from "./parts";

export type Cue = { frame: number; sound: string; volume?: number };

const SOUND: Record<string, string> = {
  up: "move", down: "move", left: "move", right: "move", l1: "move", r1: "move",
  a: "accept", x: "accept", y: "accept", start: "accept", menu: "accept", select: "accept", b: "back",
};

/** Shot takes: [ms into the take, sound], as video/footage/takes.py lays them out */
const SHOT_PRESSES: Record<string, [number, string][]> = {
  ingame: [[0, "accept"], [330, "move"], [660, "move"], [990, "move"], [1320, "move"], [1650, "move"], [1980, "accept"], [3680, "move"]],
  "controls-ps1": [],
  media: [[0, "accept"], [420, "move"], [840, "move"], [1260, "move"], [1680, "move"], [2100, "accept"]],
  storage: [[2200, "accept"], [4200, "back"]],
  cheevos: [[330, "move"], [660, "move"], [990, "move"], [1320, "reward"]],
  mp4: [[0, "accept"], [2500, "accept"]],
  ports: [[2000, "accept"]],
  portmenu: [[0, "accept"], [2000, "accept"]],
};

const inClip = (c: Clip, ms: number) => c.kind === "take" && ms >= c.fromMs && ms < c.fromMs + (c.len / FPS) * 1000;

export const SOUND_CUES = (footage: Footage): Cue[] => {
  const out: Cue[] = [];
  for (const c of CLIPS) {
    if (c.kind !== "take" || c.sounds === false) continue;
    const presses: [number, string][] =
      footage[c.take]?.presses?.map(([ms, key]) => [ms, SOUND[key] ?? "move"] as [number, string]) ?? SHOT_PRESSES[c.take] ?? [];
    for (const [ms, sound] of presses) {
      // A press at the very first frame of a clip is the cut's, not the screen's
      if (!inClip(c, ms) || (ms === c.fromMs && ms !== 0)) continue;
      out.push({ frame: c.at + Math.round(((ms - c.fromMs) / 1000) * FPS), sound, volume: sound === "reward" ? 0.7 : 1 });
    }
  }
  return out;
};
