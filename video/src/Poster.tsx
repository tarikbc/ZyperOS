import React from "react";
import { AbsoluteFill } from "remotion";
import { DeviceInfo, Footage, LIGHT, poseOf, Device } from "./parts";
import { HeroText } from "./Tour";
import { Clip } from "./timeline";

export type PosterProps = { footage: Footage; device: DeviceInfo; play: boolean };

/** The PlayStation shelf with its covers, as the tour shows it */
const SHELF: Clip[] = [{ kind: "take", take: "shelf", at: 0, len: 1, fromMs: 5100 }];

/** The hero, whole, with the intro's words; the README's has a play button */
export const Poster: React.FC<PosterProps> = ({ device, play }) => (
  <AbsoluteFill style={{ background: LIGHT.ground }}>
    <Device d={device} cam={poseOf("hero", device)} clips={SHELF} shadow={0.2} />
    <HeroText frame={0} at={0} len={10} still />
    {play && (
      <div style={{ position: "absolute", left: 1140, bottom: 150, display: "flex", alignItems: "center", gap: 22,
        fontFamily: "Lexend, sans-serif", fontSize: 30, fontWeight: 600, color: LIGHT.text }}>
        <div style={{ width: 92, height: 92, borderRadius: 46, background: LIGHT.accent, display: "flex",
          alignItems: "center", justifyContent: "center", boxShadow: "0 10px 30px rgba(0,0,0,0.18)" }}>
          <div style={{ width: 0, height: 0, borderTop: "19px solid transparent", borderBottom: "19px solid transparent",
            borderLeft: `31px solid ${LIGHT.text}`, marginLeft: 8 }} />
        </div>
        Watch the tour
      </div>
    )}
  </AbsoluteFill>
);
