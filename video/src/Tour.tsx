import React from "react";
import { AbsoluteFill, Audio, Easing, Img, interpolate, Sequence, staticFile, useCurrentFrame } from "remotion";
import { ease } from "./anim";
import { BEAT, CLIPS, MUSIC_START_S, FPS, sceneAt, SCENES, STARTS, TOTAL } from "./timeline";
import { Cam, Device, DeviceInfo, FONT, Footage, Label, Laptop, LIGHT, DARK, mixCam, Phone, pop, poseOf, punchAt, Screen, Tile } from "./parts";
import { SOUND_CUES } from "./sounds";

export type TourProps = { footage: Footage; device: DeviceInfo };

const LANGS = ["en", "zh-Hans", "es", "pt-BR", "fr", "de", "ja", "it"];
const WHIP = 1700;

/** Where the main device is at a frame: springs between poses, whips in and out */
const camera = (frame: number, d: DeviceInfo): Cam => {
  const i = sceneAt(frame);
  const sc = SCENES[i];
  const start = STARTS[i];
  const end = start + sc.beats * BEAT;
  const target = poseOf(sc.pose === "duo" ? "away" : sc.pose, d);
  let cam: Cam;
  if (sc.enter === "whip") {
    const k = pop(frame, start, 150, 15);
    cam = { ...target, x: target.x + (1 - k) * WHIP, blur: Math.min(18, Math.max(0, (1 - k) * 40 - 2)) };
  } else if (sc.enter === "cut" || i === 0) {
    cam = target;
  } else {
    const prev = SCENES[i - 1].pose;
    cam = mixCam(poseOf(prev === "duo" ? "away" : prev, d), target, pop(frame, start, 150, 17));
  }
  // Out to the left, quick, when the next scene whips in
  const next = SCENES[i + 1];
  if (next?.enter === "whip" && sc.pose !== "duo" && sc.pose !== "away") {
    const k = ease(frame, end - 6, end, Easing.in(Easing.cubic));
    cam = { ...cam, x: cam.x - k * WHIP, blur: (cam.blur ?? 0) + k * 22 };
  }
  return cam;
};

