import React from "react";
import { AbsoluteFill, Audio, Easing, Img, interpolate, Sequence, staticFile, useCurrentFrame } from "remotion";
import { ease } from "../anim";
import { MUSIC } from "../music";
import { Cam, Device, DeviceInfo, FONT, Footage, Label, LIGHT, mixCam, Phone, pop, punchAt, Screen } from "../parts";
import { SOUND_CUES } from "../sounds";
import { BEAT, FPS, MUSIC_START_S, Pose } from "../timeline";
import { HOOK_BEATS, REDDIT, SHADERS } from "./timeline";

export type RedditProps = { footage: Footage; device: DeviceInfo };

/** 4:5, the tallest a feed shows at a phone's full width */
export const W = 1080;
export const H = 1350;

/** Where the screen is for a pose, in the frame: its left, its top and its width.
 * "main": as wide as the frame allows, under the caption (the device's top edge
 * is 150 of its pixels above the screen, so its body clears the caption);
 * "split": smaller, the phone beside it; "hero": the end card's, under its words */
const SCREEN: Record<string, { x: number; y: number; w: number }> = {
  main: { x: 40, y: 560, w: 1000 },
  split: { x: 36, y: 560, w: 600 },
  hero: { x: 250, y: 760, w: 580 },
};
const camOf = (pose: Pose, d: DeviceInfo): Cam => {
  const s = SCREEN[pose] ?? SCREEN.main;
  const k = s.w / d.screen.w;
  return { k, x: s.x - d.screen.x * k, y: s.y - d.screen.y * k };
};

/** The device springs from one pose to the next as a scene starts */
const camera = (frame: number, d: DeviceInfo): Cam => {
  const i = REDDIT.sceneAt(frame);
  const sc = REDDIT.scenes[i];
  const target = camOf(sc.pose, d);
  if (i === 0) return target;
  return mixCam(camOf(REDDIT.scenes[i - 1].pose, d), target, pop(frame, REDDIT.starts[i], 150, 17));
};

/** ZyperOS's wordmark and the release, large for the hook and the end, small in the corner between */
const Brand: React.FC<{ size: number; x: number; y: number; opacity?: number; center?: boolean }> = ({ size, x, y, opacity = 1, center }) => (
  <div style={{ position: "absolute", left: center ? 0 : x, width: center ? W : undefined, top: y, opacity, display: "flex",
    alignItems: "center", justifyContent: center ? "center" : "flex-start", gap: size * 0.28, fontFamily: FONT }}>
    <Img src={staticFile("img/lockup-light.png")} style={{ height: size, width: (size * 1405) / 400 }} />
    <div style={{ fontSize: size * 0.62, fontWeight: 600, color: LIGHT.ink, letterSpacing: -1, marginTop: size * 0.08 }}>1.0.5</div>
  </div>
);

