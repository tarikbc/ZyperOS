import React from "react";
import { AbsoluteFill, Audio, Easing, Img, interpolate, Sequence, staticFile, useCurrentFrame } from "remotion";
import { ease, mix } from "../anim";
import { FPS } from "../timeline";
import { BEAT110 as BEAT, TRACK110 as MUSIC } from "./music";
import { Cam, Device, DeviceInfo, FONT, Footage, Label, Laptop, LIGHT, mixCam, pop, poseOf, punchAt, Screen } from "../parts";
import { SOUND_CUES } from "../sounds";
import { PAIR110 as PAIR, R110, Scene110, SCENES110, SPEED_ROWS } from "./timeline";

export type Release110Props = { footage: Footage; device: DeviceInfo; brick: DeviceInfo };

const WHIP = 1700;
const { starts, total, clips, sceneEnd, sceneAt } = R110;

const deviceOf = (sc: Scene110, p: Release110Props) => (sc.device === "brick" ? p.brick : p.device);
const away = (sc: Scene110) => (sc.pose === "hero" ? "away" : sc.pose);

/** Where the scene's handheld is: springs between poses, whips in and out (the tour's camera) */
const camera = (frame: number, p: Release110Props): Cam => {
  const i = sceneAt(frame);
  const sc = SCENES110[i];
  const d = deviceOf(sc, p);
  const start = starts[i];
  const end = sceneEnd(i);
  const target = poseOf(away(sc), d);
  let cam: Cam;
  if (sc.enter === "whip") {
    const k = pop(frame, start, 150, 15);
    cam = { ...target, x: target.x + (1 - k) * WHIP, blur: Math.min(18, Math.max(0, (1 - k) * 40 - 2)) };
  } else if (sc.enter === "cut" || i === 0) {
    cam = target;
  } else {
    cam = mixCam(poseOf(away(SCENES110[i - 1]), d), target, pop(frame, start, 150, 17));
  }
  const next = SCENES110[i + 1];
  if (next?.enter === "whip" && sc.pose !== "hero") {
    const k = ease(frame, end - 8, end, Easing.in(Easing.cubic));
    cam = { ...cam, x: cam.x - k * WHIP, blur: (cam.blur ?? 0) + k * 22 };
  }
  return cam;
};

/* The pair, true to size: both at one scale, standing on one line. The
 * Pixel 2's body is 81 mm tall about its picture's middle, the Brick's 110;
 * at 22 px a millimetre their feet are 2091 and 2660 px down their pictures. */
const PAIR_K = 0.33;
const FEET = { pixel2: 2091, brick: 2660 };
/** Where each stands: beside the words (right), or beside the end card (left) */
const SPOTS = { right: { pixel2: 1070, brick: 1600 }, left: { pixel2: 290, brick: 810 } };
const pairCam = (which: "pixel2" | "brick", x: number, d: DeviceInfo, rise: number): Cam => ({
  k: PAIR_K, x: x - (d.width / 2) * PAIR_K, y: 1010 - FEET[which] * PAIR_K + (1 - rise) * 1000,
});

