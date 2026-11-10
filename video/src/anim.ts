import { Easing, interpolate } from "remotion";

/** 0 to 1 between two frames, eased, clamped */
export const ease = (frame: number, from: number, to: number, easing = Easing.out(Easing.cubic)) =>
  interpolate(frame, [from, to], [0, 1], { extrapolateLeft: "clamp", extrapolateRight: "clamp", easing });

export const inOut = Easing.inOut(Easing.cubic);

/** Fades in over `inDur` from `start`, out over `outDur` before `end` */
export const windowed = (frame: number, start: number, end: number, inDur = 10, outDur = 8) =>
  Math.min(ease(frame, start, start + inDur), 1 - ease(frame, end - outDur, end, Easing.in(Easing.cubic)));

export const mix = (a: number, b: number, k: number) => a + (b - a) * k;

/** Two #rrggbb colours mixed */
export const mixColor = (a: string, b: string, k: number) => {
  const p = (c: string) => [1, 3, 5].map((i) => parseInt(c.slice(i, i + 2), 16));
  const [x, y] = [p(a), p(b)];
  return `rgb(${x.map((v, i) => Math.round(mix(v, y[i], k))).join(",")})`;
};
