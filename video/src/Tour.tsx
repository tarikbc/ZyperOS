import React from "react";
import { AbsoluteFill, Audio, Img, interpolate, Sequence, staticFile, useCurrentFrame } from "remotion";
import { ease, windowed } from "./anim";
import { CameraKey, CAMERA, CAPTIONS, CLIPS, DARK_AT, FPS, INTRO_TEXT, LIGHT_AT, MUSIC_START_S, OUTRO_TEXT, SCENES, TOTAL } from "./timeline";
import { cameraAt, CaptionBlock, Device, DeviceInfo, FONT, Footage, theme } from "./parts";
import { SOUND_CUES } from "./sounds";

export type TourProps = { footage: Footage; device: DeviceInfo };

/** The words beside the whole handheld, at the start and the end */
export const HeroText: React.FC<{ frame: number; at: number; len: number; outro?: boolean; still?: boolean }> = ({
  frame, at, len, outro, still,
}) => {
  const t = theme(0);
  const vis = still ? 1 : windowed(frame, at, at + len, 14, outro ? 1 : 10);
  if (vis <= 0) return null;
  const rise = (delay: number) => {
    const k = still ? 1 : ease(frame, at + delay, at + delay + 16);
    return { opacity: Math.min(k, vis), transform: `translateY(${(1 - k) * 20}px)` };
  };
  return (
    <div style={{ position: "absolute", left: 1140, width: 660, top: 0, bottom: 0, display: "flex", flexDirection: "column",
      justifyContent: "center", fontFamily: FONT }}>
      <Img src={staticFile("img/lockup-light.png")} style={{ ...rise(0), width: 400, marginLeft: -8, marginBottom: 36 }} />
      {outro ? (
        <>
          <div style={{ ...rise(5), fontSize: 50, lineHeight: 1.12, fontWeight: 600, color: t.text }}>
            Free, and it will stay free.
          </div>
          <div style={{ ...rise(10), fontSize: 27, lineHeight: 1.45, color: t.soft, marginTop: 20 }}>
            ZyperOS 1.0.2 for the GKD Pixel&nbsp;2, with ZyperOS Installer for macOS, Windows and Linux.
          </div>
          <div style={{ ...rise(15), fontSize: 40, fontWeight: 600, color: t.ink, marginTop: 34 }}>zyperos.com</div>
          <div style={{ ...rise(20), fontSize: 18, lineHeight: 1.5, color: t.dim, marginTop: 40 }}>
            Music: The Arplands by LordZintick (CC0), from ZyperOS's menu music.<br />
            Games and box art shown belong to their owners.
          </div>
        </>
      ) : (
        <>
          <div style={{ ...rise(5), fontSize: 52, lineHeight: 1.1, fontWeight: 600, color: t.text, textWrap: "balance" } as React.CSSProperties}>
            A fast, light firmware for the GKD Pixel&nbsp;2 that plays your whole retro library.
          </div>
          <div style={{ ...rise(10), fontSize: 27, lineHeight: 1.45, color: t.soft, marginTop: 24 }}>
            Forty systems, PortMaster ports, and your music and videos, from a card you can take out at any time.
          </div>
        </>
      )}
    </div>
  );
};

const VOLUME_UI = 0.55;

export const Tour: React.FC<TourProps> = ({ footage, device }) => {
  const frame = useCurrentFrame();
  const dark = frame < LIGHT_AT ? ease(frame, DARK_AT, DARK_AT + 10) : 1 - ease(frame, LIGHT_AT, LIGHT_AT + 8);
  const t = theme(dark);
  const cam = cameraAt(frame, CAMERA as CameraKey[], device);
  const end = 1 - ease(frame, TOTAL - 20, TOTAL - 2);
  return (
    <AbsoluteFill style={{ background: t.ground, overflow: "hidden" }}>
      <AbsoluteFill style={{ opacity: Math.min(ease(frame, 0, 12), end) }}>
        <Device d={device} cam={cam} clips={CLIPS} shadow={0.2} />
        <HeroText frame={frame} {...INTRO_TEXT} />
        {CAPTIONS.map((c, i) => (
          <CaptionBlock key={i} frame={frame} dark={dark} {...c} />
        ))}
        <HeroText frame={frame} {...OUTRO_TEXT} outro />
      </AbsoluteFill>
      <Audio src={staticFile("audio/music.mp3")} startFrom={Math.round(MUSIC_START_S * FPS)}
        volume={(f) => interpolate(f, [0, 4, TOTAL - 75, TOTAL - 4], [0, 0.9, 0.9, 0], { extrapolateRight: "clamp" })} />
      {SOUND_CUES(footage).map((c, i) => (
        <Sequence key={i} from={c.frame} durationInFrames={12} layout="none">
          <Audio src={staticFile(`audio/ui/${c.sound}.wav`)} volume={VOLUME_UI * (c.volume ?? 1)} />
        </Sequence>
      ))}
    </AbsoluteFill>
  );
};

export { SCENES };
