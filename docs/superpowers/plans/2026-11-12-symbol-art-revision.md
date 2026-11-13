# Symbol art revision

The user requested two-frame object animations instead of moving reflections.
The user also requested PortMaster art and large and small folder art.
MP4 must show both video and audio. Its user-facing name is MP4.

- [x] Replace every moving-highlight animation with two distinct object poses.
- [x] Add the missing console cartridge, lid, and disc-tray mechanisms.
- [x] Create an MP4 icon with video and audio symbols.
- [x] Create PortMaster art with a dock crane and game cartridge.
- [x] Create a large folder and a simplified small folder for list rows.
- [x] Export exactly two unique poses for the revised symbols.
- [x] Check perspective, static parts, transparent edges, and small-size readability.
- [x] Update the gallery, source records, and exported assets.

Keep the existing hardware startup sequences.
Replace power-only console animations with their real media-loading mechanisms.
Use the existing MUSE key internally, but show MP4 in all user-facing labels.
The large folder uses a 256-pixel canvas.
The small folder uses a separate simple design, tested at 24, 32, 40, and 48 pixels.

## Result

Ten symbols use two poses at 400 ms each. MP4 shows video and audio.
Nineteen missing media mechanisms are added.
PortMaster has a two-frame crane, an icon, and light/dark covers.
Folders have separate large and list designs, with five small sizes.
All 50 designs pass the renderer and exported-file checks. Runtime integration remains separate.
