import React from "react";
import { AbsoluteFill, Audio, Easing, Img, interpolate, Sequence, staticFile, useCurrentFrame } from "remotion";
import { ease } from "../anim";
import { Cam, Device, DeviceInfo, FONT, Footage, Label, LIGHT, mixCam, Phone, pop, punchAt, Screen } from "../parts";
import { SOUND_CUES } from "../sounds";
import { FPS, Pose, Scene } from "../timeline";
import { BEAT110 as BEAT, TRACK110 as MUSIC } from "../v110/music";
import { BRICK_INTRO, HOOK_BEATS_BI as HOOK_BEATS, SCENES_BI } from "./timeline";

export type BrickIntroProps = { footage: Footage; brick: DeviceInfo };

/** 4:5, the tallest a feed shows at a phone's full width */
export const W = 1080;
export const H = 1350;

const WHIP = 1300;
const { starts, total, clips, sceneEnd, sceneAt } = BRICK_INTRO;

/** Where the screen is: "main" as wide as the frame allows, under the caption
 * (the Reddit cuts'); "split" smaller on the left, the other device (the
 * friend's phone, a phone's browser) beside it */
const SCREEN: Record<string, { x: number; y: number; w: number }> = {
  main: { x: 40, y: 560, w: 1000 },
  split: { x: 30, y: 560, w: 560 },
};
const camOf = (pose: Pose, d: DeviceInfo): Cam => {
  const s = SCREEN[pose] ?? SCREEN.main;
  const k = s.w / d.screen.w;
  return { k, x: s.x - d.screen.x * k, y: s.y - d.screen.y * k };
};
/** The other device's picture at a frame: its sequence's, from the moment it comes in, else its one */
const phoneSrc = (frame: number, start: number, b: NonNullable<Scene["browser"]>): string => {
  let src = b.phone ?? "";
  let at = start + (b.phoneAt ?? 0) * BEAT;
  for (const p of b.phoneSeq ?? []) {
    if (frame >= at) src = p.src;
    at += p.beats * BEAT;
  }
  return src;
};
/** The Brick whole, standing under the opening's and the end's words */
const HERO_K = 0.31;
const heroCam = (d: DeviceInfo, drop: number): Cam => ({
  k: HERO_K, x: (W - d.width * HERO_K) / 2, y: 440 + drop * 900,
});

/** A feature's scene: up from below after the opening, a whip or a cut between
 * features (a spring where the pose changes on a cut), down and away before the end */
const camera = (frame: number, d: DeviceInfo): Cam => {
  const i = sceneAt(frame);
  const sc = SCENES_BI[i];
  const start = starts[i];
  const end = sceneEnd(i);
  const target = camOf(sc.pose, d);
  const prev = SCENES_BI[i - 1];
  let cam: Cam = target;
  if (prev?.pose === "hero") {
    const k = pop(frame, start, 140, 16);
    cam = { ...target, y: target.y + (1 - k) * 900 };
  } else if (sc.enter === "whip") {
    const k = pop(frame, start, 150, 15);
    cam = { ...target, x: target.x + (1 - k) * WHIP, blur: Math.min(18, Math.max(0, (1 - k) * 40 - 2)) };
  } else if (prev && prev.pose !== sc.pose) {
    cam = mixCam(camOf(prev.pose, d), target, pop(frame, start, 150, 17));
  }
  const next = SCENES_BI[i + 1];
  if (next?.enter === "whip" || next?.pose === "hero") {
    const k = ease(frame, end - 8, end, Easing.in(Easing.cubic));
    cam = next?.pose === "hero" ? { ...cam, y: cam.y + k * 900 } : { ...cam, x: cam.x - k * WHIP, blur: (cam.blur ?? 0) + k * 22 };
  }
  return cam;
};

/** ZyperOS's wordmark, large for the opening and the end, small in the corner between */
const Brand: React.FC<{ size: number; x: number; y: number; opacity?: number; center?: boolean }> = ({ size, x, y, opacity = 1, center }) => (
  <div style={{ position: "absolute", left: center ? 0 : x, width: center ? W : undefined, top: y, opacity, display: "flex",
    alignItems: "center", justifyContent: center ? "center" : "flex-start" }}>
    <Img src={staticFile("img/lockup-light.png")} style={{ height: size, width: (size * 1405) / 400 }} />
  </div>
);

/** Over a scene whose feature is not on the Brick yet: never in a file people see (scripts/check-waits.mjs) */
const Waiting: React.FC<{ text: string }> = ({ text }) => (
  <div style={{ position: "absolute", left: 0, width: W, top: 860, padding: "18px 40px", background: "rgba(200,30,30,0.92)",
    color: "white", fontFamily: FONT, fontSize: 34, fontWeight: 600, textAlign: "center" }}>
    WAITING: {text}
  </div>
);

