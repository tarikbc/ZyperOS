import React from "react";
import { CalculateMetadataFunction, Composition, staticFile, Still } from "remotion";
import { Tour, TourProps } from "./Tour";
import { Poster, PosterProps } from "./Poster";
import { FPS, TOTAL } from "./timeline";

/** The footage index (video/footage/takes.py) and the device's screen rect (video/device) */
const load = async () => {
  const [footage, device] = await Promise.all([
    fetch(staticFile("footage/index.json")).then((r) => r.json()),
    fetch(staticFile("device/pixel2-front.json")).then((r) => r.json()),
  ]);
  return { footage, device };
};

const tourMeta: CalculateMetadataFunction<TourProps> = async () => ({ props: await load() });
const posterMeta: CalculateMetadataFunction<PosterProps> = async ({ props }) => ({ props: { ...props, ...(await load()) } });

const empty = { footage: {}, device: { image: "", width: 2000, height: 2400, screen: { x: 0, y: 0, w: 1, h: 1 } } };

export const Root: React.FC = () => (
  <>
    <Composition id="ZyperOSTour" component={Tour} durationInFrames={TOTAL} fps={FPS} width={1920} height={1080}
      defaultProps={empty as TourProps} calculateMetadata={tourMeta} />
    {/* The site's poster, and the README's, which carries a play button */}
    <Still id="Poster" component={Poster} width={1920} height={1080}
      defaultProps={{ ...empty, play: false } as PosterProps} calculateMetadata={posterMeta} />
    <Still id="PosterPlay" component={Poster} width={1920} height={1080}
      defaultProps={{ ...empty, play: true } as PosterProps} calculateMetadata={posterMeta} />
  </>
);