export const Release110: React.FC<Release110Props> = (props) => {
  const { footage, device, brick } = props;
  const frame = useCurrentFrame();
  const i = sceneAt(frame);
  const sc = SCENES110[i];
  const start = starts[i];
  const end = sceneEnd(i);
  const cam = camera(frame, props);
  const zoom = punchAt(frame, start, end, sc.punch, BEAT);
  const labelAt = start + (sc.labelAt ?? 0) * BEAT;
  const fadeOut = 1 - ease(frame, total - 12, total - 1);
  const whipOut = SCENES110[i + 1]?.enter === "whip" ? ease(frame, end - 8, end, Easing.in(Easing.cubic)) : 0;

  // The pair: standing from frame 0 as the release opens, up from below when it comes back, then aside for the end card
  const pairShown = !!sc.pair;
  const firstPair = pairShown && !SCENES110[i - 1]?.pair;
  const toLeft = sc.id === "end" ? pop(frame, start, 140, 18) : 0;

  return (
    <AbsoluteFill style={{ background: LIGHT.ground, overflow: "hidden" }}>
      {/* No fade in: frame 0 is the poster Discord, Reddit and players show
       * before play, so it is the opening, fully drawn (Opening, the pair) */}
      <AbsoluteFill style={{ opacity: fadeOut }}>
        {sc.pose !== "hero" && (
          <Device d={deviceOf(sc, props)} cam={cam}>
            <Screen clips={clips} d={deviceOf(sc, props)} zoom={zoom} />
          </Device>
        )}

        {pairShown && (["pixel2", "brick"] as const).map((which, n) => {
          const d = which === "brick" ? brick : device;
          const rise = firstPair && i > 0 ? pop(frame, start + n * 3, 140, 15) : 1;
          const x = mix(SPOTS.right[which], SPOTS.left[which], toLeft);
          const c = pairCam(which, x, d, rise);
          return (
            <Device key={which} d={d} cam={{ ...c, x: c.x - whipOut * WHIP, blur: whipOut * 22 }}>
              <Screen clips={PAIR[which].clips} d={d} />
            </Device>
          );
        })}

        {sc.browser?.laptop && (
          <Laptop src={sc.browser.laptop} x={900} y={250} width={940} dark={0}
            k={sc.browser.laptopAt === -1 ? 1 : pop(frame, start + (sc.browser.laptopAt ?? 0) * BEAT, 150, 16) *
              (SCENES110[i + 1]?.browser?.laptop ? 1 : 1 - ease(frame, end - 5, end))} />
        )}

        {sc.label && sc.pose === "main" && (
          <Label frame={frame} at={labelAt} end={end} text={sc.label} sub={sc.sub} dark={0} x={1320} y={540} width={540} size={72} beat={BEAT} />
        )}
        {sc.label && sc.pose === "split" && sc.browser?.laptop && (
          <Label frame={frame} at={labelAt} end={end} text={sc.label} sub={sc.sub} dark={0} x={900} y={40} width={1000} size={62} anchor="top" beat={BEAT} />
        )}
        {sc.label && sc.pose === "split" && !sc.browser && (
          <Label frame={frame} at={labelAt} end={end} text={sc.label} sub={sc.sub} dark={0} x={960} y={60} width={900} size={70} anchor="top" beat={BEAT} />
        )}
        {sc.id === "psp" && <SpeedRows frame={frame} start={start} end={end} />}
        {sc.label && sc.pose === "hero" && (
          <Label frame={frame} at={labelAt} end={end} text={sc.label} sub={sc.sub} dark={0} x={110} y={540} width={620} size={76} beat={BEAT} />
        )}
        {sc.ask && <Ask frame={frame} at={labelAt} end={end} kind={sc.ask} />}

        {sc.id === "open" && <Opening frame={frame} start={start} out={whipOut} />}
        {sc.id === "end" && <EndCard110 frame={frame} start={start} />}
      </AbsoluteFill>

      <Audio src={staticFile(`music/${MUSIC.file}`)} startFrom={Math.round(MUSIC.startS * FPS)}
        volume={(f) => interpolate(f, [0, total - 2 * BEAT, total - 1], [MUSIC.gain, MUSIC.gain, 0], { extrapolateRight: "clamp" })} />
      {SOUND_CUES(footage, R110).map((c, n) => (
        <Sequence key={n} from={c.frame} durationInFrames={12} layout="none">
          <Audio src={staticFile(`audio/ui/${c.sound}.wav`)} volume={0.5 * (c.volume ?? 1)} />
        </Sequence>
      ))}
    </AbsoluteFill>
  );
};

/** Where a feature came from: the Discord, in a pill at the foot */
const Ask: React.FC<{ frame: number; at: number; end: number; kind: "asked" | "reported" }> = ({ frame, at, end, kind }) => {
  const k = pop(frame, at + BEAT, 200, 18);
  if (frame < at + BEAT - 1) return null;
  return (
    <div style={{ position: "absolute", right: 64, bottom: 58, display: "flex", alignItems: "center", gap: 14, padding: "12px 26px 12px 18px",
      borderRadius: 40, background: LIGHT.text, color: "#f4f3ec", fontFamily: FONT, fontSize: 30, fontWeight: 600,
      opacity: Math.min(1, k * 1.4) * (1 - ease(frame, end - 6, end)), transform: `translateY(${(1 - k) * 30}px)` }}>
      <div style={{ width: 16, height: 16, borderRadius: 8, background: LIGHT.accent }} />
      {kind === "asked" ? "Asked for on Discord" : "Reported on Discord"}
    </div>
  );
};

/** The release's name beside both handhelds, drawn whole from frame 0 (the
 * poster before play); only its last line arrives, two beats in */
const Opening: React.FC<{ frame: number; start: number; out: number }> = ({ frame, start, out }) => {
  const k = (d: number) => (d < 0 ? 1 : pop(frame, start + d, 180, 16));
  const rise = (d: number) => ({ opacity: Math.min(1, k(d) * 1.4), transform: `translateY(${(1 - k(d)) * 40}px)` });
  return (
    <div style={{ position: "absolute", left: 110 - out * WHIP, width: 700, top: 0, bottom: 0, display: "flex", flexDirection: "column",
      justifyContent: "center", fontFamily: FONT, filter: out ? `blur(${out * 22}px)` : undefined }}>
      <Img src={staticFile("img/lockup-light.png")} style={{ ...rise(-1), width: 400, marginLeft: -8 }} />
      <div style={{ ...rise(-1), fontSize: 168, lineHeight: 1, fontWeight: 600, color: LIGHT.text, letterSpacing: -6, marginTop: 18 }}>1.1</div>
      <div style={{ ...rise(-1), fontSize: 46, lineHeight: 1.15, fontWeight: 600, color: LIGHT.text, marginTop: 30 }}>
        Your own PICO-8, every download in one place, and more.
      </div>
      <div style={{ ...rise(2 * BEAT), fontSize: 30, lineHeight: 1.4, color: LIGHT.soft, marginTop: 16 }}>
        For the GKD Pixel 2 and the TrimUI Brick.
      </div>
    </div>
  );
};

