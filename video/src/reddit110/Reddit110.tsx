import React from "react";
import { AbsoluteFill, Audio, Easing, Img, interpolate, Sequence, staticFile, useCurrentFrame } from "remotion";
import { ease } from "../anim";
import { Cam, Device, DeviceInfo, FONT, Footage, Label, LIGHT, pop, punchAt, Screen } from "../parts";
import { SOUND_CUES } from "../sounds";
import { FPS, Pose } from "../timeline";
import { BEAT_REDDIT110 as BEAT, TRACK_REDDIT110 as MUSIC } from "./music";
import { HOOK_BEATS_R110 as HOOK_BEATS, PAIR_R110 as PAIR, REDDIT110, SceneR110, SCENES_R110 } from "./timeline";

export type Reddit110Props = { footage: Footage; device: DeviceInfo; brick: DeviceInfo };

/** 4:5, the tallest a feed shows at a phone's full width */
export const W = 1080;
export const H = 1350;

const WHIP = 1300;
const { starts, total, clips, sceneEnd, sceneAt } = REDDIT110;

/** Where the screen is for a pose, in the frame: as wide as the frame allows,
 * under the caption (the 1.0.5 cut's), on either handheld */
const SCREEN = { x: 40, y: 560, w: 1000 };
const camOf = (_pose: Pose, d: DeviceInfo): Cam => {
  const k = SCREEN.w / d.screen.w;
  return { k, x: SCREEN.x - d.screen.x * k, y: SCREEN.y - d.screen.y * k };
};
const deviceOf = (sc: SceneR110, p: Reddit110Props) => (sc.device === "brick" ? p.brick : p.device);

/** The scene's handheld: up from below after the hook, a whip where the
 * handheld changes, a cut within one */
const camera = (frame: number, p: Reddit110Props): Cam => {
  const i = sceneAt(frame);
  const sc = SCENES_R110[i];
  const d = deviceOf(sc, p);
  const start = starts[i];
  const end = sceneEnd(i);
  const target = camOf(sc.pose, d);
  let cam: Cam = target;
  if (SCENES_R110[i - 1]?.pair) {
    const k = pop(frame, start, 140, 16);
    cam = { ...target, y: target.y + (1 - k) * 900 };
  } else if (sc.enter === "whip") {
    const k = pop(frame, start, 150, 15);
    cam = { ...target, x: target.x + (1 - k) * WHIP, blur: Math.min(18, Math.max(0, (1 - k) * 40 - 2)) };
  }
  const next = SCENES_R110[i + 1];
  if (next?.enter === "whip" || next?.pair) {
    const k = ease(frame, end - 8, end, Easing.in(Easing.cubic));
    cam = next?.pair ? { ...cam, y: cam.y + k * 900 } : { ...cam, x: cam.x - k * WHIP, blur: (cam.blur ?? 0) + k * 22 };
  }
  return cam;
};

/* The pair, true to size (both renders are 22 px a millimetre): feet on one
 * line, side by side under the hook's words, and under the end card's */
const PAIR_K = 0.3;
const FEET = { pixel2: 2091, brick: 2660 };
const SPOTS = { pixel2: 280, brick: 764 };
const pairCam = (which: "pixel2" | "brick", d: DeviceInfo, rise: number): Cam => ({
  k: PAIR_K, x: SPOTS[which] - (d.width / 2) * PAIR_K, y: 1310 - FEET[which] * PAIR_K + (1 - rise) * 900,
});

/** ZyperOS's wordmark and the release, large for the hook and the end, small in the corner between */
const Brand: React.FC<{ size: number; x: number; y: number; opacity?: number; center?: boolean }> = ({ size, x, y, opacity = 1, center }) => (
  <div style={{ position: "absolute", left: center ? 0 : x, width: center ? W : undefined, top: y, opacity, display: "flex",
    alignItems: "center", justifyContent: center ? "center" : "flex-start", gap: size * 0.28, fontFamily: FONT }}>
    <Img src={staticFile("img/lockup-light.png")} style={{ height: size, width: (size * 1405) / 400 }} />
    <div style={{ fontSize: size * 0.62, fontWeight: 600, color: LIGHT.ink, letterSpacing: -1, marginTop: size * 0.08 }}>1.1</div>
  </div>
);