export const Tour: React.FC<TourProps> = ({ footage, device }) => {
  const frame = useCurrentFrame();
  const i = sceneAt(frame);
  const sc = SCENES[i];
  const start = STARTS[i];
  const end = start + sc.beats * BEAT;
  const cam = camera(frame, device);
  const zoom = punchAt(frame, start, end, sc.punch);
  const labelAt = start + (sc.labelAt ?? 0) * BEAT;
  const fadeOut = 1 - ease(frame, TOTAL - 12, TOTAL - 1);

  // The pair, light and dark: the ground wipes to dark on its right half
  const duo = sc.id === "themes";
  const wipe = duo ? pop(frame, start + BEAT, 160, 20) : 0;

  return (
    <AbsoluteFill style={{ background: LIGHT.ground, overflow: "hidden" }}>
      <AbsoluteFill style={{ opacity: Math.min(ease(frame, 0, 4), fadeOut) }}>
        {duo && (
          <div style={{ position: "absolute", top: 0, bottom: 0, right: 0, width: 960 * wipe + 60 * wipe, background: DARK.ground,
            clipPath: "polygon(60px 0, 100% 0, 100% 100%, 0 100%)" }} />
        )}

        {sc.pose !== "away" && !duo && (
          <Device d={device} cam={cam}>
            <Screen clips={CLIPS} d={device} zoom={zoom} />
          </Device>
        )}

        {duo && (["home-light", "home-dark"] as const).map((src, n) => {
          const k = pop(frame, start + n * 4, 140, 14);
          const kk = 0.4;
          const c: Cam = { k: kk, x: 560 + n * 820 - 999.5 * kk, y: 600 - 1200 * kk + (1 - k) * 900 };
          const out = ease(frame, end - 6, end, Easing.in(Easing.cubic));
          return (
            <Device key={src} d={device} cam={{ ...c, y: c.y - out * 1200 }}>
              <Img src={staticFile(`footage/${src}.png`)} style={{ position: "absolute", left: device.screen.x, top: device.screen.y,
                width: device.screen.w, height: device.screen.h }} />
            </Device>
          );
        })}

        {sc.id === "languages" && LANGS.map((code, n) => {
          const k = pop(frame, start + 4 + n * 4, 190, 14);
          const out = ease(frame, end - 6, end, Easing.in(Easing.cubic));
          return (
            <div key={code} style={{ position: "absolute", inset: 0, transform: `translateX(${-out * 2000}px)` }}>
              <Tile src={`footage/lang-${code}.png`} x={115 + (n % 4) * 430} y={330 + Math.floor(n / 4) * 330} w={400} k={k} />
            </div>
          );
        })}

        {sc.browser?.laptop && (
          <Laptop src={sc.browser.laptop} x={900} y={250} width={940} dark={0}
            k={sc.browser.laptopAt === -1 ? 1 : pop(frame, start + (sc.browser.laptopAt ?? 0) * BEAT, 150, 16) *
              (SCENES[i + 1]?.browser?.laptop ? 1 : 1 - ease(frame, end - 5, end))} />
        )}
        {sc.browser?.phone && (
          <Phone src={sc.browser.phone} x={sc.browser.laptop ? 1560 : 1240} y={sc.browser.laptop ? 400 : 200} height={sc.browser.laptop ? 600 : 760}
            k={pop(frame, start + (sc.browser.phoneAt ?? 0) * BEAT, 160, 14) * (1 - ease(frame, end - 5, end))} />
        )}

        {sc.label && sc.pose === "main" && (
          <Label frame={frame} at={labelAt} end={end} text={sc.label} sub={sc.sub} dark={0} x={1320} y={540} width={540} size={72} />
        )}
        {sc.label && sc.pose === "split" && (
          <Label frame={frame} at={labelAt} end={end} text={sc.label} sub={sc.sub} dark={0}
            x={sc.browser?.laptop ? 900 : 900} y={sc.browser?.laptop ? 40 : 300} width={sc.browser?.laptop ? 1000 : 320} size={62} anchor="top" />
        )}
        {sc.label && duo && (
          <Label frame={frame} at={labelAt} end={end} text={sc.label} sub={sc.sub} dark={0} x={110} y={60} width={800} size={66} anchor="top" />
        )}
        {sc.label && sc.id === "languages" && (
          <Label frame={frame} at={labelAt} end={end} text={sc.label} dark={0} x={0} y={90} width={1920} size={70} align="center" anchor="top" />
        )}

        {sc.id === "end" && <EndCard frame={frame} start={start} />}
      </AbsoluteFill>

      <Audio src={staticFile("audio/music.mp3")} startFrom={Math.round(MUSIC_START_S * FPS)}
        volume={(f) => interpolate(f, [0, TOTAL - 45, TOTAL - 2], [0.9, 0.9, 0], { extrapolateRight: "clamp" })} />
      {SOUND_CUES(footage).map((c, n) => (
        <Sequence key={n} from={c.frame} durationInFrames={12} layout="none">
          <Audio src={staticFile(`audio/ui/${c.sound}.wav`)} volume={0.5 * (c.volume ?? 1)} />
        </Sequence>
      ))}
    </AbsoluteFill>
  );
};

/** The last three seconds: the name, where to get it, the music's credit */
export const EndCard: React.FC<{ frame: number; start: number; still?: boolean }> = ({ frame, start, still }) => {
  const k = (d: number) => (still ? 1 : pop(frame, start + d, 180, 16));
  const rise = (d: number) => ({ opacity: Math.min(1, k(d) * 1.4), transform: `translateY(${(1 - k(d)) * 40}px)` });
  return (
    <div style={{ position: "absolute", left: 1120, width: 740, top: 0, bottom: 0, display: "flex", flexDirection: "column",
      justifyContent: "center", fontFamily: FONT }}>
      <Img src={staticFile("img/lockup-light.png")} style={{ ...rise(0), width: 460, marginLeft: -10, marginBottom: 34 }} />
      <div style={{ ...rise(4), fontSize: 50, lineHeight: 1.12, fontWeight: 600, color: LIGHT.text }}>
        Free, for the GKD Pixel&nbsp;2.
      </div>
      <div style={{ ...rise(8), fontSize: 64, fontWeight: 600, color: LIGHT.ink, marginTop: 22, letterSpacing: -1 }}>zyperos.com</div>
      <div style={{ ...rise(14), fontSize: 19, lineHeight: 1.5, color: LIGHT.dim, marginTop: 44 }}>
        Music: The Arplands by LordZintick (CC0), from ZyperOS's menu music.<br />
        Games and box art shown belong to their owners.
      </div>
    </div>
  );
};
