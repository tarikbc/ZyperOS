# System artwork

The original pixel console icons are by **BenJelter**:
[Console Icon Pack](https://benjelter.itch.io/console-icon-pack).
Please support the original artist at that page.

The supplied **Pixel UI - Smart Folders v1.4** pack credits
**YOjeremyRaps** for additional icons, consoles and animations, based on
BenJelter's original work. Its metadata identifier is
`animated-pixel-console-icons-smart-folders-icons-8fd837`.
The source archive is `pixel-ui-smart-folders.zip`.

Imported assets retain this attribution. They are not relabeled as original
ZyperOS work or included under ZyperOS's license. No separate license text
was present in the supplied archive. Credit alone does not add license terms.

## ZyperOS additions

`source/WSC.png` is a new WonderSwan Color illustration for ZyperOS, generated
with the built-in image tool from the user's photograph on 2026-11-04. It is
not an original icon by BenJelter or YOjeremyRaps. The photograph served as a
hardware reference and is not included in the repository.

The corrected illustration uses the Crystal Blue shell, two left-hand groups
of four controls, diagonal lower-right A/B buttons, a speaker above them, and
the three bottom controls. It replaces the inaccurate silver draft.

### Drawn from real hardware photos

Ten shelves had no icon in the pack. I rejected invented hardware, so
`tools/systemart-draw.py` draws them from real photos on Wikimedia Commons:
each photo is reduced to a small pixel map (`drawn/<KEY>.txt`, which names
its photo, author and license) and styled like the pack, with its tone
ramps, a one-pixel black outline and its two-frame motion. Ports uses the
pack's own DOS PC (`by_platform/dos/logo.gif`), as PortMaster ports are
mostly PC games (My pick, 2026-11-05).

A drawing from a public-domain photo is public domain. A drawing from a
CC BY or CC BY-SA photo is shared under that license, crediting the
photographer below.

- `source/3DO.gif`, 3DO: [3DO-FZ-1-Console-FL.png](https://commons.wikimedia.org/wiki/File:3DO-FZ-1-Console-FL.png) by Evan-Amos, modified by Gunnar.offel (Public domain); [3DO-FZ1-Console-Set.jpg](https://commons.wikimedia.org/wiki/File:3DO-FZ1-Console-Set.jpg) by Evan-Amos (CC BY-SA 3.0)
- `source/AW.gif`, Atomiswave: [Atomiswave.jpg](https://commons.wikimedia.org/wiki/File:Atomiswave.jpg) by Black Squirrel (CC BY 4.0); [Cartouche Atomiswave.jpg](https://commons.wikimedia.org/wiki/File:Cartouche_Atomiswave.jpg) by Jpl752 (CC BY-SA 4.0)
- `source/GW.gif`, Game & Watch: [Game&watch-donkey-kong-2.png](https://commons.wikimedia.org/wiki/File:Game&watch-donkey-kong-2.png) by Evan-Amos (Public domain)
- `source/J2ME.gif`, J2ME: [Nokia-NGage-Front-Flat.jpg](https://commons.wikimedia.org/wiki/File:Nokia-NGage-Front-Flat.jpg) by Evan-Amos (Public domain)
- `source/MSX2.gif`, MSX2: [MSX FS-A1WX.JPG](https://commons.wikimedia.org/wiki/File:MSX_FS-A1WX.JPG) by Xxm (CC BY-SA 3.0)
- `source/MUSE.gif`, Muse: [Sony-wm-fx421-walkman.jpg](https://commons.wikimedia.org/wiki/File:Sony-wm-fx421-walkman.jpg) by Evan-Amos (Public domain)
- `source/NAOMI.gif`, NAOMI: [Naomi 2.jpg](https://commons.wikimedia.org/wiki/File:Naomi_2.jpg) by Mahcann (CC BY-SA 4.0)
- `source/NEOCD.gif`, Neo Geo CD: [Neo-Geo-CD-TL-FL.png](https://commons.wikimedia.org/wiki/File:Neo-Geo-CD-TL-FL.png) by Evan-Amos (Public domain); [Neo-Geo-CD-Controller-FL.jpg](https://commons.wikimedia.org/wiki/File:Neo-Geo-CD-Controller-FL.jpg) by Evan-Amos (Public domain)
- `source/VECTREX.gif`, Vectrex: [Vectrex-Console-Set.png](https://commons.wikimedia.org/wiki/File:Vectrex-Console-Set.png) by Evan-Amos (CC BY-SA 3.0); [GCE-Vectrex-Controller-FL.jpg](https://commons.wikimedia.org/wiki/File:GCE-Vectrex-Controller-FL.jpg) by Evan-Amos (Public domain)

The eight below are not shelves. They are for the File Transfer page's
folders: the page shows them beside each folder that holds no system, as
the shelf icons stand beside the systems' folders. Music keeps the Muse
walkman. `tools/systemart-reduce.py` made their maps from the photos (its
options are on each map's `## reduced:` line), and `tools/systemart.py`
lists them in the manifest without making them shelves.

- `source/ROMS.gif`, ROMs: [Nintendo-Game-Boy-Cartridge-Black.jpg](https://commons.wikimedia.org/wiki/File:Nintendo-Game-Boy-Cartridge-Black.jpg) by Evan-Amos (Public domain)
- `source/BIOS.gif`, BIOS: [PhoenixBios K7 - 01.jpg](https://commons.wikimedia.org/wiki/File:PhoenixBios_K7_-_01.jpg) by Xosema (CC BY-SA 4.0)
- `source/SAVES.gif`, Saves: [PSX-Memory-Card.png](https://commons.wikimedia.org/wiki/File:PSX-Memory-Card.png) by Evan-Amos, modified by Gunnar.offel (Public domain)
- `source/STATES.gif`, Save States: [Floppy disk R08.jpg](https://commons.wikimedia.org/wiki/File:Floppy_disk_R08.jpg) by Marc Ryckaert (CC BY-SA 4.0)
- `source/BGM.gif`, menu music (Roms/BGM): [Compact audio cassette 4.jpg](https://commons.wikimedia.org/wiki/File:Compact_audio_cassette_4.jpg) by Orion 8 (Public domain)
- `source/VIDEOS.gif`, Videos: [VHS-Video-Tape-Top-Flat.jpg](https://commons.wikimedia.org/wiki/File:VHS-Video-Tape-Top-Flat.jpg) by Evan-Amos (Public domain)
- `source/BOOKS.gif`, Audiobooks: [PediaPress Hardcover standing01.jpg](https://commons.wikimedia.org/wiki/File:PediaPress_Hardcover_standing01.jpg) by PediaPress (CC BY-SA 3.0)
- `source/FOLDER.gif`, any other folder: [Manila folder.jpg](https://commons.wikimedia.org/wiki/File:Manila_folder.jpg) by allispossible.org.uk (CC BY 2.0)

Console names and marks identify the depicted hardware and belong to their
respective owners. Generated additions do not imply endorsement by the
original icon artists or hardware makers.