export const Reddit: React.FC<RedditProps> = ({ footage, device }) => {
  const frame = useCurrentFrame();
  const i = REDDIT.sceneAt(frame);
  const sc = REDDIT.scenes[i];
  const start = REDDIT.starts[i];
  const end = REDDIT.sceneEnd(i);
  const cam = camera(frame, device);
  const zoom = punchAt(frame, start, end, sc.punch);
  const total = REDDIT.total;
  const fadeOut = 1 - ease(frame, total - 12, total - 1);
  const isEnd = sc.id === "end";

  // The hook's large "ZyperOS 1.0.5" from the first frame, giving way to the
  // corner's small one as the first label lands
  const hookOut = ease(frame, HOOK_BEATS * BEAT - 8, HOOK_BEATS * BEAT, Easing.in(Easing.cubic));
  const endAt = REDDIT.starts[REDDIT.scenes.length - 1];
  const cornerIn = pop(frame, HOOK_BEATS * BEAT - 2, 200, 18) * (1 - ease(frame, endAt - 6, endAt));

  // The shader on screen, for its name, until the in-game menu takes over
  let shader = "";
  if (sc.id === "shaders") {
    let at = start;
    for (const s of SHADERS) {
      if (frame >= at) shader = s.name;
      at += Math.round(s.beats * BEAT);
    }
    if (frame >= at) shader = "";
  }

  return (
    <AbsoluteFill style={{ background: LIGHT.ground, overflow: "hidden" }}>
      <AbsoluteFill style={{ opacity: fadeOut }}>
        <Device d={device} cam={cam}>
          <Screen clips={REDDIT.clips} d={device} zoom={zoom} />
        </Device>

        {sc.browser?.phone && (
          <Phone src={sc.browser.phone} pic={sc.browser.phonePic} x={672} y={500} height={760}
            k={pop(frame, start + (sc.browser.phoneAt ?? 0) * BEAT, 160, 14) * (1 - ease(frame, end - 5, end))} />
        )}

        {shader && (
          <div style={{ position: "absolute", left: SCREEN.main.x + 28, top: SCREEN.main.y + 750 - 96, padding: "12px 26px",
            borderRadius: 40, background: "rgba(17,19,16,0.78)", color: "#f4f3ec", fontFamily: FONT, fontSize: 40, fontWeight: 600 }}>
            {shader}
          </div>
        )}

        {frame < HOOK_BEATS * BEAT && (
          <div style={{ position: "absolute", inset: 0, opacity: 1 - hookOut, transform: `translateY(${-hookOut * 30}px)` }}>
            <Brand size={150} x={0} y={110} center />
            <div style={{ position: "absolute", left: 0, width: W, top: 300, textAlign: "center", fontFamily: FONT, fontSize: 52,
              fontWeight: 600, color: LIGHT.text }}>New in 1.0.3, 1.0.4 and 1.0.5</div>
          </div>
        )}
        {cornerIn > 0.01 && <Brand size={56} x={60} y={48} opacity={Math.min(1, cornerIn * 1.4)} />}
        {sc.tag && frame >= start + (sc.labelAt ?? 0) * BEAT && (
          <div style={{ position: "absolute", right: 60, top: 52, padding: "8px 22px", borderRadius: 30, background: LIGHT.accent,
            color: LIGHT.text, fontFamily: FONT, fontSize: 30, fontWeight: 600,
            opacity: Math.min(1, pop(frame, start + (sc.labelAt ?? 0) * BEAT, 200, 18) * 1.4) * (1 - ease(frame, end - 6, end)) }}>
            New in {sc.tag}
          </div>
        )}

        {sc.label && (
          <Label frame={frame} at={start + (sc.labelAt ?? 0) * BEAT} end={end} text={sc.label} sub={sc.sub} dark={0}
            x={60} y={118} width={960} size={78} subSize={38} anchor="top" />
        )}

        {isEnd && <EndCard frame={frame} start={start} />}
      </AbsoluteFill>

      <Audio src={staticFile(`music/${MUSIC.file}`)} startFrom={Math.round(MUSIC_START_S * FPS)}
        volume={(f) => interpolate(f, [0, total - 2 * BEAT, total - 1], [0.9, 0.9, 0], { extrapolateRight: "clamp" })} />
      {SOUND_CUES(footage, REDDIT).map((c, n) => (
        <Sequence key={n} from={c.frame} durationInFrames={12} layout="none">
          <Audio src={staticFile(`audio/ui/${c.sound}.wav`)} volume={0.5 * (c.volume ?? 1)} />
        </Sequence>
      ))}
    </AbsoluteFill>
  );
};

/** The last four seconds: the name, the release, where to get it, the music's credit */
const EndCard: React.FC<{ frame: number; start: number }> = ({ frame, start }) => {
  const k = (dl: number) => pop(frame, start + dl, 180, 16);
  const rise = (dl: number) => ({ opacity: Math.min(1, k(dl) * 1.4), transform: `translateY(${(1 - k(dl)) * 40}px)` });
  return (
    <div style={{ position: "absolute", left: 0, width: W, top: 80, display: "flex", flexDirection: "column", alignItems: "center",
      fontFamily: FONT, textAlign: "center" }}>
      <div style={{ ...rise(0), position: "relative", width: W, height: 150 }}><Brand size={130} x={0} y={0} center /></div>
      <div style={{ ...rise(4), fontSize: 52, fontWeight: 600, color: LIGHT.text, marginTop: 36 }}>Free, for the GKD Pixel&nbsp;2.</div>
      <div style={{ ...rise(8), fontSize: 76, fontWeight: 600, color: LIGHT.ink, marginTop: 14, letterSpacing: -1 }}>zyperos.com</div>
      <div style={{ ...rise(14), fontSize: 22, lineHeight: 1.5, color: LIGHT.dim, marginTop: 26 }}>
        Music: {MUSIC.title} by {MUSIC.artist} ({MUSIC.license}), from ZyperOS's menu music.<br />
        Games and box art shown belong to their owners.
      </div>
    </div>
  );
};
