# Mechanical animation overrides

Keep the approved source artwork and the existing group records.
Create one new group module for your assigned mechanisms.
Do not edit `animation.js`, `export.js`, or another group's module.

Register each override before `animation.js` loads:

```js
window.ZyperArtOverrides ||= [];
window.ZyperArtOverrides.push({
  id: 'console-lids',
  assets: [{
    key: 'SATURN',
    description: 'Disc lid opens and closes',
    duration: 6000,
    motionBounds: [x, y, width, height]
  }],
  extraImages: [{key:'SATURN_BAY',file:'SATURN-bay.png',width:1254,height:1254}],
  render(ctx,key,time,env) {}
});
```

The override merges its asset fields with the original profile.
Its render function replaces the original power-only animation.
The shared renderer keeps the original case pixels outside `motionBounds`.
The context uses source-image coordinates.
An optional `viewBox:[x,y,width,height]` can add static space for moving parts.
Use it instead of flattening a lid or clipping a cartridge.
The existing group contract defines `env` helpers.
New layers use the built-in image tool and stay inside `source/`.
Keep every generated source unchanged after selection.
Record full prompts, source hashes, hardware references, perspective axes, and motion checks in a matching JSON file.

Lids rotate about their real hinges.
Trays translate along the case depth direction without stretching.
Cartridges enter the correct slot and pass behind its near edge.
Keep controllers, cables, and the rest of the case fixed.
Keep important moving parts inside the 256-pixel export canvas.
Do not replace mechanical action with a light or reflection.
Use smooth pose changes for hardware. Two fixed poses are for the symbolic icons.
No browser tools: the parent owns the shared Playwright page and runs the visual checks.
