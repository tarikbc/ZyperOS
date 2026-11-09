import React from "react";
import { continueRender, delayRender, Img, OffthreadVideo, Sequence, staticFile } from "remotion";
import { ease, inOut, mix, mixColor, windowed } from "./anim";
import { Clip, FPS, Pose } from "./timeline";

export type DeviceInfo = { image: string; width: number; height: number; screen: { x: number; y: number; w: number; h: number } };
export type Take = { fps: number; ms: number; presses?: [number, string, number][] };
export type Footage = Record<string, Take>;

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

/** The device's place on the canvas for a pose: scale k, then a shift */
export const poseOf = (pose: Pose, d: DeviceInfo) => {
  if (pose === "hero") {
    // Whole, to the left, 900 px tall (the product's bounds in the render)
    const k = 900 / 1796;
    return { k, x: 700 - 999.5 * k, y: 540 - 1200 * k };
  }
  // The screen 1088 px wide at the left, centred in height
  const k = 1088 / d.screen.w;
  return { k, x: 96 - d.screen.x * k, y: (1080 - d.screen.h * k) / 2 - d.screen.y * k };
};

export const cameraAt = (frame: number, keys: { at: number; pose: Pose; dur?: number }[], d: DeviceInfo) => {
  let cur = poseOf(keys[0].pose, d);
  for (const key of keys.slice(1)) {
    const next = poseOf(key.pose, d);
    const t = ease(frame, key.at, key.at + (key.dur ?? 30), inOut);
    cur = { k: mix(cur.k, next.k, t), x: mix(cur.x, next.x, t), y: mix(cur.y, next.y, t) };
  }
  return cur;
};

/** What the screen shows: the clips, each in its own sequence, the later on top */
export const Screen: React.FC<{ clips: Clip[]; d: DeviceInfo }> = ({ clips, d }) => (
  <div style={{ position: "absolute", left: d.screen.x, top: d.screen.y, width: d.screen.w, height: d.screen.h,
    background: "#000", overflow: "hidden" }}>
    {clips.map((c, i) => (
      <Sequence key={i} from={c.at} durationInFrames={c.len} layout="none">
        {c.kind === "image" ? (
          <Img src={staticFile(c.src)} style={{ position: "absolute", inset: 0, width: "100%", height: "100%", imageRendering: "pixelated" }} />
        ) : (
          <OffthreadVideo muted src={staticFile(c.kind === "take" ? `footage/${c.take}.mp4` : c.src)}
            startFrom={Math.round((c.fromMs / 1000) * FPS)}
            style={{ position: "absolute", inset: 0, width: "100%", height: "100%" }} />
        )}
      </Sequence>
    ))}
    {/* The glass: the faintest light from the top left */}
    <div style={{ position: "absolute", inset: 0, background:
      "linear-gradient(135deg, rgba(255,255,255,0.07) 0%, rgba(255,255,255,0.0) 38%)" }} />
  </div>
);

/** The Pixel 2 with the screen under its cut-out, at the camera's place */
export const Device: React.FC<{ d: DeviceInfo; cam: { k: number; x: number; y: number }; clips: Clip[]; shadow: number }> = ({
  d, cam, clips, shadow,
}) => (
  <div style={{ position: "absolute", left: 0, top: 0, width: d.width, height: d.height, transformOrigin: "0 0",
    transform: `translate(${cam.x}px, ${cam.y}px) scale(${cam.k})` }}>
    <Screen clips={clips} d={d} />
    <Img src={staticFile("device/pixel2-front.png")} style={{ position: "absolute", inset: 0, width: d.width, height: d.height,
      filter: `drop-shadow(0 ${40}px ${60}px rgba(0,0,0,${shadow}))` }} />
  </div>
);

/** A scene's words beside the screen: a small path, the heading, a line */
export const CaptionBlock: React.FC<{
  frame: number; at: number; len: number; eyebrow?: string; head: string; sub?: string; dark: number; x?: number; width?: number;
}> = ({ frame, at, len, eyebrow, head, sub, dark, x = 1400, width = 450 }) => {
  const t = theme(dark);
  const vis = windowed(frame, at, at + len, 12, 8);
  if (vis <= 0) return null;
  const rise = (delay: number) => {
    const k = ease(frame, at + delay, at + delay + 14);
    return { opacity: Math.min(k, vis), transform: `translateY(${(1 - k) * 18}px)` };
  };
  return (
    <div style={{ position: "absolute", left: x, width, top: 0, bottom: 0, display: "flex", flexDirection: "column",
      justifyContent: "center", fontFamily: FONT }}>
      {eyebrow && (
        <div style={{ ...rise(0), fontSize: 22, fontWeight: 600, letterSpacing: 0.4, color: t.ink, marginBottom: 14 }}>{eyebrow}</div>
      )}
      <div style={{ ...rise(eyebrow ? 3 : 0), fontSize: 54, lineHeight: 1.08, fontWeight: 600, color: t.text,
        textWrap: "balance" } as React.CSSProperties}>{head}</div>
      {sub && (
        <div style={{ ...rise(7), fontSize: 26, lineHeight: 1.45, color: t.soft, marginTop: 22,
          textWrap: "pretty" } as React.CSSProperties}>{sub}</div>
      )}
    </div>
  );
};
