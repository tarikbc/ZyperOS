import React from "react";
import { continueRender, delayRender, Img, OffthreadVideo, Sequence, spring, staticFile } from "remotion";
import { ease, mix, mixColor } from "./anim";
import { Clip, FPS, Pose, Punch } from "./timeline";

export type DeviceInfo = { image: string; width: number; height: number; screen: { x: number; y: number; w: number; h: number } };
export type Take = { fps: number; ms: number; presses?: [number, string, number][] };
export type Footage = Record<string, Take>;
export type Cam = { k: number; x: number; y: number; blur?: number; rot?: number };

/** The site's tokens (site/index.html) */
export const LIGHT = { ground: "#e6e4da", text: "#1a1c1a", soft: "#3d403e", dim: "#5a5d5a", ink: "#476b00", accent: "#bef23c" };
export const DARK = { ground: "#111310", text: "#e4e2d8", soft: "#c3c3ba", dim: "#969998", ink: "#bef23c", accent: "#bef23c" };
export const theme = (k: number) => ({
  ground: mixColor(LIGHT.ground, DARK.ground, k), text: mixColor(LIGHT.text, DARK.text, k),
  soft: mixColor(LIGHT.soft, DARK.soft, k), ink: mixColor(LIGHT.ink, DARK.ink, k), dim: mixColor(LIGHT.dim, DARK.dim, k),
});
export const FONT = "Lexend, 'Noto Sans JP', 'Noto Sans SC', sans-serif";

/** Lexend (the launcher's and the site's) and the CJK menu fonts, before the first frame */
const fonts = delayRender("fonts");
Promise.all([
  new FontFace("Lexend", `url(${staticFile("fonts/menu.ttf")})`, { weight: "400" }),
  new FontFace("Lexend", `url(${staticFile("fonts/title.ttf")})`, { weight: "600" }),
  new FontFace("Noto Sans JP", `url(${staticFile("fonts/ja-menu.otf")})`),
  new FontFace("Noto Sans SC", `url(${staticFile("fonts/zh-menu.otf")})`),
].map((f) => f.load())).then((loaded) => {
  loaded.forEach((f) => (document.fonts as unknown as Set<FontFace>).add(f));
  continueRender(fonts);
});

/** A settled spring with a touch of overshoot: the motion of everything that arrives */
export const pop = (frame: number, delay = 0, stiffness = 170, damping = 15) =>
  spring({ frame: frame - delay, fps: FPS, config: { damping, stiffness: stiffness * EASE_K, mass: 0.7 } });

/** How brisk every spring is: below 1 eases them, so nothing feels rushed */
export const EASE_K = 0.72;

/** Where the device is for a pose: scale k, then a shift */
export const poseOf = (pose: Pose, d: DeviceInfo): Cam => {
  if (pose === "hero") {
    const k = 900 / 1796;
    return { k, x: 640 - 999.5 * k, y: 540 - 1200 * k };
  }
  if (pose === "split") {
    const k = 700 / d.screen.w;
    return { k, x: 70 - d.screen.x * k, y: 560 - (d.screen.y + d.screen.h / 2) * k };
  }
  const k = 1000 / d.screen.w;
  const main = { k, x: 110 - d.screen.x * k, y: 540 - (d.screen.y + d.screen.h / 2) * k };
  return pose === "away" ? { ...main, x: main.x - 2200 } : main;
};

export const mixCam = (a: Cam, b: Cam, t: number): Cam => ({ k: mix(a.k, b.k, t), x: mix(a.x, b.x, t), y: mix(a.y, b.y, t) });

/** The screen's digital zoom: the point brought to the middle, `zoom` times larger */
export const punchAt = (frame: number, start: number, end: number, punches: Punch[] | undefined) => {
  let z = 1, px = 0.5, py = 0.5;
  for (const p of punches ?? []) {
    const a = pop(frame, start + p.at * 15, 120, 18) * (1 - ease(frame, end - 10, end));
    z = mix(z, p.zoom, a);
    px = mix(px, p.x, a);
    py = mix(py, p.y, a);
  }
  return { z, px, py };
};

