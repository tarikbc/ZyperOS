// Stills of the tour at chosen frames, and a contact sheet of them, to look at
// before the full render: npm run stills [-- FRAME ...]
// Without frames: two of every scene (scenes last whole bars of src/music.ts's track) (just after it lands, and past its
// middle). Writes out/stills/<frame>.jpeg and out/contact-sheet.jpg,
// six to a row in the order given.
import { bundle } from "@remotion/bundler";
import { renderStill, selectComposition } from "@remotion/renderer";
import { execFileSync } from "node:child_process";
import { mkdirSync, readFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.join(here, "..");
const out = path.join(root, "out/stills");
mkdirSync(out, { recursive: true });

let frames = process.argv.slice(2).map(Number).filter((n) => !Number.isNaN(n));
if (!frames.length) {
  // The scene starts, from the beats of the timeline's SCENES, in order
  const src = readFileSync(path.join(root, "src/timeline.ts"), "utf8");
  const table = src.slice(src.indexOf("export const SCENES"), src.indexOf("/** Scene starts"));
  const bpm = Number(readFileSync(path.join(root, "src/music.ts"), "utf8").match(/bpm: ([\d.]+)/)[1]);
  const beat = 1800 / bpm;
  const beats = [...table.matchAll(/bars: ([\d.]+)/g)].map((m) => Math.round(Number(m[1]) * 4));
  let at = 0;
  for (const b of beats) {
    const len = Math.round(b * beat);
    frames.push(at + Math.min(12, len - 1), at + Math.round(len * 0.6));
    at += len;
  }
}
const serveUrl = await bundle({ entryPoint: path.join(root, "src/index.ts") });
const composition = await selectComposition({ serveUrl, id: "ZyperOSTour" });
for (const frame of frames) {
  const file = path.join(out, `${String(frame).padStart(4, "0")}.jpeg`);
  await renderStill({ serveUrl, composition, frame, output: file, imageFormat: "jpeg", jpegQuality: 85 });
  process.stdout.write(`${frame} `);
}
console.log();
if (frames.length < 2) process.exit(0);
const cols = 6;
const list = frames.map((f) => path.join(out, `${String(f).padStart(4, "0")}.jpeg`));
const inputs = list.flatMap((f) => ["-i", f]);
// In the order given, left to right: the file names say the frames
const labelled = list.map((_, i) => `[${i}]scale=480:-1[v${i}]`);
const layout = list.map((_, i) => `${(i % cols) * 480}_${Math.floor(i / cols) * 270}`).join("|");
const filter = `${labelled.join(";")};${list.map((_, i) => `[v${i}]`).join("")}xstack=inputs=${list.length}:layout=${layout}:fill=black`;
execFileSync("ffmpeg", ["-loglevel", "error", "-y", ...inputs, "-filter_complex", filter, "-frames:v", "1",
  path.join(root, "out/contact-sheet.jpg")]);
console.log("out/contact-sheet.jpg");
