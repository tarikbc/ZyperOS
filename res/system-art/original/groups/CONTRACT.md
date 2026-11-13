# Group contract

Each group owns one `<id>.js` file and one `<id>.json` record in this directory.
Each group owns its assigned source PNGs under `../source/`.
Do not change another group's files, `animation.js`, or the installed catalogue.

Register the group before `animation.js` loads:

```js
window.ZyperOriginalGroups ||= [];
window.ZyperOriginalGroups.push({
  id: 'example',
  assets: [{
    key: 'KEY', name: 'Name', file: 'KEY.png', width: 1024, height: 1024,
    description: 'Power switch', duration: 6000,
    motionBounds: [0, 0, 1024, 1024],
    // Optional screen. Coordinates use the source image's pixel space.
    screen: {rect: [100, 100, 400, 300], native: [160, 144], bezel: '#252731'}
  }],
  extraImages: [], // {key, file, width, height}
  render(ctx, key, time, env) {},
  getScreen(key, time, env) { return null; }
});
```

`render` draws only moving parts over the fixed source image.
`motionBounds` uses `[x, y, width, height]` in source pixels.
The context already uses the source image's coordinates.
`getScreen` returns a canvas at the screen's native resolution.
For two screens, use `screens: [{rect,native,bezel}, {rect,native,bezel}]`.
The shared renderer passes the screen index as the fourth `getScreen` argument.
The shared renderer places that canvas inside the screen rectangle and keeps its aspect ratio.
Set `screen.displayAspect` when the physical LCD uses non-square pixels.
For example, Game Gear uses a 160×144 raster on a 4:3 display.
Use a front view for handheld screens where possible.
`env` contains `images`, `sourceSizes`, `canvas(width,height)`, `polygon(ctx,points)`, `clipImage(ctx,key,points)`, `image(ctx,key)`, `nintendo(ctx,x,y,scale,color)`, and `cycle(time)`.
The Nintendo helper draws the documented Game Boy cartridge-header bitmap.
`cycle` returns a smooth 0-to-1-to-0 opening amount over 6000 ms.
Keep every source image immutable.
Keep the case, controller, and cable fixed.
Use a real power control or indicator when media motion adds no value.
Do not invent a startup sequence for hardware without a fixed startup screen.
Record that exception and use its real power or display behavior.

Use the built-in image tool for every new hardware image.
Use separate calls for separate assets.
Use the approved images in `../source/` as style references.
Use hardware references to check controls, proportions, and perspective.
Do not use pixels from the existing public icon pack.
Do not reduce hardware photos into icons.
Save every final source inside the repository.
Record the full prompts, source hashes, hardware references, motion choice, and perspective checks in the group JSON.
Generated sources may have different sizes. Read the actual PNG dimensions.
Do not assume the requested size is the returned size.

Review the generated image before adding it to the group.
Review moving parts against the case's projected axes.
Lids rotate about fixed hinges. Trays translate without stretching.
Cartridges pass behind slot edges. Screens preserve their native aspect ratio.
Do not use a substitute font for a requested original startup wordmark without marking it unfinished.
Report limitations honestly. Do not mark an asset complete until its image and animation are checked.

Provide a useful loop of 6000 ms unless the hardware startup needs a different duration.
Do not add sound.
The final export uses at most 64 unique poses per asset.
Report progress after each group of four assets.