export const BrickIntro: React.FC<BrickIntroProps> = (props) => {
  const { footage, brick } = props;
  const frame = useCurrentFrame();
  const i = sceneAt(frame);
  const sc = SCENES_BI[i];
  const start = starts[i];
  const end = sceneEnd(i);
  const zoom = punchAt(frame, start, end, sc.punch, BEAT);
  const fadeOut = 1 - ease(frame, total - 12, total - 1);
  const isEnd = sc.id === "end";
  const hero = sc.pose === "hero";

  // The opening's large name, whole from frame 0 (the poster before play),
  // giving way to the corner's small one as the first label lands
  const hookOut = ease(frame, HOOK_BEATS * BEAT - 8, HOOK_BEATS * BEAT, Easing.in(Easing.cubic));
  const endAt = starts[SCENES_BI.length - 1];
  const cornerIn = pop(frame, HOOK_BEATS * BEAT - 2, 200, 18) * (1 - ease(frame, endAt - 6, endAt));
  // The Brick whole: standing from frame 0, down and away as the first feature
  // comes, up again for the end
  const heroDrop = isEnd ? 1 - pop(frame, start, 140, 15) : ease(frame, end - 8, end, Easing.in(Easing.cubic));

  return (
    <AbsoluteFill style={{ background: LIGHT.ground, overflow: "hidden" }}>
      {/* No fade in: frame 0 is the poster Reddit shows before play */}
      <AbsoluteFill style={{ opacity: fadeOut }}>
        <Device d={brick} cam={hero ? heroCam(brick, heroDrop) : camera(frame, brick)}>
          <Screen clips={clips} d={brick} zoom={hero ? undefined : zoom} />
        </Device>

        {sc.browser?.phone && (
          <Phone src={phoneSrc(frame, start, sc.browser)} pic={sc.browser.phonePic} x={700} y={520} height={720}
            k={pop(frame, start + (sc.browser.phoneAt ?? 0) * BEAT, 160, 14) * (1 - ease(frame, end - 5, end))} />
        )}

        {frame < HOOK_BEATS * BEAT && (
          <div style={{ position: "absolute", inset: 0, opacity: 1 - hookOut, transform: `translateY(${-hookOut * 30}px)` }}>
            <Brand size={140} x={0} y={70} center />
            <div style={{ position: "absolute", left: 0, width: W, top: 250, textAlign: "center", fontFamily: FONT, fontSize: 60,
              fontWeight: 600, color: LIGHT.text }}>for the TrimUI Brick</div>
            <div style={{ position: "absolute", left: 0, width: W, top: 340, textAlign: "center", fontFamily: FONT, fontSize: 38,
              color: LIGHT.soft, opacity: Math.min(1, pop(frame, 4 * BEAT, 180, 16) * 1.4) }}>
              Free, from its own microSD card
            </div>
          </div>
        )}
        {cornerIn > 0.01 && <Brand size={56} x={60} y={48} opacity={Math.min(1, cornerIn * 1.4)} />}

        {sc.label && (
          <Label frame={frame} at={start + (sc.labelAt ?? 0) * BEAT} end={end} text={sc.label} sub={sc.sub} dark={0}
            x={60} y={118} width={960} size={78} subSize={38} anchor="top" beat={BEAT} />
        )}
        {sc.wait && <Waiting text={sc.wait} />}

        {isEnd && <EndCard frame={frame} start={start} />}
      </AbsoluteFill>

      <Audio src={staticFile(`music/${MUSIC.file}`)} startFrom={Math.round(MUSIC.startS * FPS)}
        volume={(f) => interpolate(f, [0, total - 2 * BEAT, total - 1], [MUSIC.gain, MUSIC.gain, 0], { extrapolateRight: "clamp" })} />
      {SOUND_CUES(footage, BRICK_INTRO).map((c, n) => (
        <Sequence key={n} from={c.frame} durationInFrames={12} layout="none">
          <Audio src={staticFile(`audio/ui/${c.sound}.wav`)} volume={0.5 * (c.volume ?? 1)} />
        </Sequence>
      ))}
    </AbsoluteFill>
  );
};

/** The last seconds, over the Brick: the name, where to get it, the music's credit */
const EndCard: React.FC<{ frame: number; start: number }> = ({ frame, start }) => {
  const k = (dl: number) => pop(frame, start + dl, 180, 16);
  const rise = (dl: number) => ({ opacity: Math.min(1, k(dl) * 1.4), transform: `translateY(${(1 - k(dl)) * 40}px)` });
  return (
    <div style={{ position: "absolute", left: 0, width: W, top: 50, display: "flex", flexDirection: "column", alignItems: "center",
      fontFamily: FONT, textAlign: "center" }}>
      <div style={{ ...rise(0), position: "relative", width: W, height: 130 }}><Brand size={120} x={0} y={0} center /></div>
      <div style={{ ...rise(4), fontSize: 40, fontWeight: 600, color: LIGHT.text, marginTop: 14, padding: "0 60px" }}>
        Free and open source, for the TrimUI&nbsp;Brick.
      </div>
      <div style={{ ...rise(8), fontSize: 76, fontWeight: 600, color: LIGHT.ink, marginTop: 6, letterSpacing: -1 }}>zyperos.com</div>
      <div style={{ ...rise(14), fontSize: 22, lineHeight: 1.5, color: LIGHT.dim, marginTop: 10 }}>
        Music: {MUSIC.artist}, {MUSIC.title} ({MUSIC.license}).<br />
        Games and box art shown belong to their owners.
      </div>
    </div>
  );
};