export const Reddit110: React.FC<Reddit110Props> = (props) => {
  const { footage } = props;
  const frame = useCurrentFrame();
  const i = sceneAt(frame);
  const sc = SCENES_R110[i];
  const start = starts[i];
  const end = sceneEnd(i);
  const cam = camera(frame, props);
  const zoom = punchAt(frame, start, end, sc.punch, BEAT);
  const fadeOut = 1 - ease(frame, total - 12, total - 1);
  const isEnd = sc.id === "end";

  // The hook's large "ZyperOS 1.1", whole from frame 0 (the poster before
  // play), giving way to the corner's small one as the first label lands
  const hookOut = ease(frame, HOOK_BEATS * BEAT - 8, HOOK_BEATS * BEAT, Easing.in(Easing.cubic));
  const endAt = starts[SCENES_R110.length - 1];
  const cornerIn = pop(frame, HOOK_BEATS * BEAT - 2, 200, 18) * (1 - ease(frame, endAt - 6, endAt));
  // The pair: standing from frame 0, down and out as the band comes in, up again for the end
  const pairOut = sc.pair ? (isEnd ? 0 : ease(frame, end - 8, end, Easing.in(Easing.cubic))) : 1;

  return (
    <AbsoluteFill style={{ background: LIGHT.ground, overflow: "hidden" }}>
      {/* No fade in: frame 0 is the poster Reddit shows before play */}
      <AbsoluteFill style={{ opacity: fadeOut }}>
        {!sc.pair && (
          <Device d={deviceOf(sc, props)} cam={cam}>
            <Screen clips={clips} d={deviceOf(sc, props)} zoom={zoom} />
          </Device>
        )}

        {sc.pair && (["pixel2", "brick"] as const).map((which, n) => {
          const d = which === "brick" ? props.brick : props.device;
          const rise = isEnd ? pop(frame, start + n * 3, 140, 15) : 1;
          const c = pairCam(which, d, rise);
          return (
            <Device key={which} d={d} cam={{ ...c, y: c.y + pairOut * 900 }}>
              <Screen clips={PAIR[which].clips} d={d} />
            </Device>
          );
        })}

        {frame < HOOK_BEATS * BEAT && (
          <div style={{ position: "absolute", inset: 0, opacity: 1 - hookOut, transform: `translateY(${-hookOut * 30}px)` }}>
            <Brand size={150} x={0} y={96} center />
            <div style={{ position: "absolute", left: 0, width: W, top: 296, textAlign: "center", fontFamily: FONT, fontSize: 56,
              fontWeight: 600, color: LIGHT.text }}>Everything new since 1.0.5</div>
            <div style={{ position: "absolute", left: 0, width: W, top: 378, textAlign: "center", fontFamily: FONT, fontSize: 36,
              color: LIGHT.soft, opacity: Math.min(1, pop(frame, 4 * BEAT, 180, 16) * 1.4) }}>
              On the GKD Pixel 2 and the TrimUI Brick
            </div>
          </div>
        )}
        {cornerIn > 0.01 && <Brand size={56} x={60} y={48} opacity={Math.min(1, cornerIn * 1.4)} />}
        {sc.tag && (
          <div style={{ position: "absolute", right: 60, top: 52, padding: "8px 22px", borderRadius: 30, background: LIGHT.accent,
            color: LIGHT.text, fontFamily: FONT, fontSize: 30, fontWeight: 600,
            opacity: Math.min(1, pop(frame, start, 200, 18) * 1.4) * (1 - ease(frame, end - 6, end)) }}>
            New in {sc.tag}
          </div>
        )}

        {sc.label && (
          <Label frame={frame} at={start + (sc.labelAt ?? 0) * BEAT} end={end} text={sc.label} sub={sc.sub} dark={0}
            x={60} y={118} width={960} size={78} subSize={38} anchor="top" beat={BEAT} />
        )}

        {isEnd && <EndCardR110 frame={frame} start={start} />}
      </AbsoluteFill>

      <Audio src={staticFile(`music/${MUSIC.file}`)} startFrom={Math.round(MUSIC.startS * FPS)}
        volume={(f) => interpolate(f, [0, total - 2 * BEAT, total - 1], [MUSIC.gain, MUSIC.gain, 0], { extrapolateRight: "clamp" })} />
      {SOUND_CUES(footage, REDDIT110).map((c, n) => (
        <Sequence key={n} from={c.frame} durationInFrames={12} layout="none">
          <Audio src={staticFile(`audio/ui/${c.sound}.wav`)} volume={0.5 * (c.volume ?? 1)} />
        </Sequence>
      ))}
    </AbsoluteFill>
  );
};

/** The last seconds, over both handhelds: the name, where to get it, the music's credit */
const EndCardR110: React.FC<{ frame: number; start: number }> = ({ frame, start }) => {
  const k = (dl: number) => pop(frame, start + dl, 180, 16);
  const rise = (dl: number) => ({ opacity: Math.min(1, k(dl) * 1.4), transform: `translateY(${(1 - k(dl)) * 40}px)` });
  return (
    <div style={{ position: "absolute", left: 0, width: W, top: 70, display: "flex", flexDirection: "column", alignItems: "center",
      fontFamily: FONT, textAlign: "center" }}>
      <div style={{ ...rise(0), position: "relative", width: W, height: 150 }}><Brand size={130} x={0} y={0} center /></div>
      <div style={{ ...rise(4), fontSize: 40, fontWeight: 600, color: LIGHT.text, marginTop: 24, padding: "0 60px" }}>
        Free, for the GKD Pixel&nbsp;2 and the TrimUI&nbsp;Brick.
      </div>
      <div style={{ ...rise(8), fontSize: 76, fontWeight: 600, color: LIGHT.ink, marginTop: 10, letterSpacing: -1 }}>zyperos.com</div>
      <div style={{ ...rise(14), fontSize: 22, lineHeight: 1.5, color: LIGHT.dim, marginTop: 16 }}>
        Music: {MUSIC.artist}, {MUSIC.title} ({MUSIC.license}).<br />
        Games and box art shown belong to their owners.
      </div>
    </div>
  );
};