/** What the screen shows: the clips, each in its own sequence, the later on top */
export const Screen: React.FC<{ clips: Clip[]; d: DeviceInfo; zoom?: { z: number; px: number; py: number } }> = ({ clips, d, zoom }) => {
  const z = zoom ?? { z: 1, px: 0.5, py: 0.5 };
  // The point (px, py) of the picture lands in the middle, as far as the edges allow
  const ox = Math.min(0, Math.max(d.screen.w - d.screen.w * z.z, d.screen.w / 2 - z.px * d.screen.w * z.z));
  const oy = Math.min(0, Math.max(d.screen.h - d.screen.h * z.z, d.screen.h / 2 - z.py * d.screen.h * z.z));
  return (
    <div style={{ position: "absolute", left: d.screen.x, top: d.screen.y, width: d.screen.w, height: d.screen.h,
      background: "#000", overflow: "hidden" }}>
      <div style={{ position: "absolute", left: 0, top: 0, width: d.screen.w, height: d.screen.h, transformOrigin: "0 0",
        transform: `translate(${ox}px, ${oy}px) scale(${z.z})` }}>
        {clips.map((c, i) => (
          <Sequence key={i} from={c.at} durationInFrames={c.len} layout="none">
            {c.kind === "image" ? (
              <Img src={staticFile(c.src)} style={{ position: "absolute", inset: 0, width: "100%", height: "100%", imageRendering: "pixelated" }} />
            ) : (
              <OffthreadVideo muted src={staticFile(`footage/${c.take}.mp4`)} playbackRate={c.rate}
                startFrom={Math.round((c.fromMs / 1000) * FPS)}
                style={{ position: "absolute", inset: 0, width: "100%", height: "100%" }} />
            )}
          </Sequence>
        ))}
      </div>
      <div style={{ position: "absolute", inset: 0, background:
        "linear-gradient(135deg, rgba(255,255,255,0.07) 0%, rgba(255,255,255,0.0) 38%)" }} />
    </div>
  );
};

/** The Pixel 2 with a screen under its cut-out, at the camera's place */
export const Device: React.FC<{ d: DeviceInfo; cam: Cam; children: React.ReactNode; shadow?: number }> = ({ d, cam, children, shadow = 0.22 }) => (
  <div style={{ position: "absolute", left: 0, top: 0, width: d.width, height: d.height, transformOrigin: "0 0",
    transform: `translate(${cam.x}px, ${cam.y}px) scale(${cam.k}) rotate(${cam.rot ?? 0}deg)`,
    filter: cam.blur ? `blur(${cam.blur}px)` : undefined }}>
    {children}
    <Img src={staticFile("device/pixel2-front.png")} style={{ position: "absolute", inset: 0, width: d.width, height: d.height,
      filter: `drop-shadow(0 40px 60px rgba(0,0,0,${shadow}))` }} />
  </div>
);

