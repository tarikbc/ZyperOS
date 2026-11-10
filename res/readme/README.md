# Launcher screenshots

The screenshots in this folder show the current launcher in Beard, the light
theme: the README and the website show every screen in light. `dark/` holds
the screens their light-and-dark sections show in Salt & Pepper, the
Nintendo 64 shelf, the home, the ZyperOS menu and a game's Cover and Video
page, and the top bar's shot (`topbar`, the song's note and the
headphone mark) in both themes. The 3D device on the website shows the light ones too.
The logo/wordmark files remain the branding generator's output.

These are 1024x768 frames rendered by the native ZyperOS launcher, not edited
mockups or photographs of a running device. The three `portal` shots are the
File Transfer page (res/web) in headless Chrome, at a computer's 1280x800 and
a phone's 390x844, served by the real routes over a scratch card of freeware
names (build-native/portal-serve). The source commit, command flags
and output hashes are recorded in `captures.json`, with the art each one
shows.

The capture uses temporary demonstration cards: empty game files named after
well-known games, empty music files with stand-in names, a card with no games
for the empty home, and curated Metal Gear Solid details (the synopsis is in
my words). The shots show the games' real box art, as the launcher's own
Scraper would put it on the card: covers, title screens and screenshots from
the [libretro-thumbnails](https://github.com/libretro-thumbnails) sets,
fetched once into a cache on the computer that takes the shots, outside this
repository. The Cover and Video shots (`media`, `media-look`, `media-video`)
show Metal Gear Solid's boxes, title screen and screenshot in the places the
page's fixture names; the in-game menu and Controls shots stand over a
Spyro the Dragon screenshot; the video snaps behind the play marks are two
seconds of each game's libretro screenshot. `captures.json` names every
picture used, its set and its checksum. The games and their art belong to
their owners: ZyperOS ships no games and no art, and only these composed
screenshots are in the repository.

The home screens show the system art in res/system-icons. MP4 shows clearly
named demonstration albums, a film named after Blender's freely licensed
Sintel and the public-domain Treasure Island as a book, all of them empty
files. The ScummVM shelf shows the freeware games ScummVM's own site gives
away, with their libretro covers. The made-up port behind the Ports menu and
Controls shots (tools/fixtures/ports) has no art. The games pages' hearts
mark demonstration favorites, set for those shots alone.
Achievement rows and badges, playback timing, transfer progress, the hotspot,
the Scraper's counts, the Storage page and the System Update page use the
launcher's built-in shot fixtures and stand-in values. No ROMs, audio,
account credentials or personal save data are included or required. The
clock and battery are fixed preview values.

## Refresh

```sh
make native
python3 tools/readme-shots.py --art
```

Review `build/readme-shots/light/` and `dark/`. Copy the approved PNGs to this
folder and `dark/`, and copy `manifest.json` to `captures.json`. Names after
the command take only those shots (`python3 tools/readme-shots.py menu`).
`--art` fetches what the cache lacks from raw.githubusercontent.com the first
time (ffmpeg makes the video snaps) and nothing after; `--art-cache` moves
the cache. Without `--art` every game wears the launcher's generated card. The page shots need Google Chrome (`--chrome` names another
Chromium, `--no-page` leaves them out), and the page's server listens on port
8080 or 8081 while they are taken. Apart from that first fetch of the art, the launcher and the page make no
requests outside this computer for the captures. A browser's frames are not quite
repeatable: a shadow can differ by a few pixels from run to run, and the
upload's percentage by its timing.

Then rebuild the device poster and the static site:

```sh
blender --background --python tools/build-pixel2-model.py -- --render
sh mk/site.sh
```
