// check-waits.mjs [TIMELINE.ts]: the Brick introduction's scenes that still
// wait for a feature on the Brick (a scene's `wait`, drawn as a WAITING band
// over its stand-in screen), one a line; exits 1 while any is left, so the
// finish script never makes a file people see with a stand-in in it.
// Run from video/:  node scripts/check-waits.mjs
import { execFileSync } from "node:child_process";
import { tmpdir } from "node:os";
import path from "node:path";
const tl = process.argv[2] ?? "src/brick-intro/timeline.ts";
const out = path.join(tmpdir(), `waits-${process.pid}.mjs`);
execFileSync("npx", ["esbuild", tl, "--bundle", "--format=esm", "--platform=node", `--outfile=${out}`, "--log-level=error"], { stdio: "inherit" });
const { SCENES_BI } = await import(out);
if (!Array.isArray(SCENES_BI)) { console.error(`${tl} has no SCENES_BI`); process.exit(2); }
const waiting = SCENES_BI.filter((s) => s.wait);
for (const s of waiting) console.log(`${s.id}: ${s.wait}`);
console.log(waiting.length ? `${waiting.length} scene(s) still wait` : "no scene waits");
process.exit(waiting.length ? 1 : 0);
