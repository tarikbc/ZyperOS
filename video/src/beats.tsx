import React from "react";
import { Easing, interpolate } from "remotion";
import { ease } from "./anim";
import { FPS } from "./timeline";
import { FONT, LIGHT, pop } from "./parts";

const secs = (frame: number, start: number) => (frame - start) / FPS;
const fmt = (s: number) => (s < 10 ? `${s.toFixed(1)} s` : `${Math.floor(s)} s`);

/** One racer: a name, a timer, and a bar out of 40 seconds */
const Row: React.FC<{ y: number; name: string; t: number; done: boolean; color: string; k: number }> = ({ y, name, t, done, color, k }) => (
  <div style={{ position: "absolute", left: 960, top: y, width: 880, fontFamily: FONT, opacity: Math.min(1, k * 1.5),
    transform: `translateX(${(1 - k) * 80}px)` }}>
    <div style={{ display: "flex", alignItems: "baseline", justifyContent: "space-between" }}>
      <div style={{ fontSize: 34, fontWeight: 600, color: LIGHT.text }}>{name}</div>
      <div style={{ fontSize: 64, fontWeight: 600, color: done ? LIGHT.ink : LIGHT.text, fontVariantNumeric: "tabular-nums", letterSpacing: -1 }}>
        {fmt(t)}{done ? " ✓" : ""}
      </div>
    </div>
    <div style={{ marginTop: 10, height: 18, borderRadius: 9, background: "#cfcdc2", overflow: "hidden" }}>
      <div style={{ height: "100%", width: `${Math.min(100, (t / 40) * 100)}%`, background: color, borderRadius: 9 }} />
    </div>
  </div>
);

/**
 * ZyperOS's start against stock firmware's: Tarik's own timing, about 2 s
 * against 30 to 40. ZyperOS's side is the device on the left, the real boot
 * animation into the home; stock's is a plain loading panel, nothing of its
 * own interface, its timer sped up once ZyperOS is done.
 */
export const Race: React.FC<{ frame: number; start: number; end: number }> = ({ frame, start, end }) => {
  const t = secs(frame, start) - 0.3;
  const zy = Math.max(0, Math.min(2, t));
  const lapse = 15;
  const stock = t < 2 ? Math.max(0, t) : Math.min(35, 2 + (t - 2) * lapse);
  const out = 1 - ease(frame, end - 6, end);
  const k = (d: number) => pop(frame, start + d, 160, 16) * out;
  const spin = (frame - start) * 14;
  return (
    <>
      <Row y={360} name="ZyperOS" t={zy} done={t >= 2} color={LIGHT.ink} k={k(4)} />
      <Row y={540} name="Stock firmware" t={stock} done={false} color="#8a8d89" k={k(7)} />
      <div style={{ position: "absolute", left: 960, top: 730, display: "flex", alignItems: "center", gap: 28, fontFamily: FONT,
        opacity: Math.min(1, k(10) * 1.5) }}>
        <div style={{ width: 280, height: 210, borderRadius: 14, background: "#2b2d2b", display: "flex", alignItems: "center",
          justifyContent: "center", boxShadow: "0 16px 40px rgba(17,19,16,0.25)" }}>
          <div style={{ width: 54, height: 54, borderRadius: 27, border: "6px solid #555955", borderTopColor: "#b9bcb7",
            transform: `rotate(${spin}deg)` }} />
        </div>
        <div style={{ fontSize: 24, lineHeight: 1.45, color: LIGHT.dim, maxWidth: 520 }}>
          Stock firmware, still starting.
          <br />
          {t >= 2 ? `Time-lapse ×${lapse}.` : " "}
        </div>
      </div>
    </>
  );
};

/** POWER, pressed: a key cap that pops at each press */
const Key: React.FC<{ frame: number; at: number }> = ({ frame, at }) => {
  if (frame < at - 2 || frame > at + 24) return null;
  const k = pop(frame, at, 260, 14);
  const press = interpolate(frame, [at, at + 3, at + 8], [1, 0.9, 1], { extrapolateLeft: "clamp", extrapolateRight: "clamp" });
  const fade = 1 - ease(frame, at + 16, at + 24, Easing.in(Easing.cubic));
  return (
    <div style={{ display: "inline-flex", alignItems: "center", justifyContent: "center", height: 64, padding: "0 28px",
      borderRadius: 32, background: "#1d201e", color: "#e4e2d8", fontFamily: FONT, fontSize: 28, fontWeight: 600, letterSpacing: 2,
      transform: `scale(${(0.6 + 0.4 * k) * press})`, opacity: Math.min(1, k * 2) * fade, boxShadow: "0 10px 24px rgba(0,0,0,0.25)" }}>
      POWER
    </div>
  );
};

/** Off, and back in the game: the two presses, and the timer from the second to the game */
export const Power: React.FC<{ frame: number; start: number; end: number; presses: number[]; backAt: number }> = ({
  frame, start, end, presses, backAt,
}) => {
  const on = start + presses[1] * FPS;
  const t = Math.max(0, Math.min((backAt - presses[1]), (frame - on) / FPS));
  const shown = frame >= on;
  const done = frame >= start + backAt * FPS;
  const out = 1 - ease(frame, end - 6, end);
  return (
    <div style={{ position: "absolute", left: 1320, top: 760, width: 540, fontFamily: FONT, opacity: out }}>
      <div style={{ height: 70 }}>{presses.map((p) => <Key key={p} frame={frame} at={start + p * FPS} />)}</div>
      {shown && (
        <div style={{ marginTop: 14, fontSize: 64, fontWeight: 600, color: done ? LIGHT.ink : LIGHT.text, fontVariantNumeric: "tabular-nums",
          letterSpacing: -1, opacity: ease(frame, on, on + 6) }}>
          {t.toFixed(1)} s{done ? " ✓" : ""}
        </div>
      )}
    </div>
  );
};