/** Each game's speed against its full speed, before and after (the release
 * notes' numbers, on my Pixel 2): a bar to the full-speed line, the old
 * speed in grey, the new one growing over it */
const SpeedRows: React.FC<{ frame: number; start: number; end: number }> = ({ frame, start, end }) => {
  const out = 1 - ease(frame, end - 6, end);
  return (
    <>
      {SPEED_ROWS.map((r, n) => {
        const inAt = start + (2 + n * 3) * BEAT;
        const grow = pop(frame, inAt + 1.5 * BEAT, 120, 16);
        const k = pop(frame, inAt, 160, 16) * out;
        const v = mix(r.from, r.to, frame < inAt + 1.5 * BEAT ? 0 : grow);
        const done = grow > 0.98;
        return (
          <div key={r.name} style={{ position: "absolute", left: 960, top: 330 + n * 205, width: 880, fontFamily: FONT,
            opacity: Math.min(1, k * 1.5), transform: `translateX(${(1 - k) * 80}px)` }}>
            <div style={{ display: "flex", alignItems: "baseline", justifyContent: "space-between", gap: 30 }}>
              <div style={{ fontSize: 34, fontWeight: 600, color: LIGHT.text }}>
                {r.name} <span style={{ fontWeight: 400, color: LIGHT.dim, fontSize: 26 }}>{r.system}</span>
              </div>
              <div style={{ fontSize: 60, fontWeight: 600, color: done ? LIGHT.ink : LIGHT.text, fontVariantNumeric: "tabular-nums",
                letterSpacing: -1, whiteSpace: "nowrap" }}>
                {v.toFixed(2)}x
              </div>
            </div>
            <div style={{ marginTop: 10, height: 18, borderRadius: 9, background: "#cfcdc2", overflow: "hidden", position: "relative" }}>
              <div style={{ position: "absolute", left: 0, top: 0, bottom: 0, width: `${r.from * 100}%`, background: "#8a8d89", borderRadius: 9 }} />
              <div style={{ position: "absolute", left: 0, top: 0, bottom: 0, width: `${v * 100}%`, background: LIGHT.ink, borderRadius: 9,
                opacity: frame < inAt + 1.5 * BEAT ? 0 : 1 }} />
            </div>
            <div style={{ marginTop: 10, fontSize: 24, color: LIGHT.dim }}>was {r.from.toFixed(2)}x in 1.0.6 · 1.00x is full speed</div>
          </div>
        );
      })}
    </>
  );
};

/** The last seconds: the name, both handhelds, where to get it, thanks, the music's credit */
export const EndCard110: React.FC<{ frame: number; start: number; still?: boolean }> = ({ frame, start, still }) => {
  const k = (d: number) => (still ? 1 : pop(frame, start + d, 180, 16));
  const rise = (d: number) => ({ opacity: Math.min(1, k(d) * 1.4), transform: `translateY(${(1 - k(d)) * 40}px)` });
  return (
    <div style={{ position: "absolute", left: 1220, width: 640, top: 0, bottom: 0, display: "flex", flexDirection: "column",
      justifyContent: "center", fontFamily: FONT }}>
      <Img src={staticFile("img/lockup-light.png")} style={{ ...rise(14), width: 400, marginLeft: -8, marginBottom: 30 }} />
      <div style={{ ...rise(18), fontSize: 44, lineHeight: 1.15, fontWeight: 600, color: LIGHT.text }}>
        Free, for the GKD Pixel&nbsp;2 and the TrimUI&nbsp;Brick.
      </div>
      <div style={{ ...rise(22), fontSize: 60, fontWeight: 600, color: LIGHT.ink, marginTop: 20, letterSpacing: -1 }}>zyperos.com</div>
      <div style={{ ...rise(28), fontSize: 30, lineHeight: 1.35, color: LIGHT.text, marginTop: 34 }}>
        Thank you, Discord. So much of 1.1 came from you.
      </div>
      <div style={{ ...rise(34), fontSize: 18, lineHeight: 1.5, color: LIGHT.dim, marginTop: 34 }}>
        Music: {MUSIC.artist}, {MUSIC.title} ({MUSIC.license}).<br />
        Games and box art shown belong to their owners.
      </div>
    </div>
  );
};
