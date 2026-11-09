// Stills of the tour at chosen frames, and a contact sheet of them, to look at
// before the full render: npm run stills [-- FRAME ...]
// Without frames: three of every scene (a second in, the middle, a second
// before its end). Writes out/stills/<frame>.jpeg and out/contact-sheet.jpg,
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
  // The scene starts, read from the timeline's SCENES table
  const src = readFileSync(path.join(root, "src/timeline.ts"), "utf8");
  const table = src.slice(src.indexOf("export const SCENES"), src.indexOf("} as const"));
  const bars = [...table.matchAll(/bar\(([\d.]+)\)/g)].map((m) => Math.round(Number(m[1]) * 60));
  bars.push(56 * 60);
  for (let i = 0; i + 1 < bars.length; i++) {
    const [a, b] = [bars[i], bars[i + 1]];
    frames.push(a + 30, Math.round((a + b) / 2), b - 30);
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
