import React from "react";
import { AbsoluteFill, Img, staticFile } from "remotion";
import { Device, DeviceInfo, FONT, Footage, LIGHT, poseOf, Screen } from "./parts";
import { Clip } from "./timeline";

export type PosterProps = { footage: Footage; device: DeviceInfo; play: boolean };

/** The PlayStation shelf with its covers, as the tour opens on it */
const SHELF: Clip[] = [{ kind: "take", take: "shelf", at: 0, len: 1, fromMs: 5100, rate: 1, scene: "poster" }];

/** The device whole, the site's headline; the README's has a play button */
export const Poster: React.FC<PosterProps> = ({ device, play }) => (
  <AbsoluteFill style={{ background: LIGHT.ground }}>
    <Device d={device} cam={poseOf("hero", device)}>
      <Screen clips={SHELF} d={device} />
    </Device>
    <div style={{ position: "absolute", left: 1120, width: 700, top: 0, bottom: 0, display: "flex", flexDirection: "column",
      justifyContent: "center", fontFamily: FONT }}>
      <Img src={staticFile("img/lockup-light.png")} style={{ width: 420, marginLeft: -8, marginBottom: 36 }} />
      <div style={{ fontSize: 54, lineHeight: 1.08, fontWeight: 600, color: LIGHT.text, letterSpacing: -1 }}>
        A fast, light firmware for the GKD Pixel&nbsp;2 that plays your whole retro library.
      </div>
      {play && (
        <div style={{ display: "flex", alignItems: "center", gap: 22, marginTop: 48, fontSize: 32, fontWeight: 600, color: LIGHT.text }}>
          <div style={{ width: 96, height: 96, borderRadius: 48, background: LIGHT.accent, display: "flex",
            alignItems: "center", justifyContent: "center", boxShadow: "0 10px 30px rgba(0,0,0,0.18)" }}>
            <div style={{ width: 0, height: 0, borderTop: "20px solid transparent", borderBottom: "20px solid transparent",
              borderLeft: `32px solid ${LIGHT.text}`, marginLeft: 8 }} />
          </div>
          Watch the tour
        </div>
      )}
    </div>
  </AbsoluteFill>
);