/** A label that lands: each word rises out of a mask, a lime bar wipes in, a line follows */
export const Label: React.FC<{
  frame: number; at: number; end: number; text: string; sub?: string; dark: number;
  x: number; y: number; width: number; size?: number; align?: "left" | "center"; anchor?: "middle" | "top";
}> = ({ frame, at, end, text, sub, dark, x, y, width, size: wanted = 76, align = "left", anchor = "middle" }) => {
  let size = wanted;
  const t = theme(dark);
  if (frame < at || frame >= end) return null;
  const words = text.split(" ");
  // A long word shrinks the label until it fits its column
  const longest = Math.max(...words.map((w) => w.length));
  size = Math.min(size, Math.floor((width / (longest * 0.56)) * 1));
  const out = (i: number) => ease(frame, end - 9 + i * 0.5, end - 2 + i * 0.5);
  return (
    <div style={{ position: "absolute", left: x, top: y, width, transform: anchor === "middle" ? "translateY(-50%)" : undefined,
      fontFamily: FONT, textAlign: align }}>
      <div style={{ height: 10, width: 84 * pop(frame, at, 200, 18), background: LIGHT.accent, borderRadius: 5, marginBottom: 26,
        marginLeft: align === "center" ? "auto" : 0, marginRight: align === "center" ? "auto" : 0, opacity: 1 - out(0) }} />
      <div style={{ fontSize: size, lineHeight: 1.04, fontWeight: 600, color: t.text, letterSpacing: -1 }}>
        {words.map((w, i) => {
          const k = pop(frame, at + i * 3, 210, 17);
          return (
            <span key={i} style={{ display: "inline-block", overflow: "hidden", verticalAlign: "top", paddingBottom: 6, marginRight: "0.24em" }}>
              <span style={{ display: "inline-block", transform: `translateY(${(1 - k) * 105 - out(i) * 110}%)` }}>{w}</span>
            </span>
          );
        })}
      </div>
      {sub && (
        <div style={{ fontSize: 30, lineHeight: 1.35, color: t.soft, marginTop: 18,
          opacity: Math.min(ease(frame, at + 6, at + 14), 1 - out(0)),
          transform: `translateY(${(1 - ease(frame, at + 6, at + 16)) * 16}px)` }}>{sub}</div>
      )}
    </div>
  );
};

/** File Transfer in a computer's browser: a window with the page */
export const Laptop: React.FC<{ src: string; x: number; y: number; width: number; k: number; dark: number }> = ({ src, x, y, width, k }) => {
  const h = (width * 800) / 1280;
  return (
    <div style={{ position: "absolute", left: x, top: y + (1 - k) * 120, width, opacity: Math.min(1, k * 1.5),
      transform: `scale(${0.9 + 0.1 * k})`, transformOrigin: "50% 100%",
      borderRadius: 16, overflow: "hidden", background: "#f5f4ee", boxShadow: "0 30px 70px rgba(17,19,16,0.28), 0 0 0 1px rgba(0,0,0,0.08)" }}>
      <div style={{ height: 40, display: "flex", alignItems: "center", gap: 8, padding: "0 16px", background: "#d9d7cc" }}>
        {["#e0675a", "#e8b84a", "#7cbf5a"].map((c) => <div key={c} style={{ width: 12, height: 12, borderRadius: 6, background: c }} />)}
        <div style={{ marginLeft: 18, flex: 1, height: 24, borderRadius: 12, background: "#f5f4ee", fontFamily: FONT, fontSize: 14,
          color: "#5a5d5a", display: "flex", alignItems: "center", paddingLeft: 14 }}>192.168.1.42</div>
      </div>
      <Img src={staticFile(src)} style={{ display: "block", width, height: h }} />
    </div>
  );
};

/** ...and in a phone's */
export const Phone: React.FC<{ src: string; x: number; y: number; height: number; k: number }> = ({ src, x, y, height, k }) => {
  const w = (height * 780) / 1688;
  return (
    <div style={{ position: "absolute", left: x, top: y + (1 - k) * 260, width: w + 24, height: height + 24, padding: 12,
      borderRadius: 52, background: "#151715", opacity: Math.min(1, k * 2),
      transform: `rotate(${(1 - k) * 8 - 3}deg)`, boxShadow: "0 30px 70px rgba(17,19,16,0.35)" }}>
      <Img src={staticFile(src)} style={{ display: "block", width: w, height, borderRadius: 40 }} />
    </div>
  );
};

/** A screen on its own, a still of the launcher, for the grid and the pair */
export const Tile: React.FC<{ src: string; x: number; y: number; w: number; k: number; label?: string; dark?: boolean }> = ({ src, x, y, w, k }) => (
  <div style={{ position: "absolute", left: x, top: y, width: w, height: (w * 3) / 4, borderRadius: w * 0.04, overflow: "hidden",
    transform: `scale(${0.6 + 0.4 * k})`, opacity: Math.min(1, k * 1.6), boxShadow: "0 20px 50px rgba(17,19,16,0.25)" }}>
    <Img src={staticFile(src)} style={{ display: "block", width: "100%", height: "100%" }} />
  </div>
);
