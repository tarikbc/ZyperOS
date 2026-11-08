# Launcher screenshots

The screenshots in this folder show the current launcher in Beard, the light
theme: the README and the website show every screen in light. `dark/` holds
the two screens their light-and-dark sections show in Salt & Pepper, the home
and the ZyperOS menu. The 3D device on the website shows the light ones too.
The logo/wordmark files remain the branding generator's output.

These are 1024x768 frames rendered by the native ZyperOS launcher, not edited
mockups or photographs of a running device. The three `portal` shots are the
File Transfer page (res/web) in headless Chrome, at a computer's 1280x800 and
a phone's 390x844, served by the real routes over a scratch card of freeware
names (build-native/portal-serve). The source commit, command flags
and output hashes are recorded in `captures.json`, which also names the frames
reused from the sessions that built a screen.

The capture uses temporary demonstration cards: empty game and music files
with freeware or stand-in names, a card with no games for the empty home, and
curated Metal Gear Solid metadata. The launcher draws every game card itself:
no box art and no game screens are included. The home screens show the
system art in res/system-icons. MP4 shows clearly named demonstration albums.
Achievement rows, playback timing, transfer progress, the hotspot, the
Scraper's counts, the Storage page and the System Update page use the launcher's built-in shot
fixtures and stand-in values. No ROMs, audio, account credentials or personal
save data are included or required. The clock and battery are fixed preview
values.

## Refresh

```sh
make native
python3 tools/readme-shots.py
```

Review `build/readme-shots/light/` and `dark/`. Copy the approved PNGs to this
folder and `dark/`, and copy `manifest.json` to `captures.json`. Names after
the command take only those shots (`python3 tools/readme-shots.py menu`).
`--art-dir` copies local covers into the card; leave it out for these
pictures. The page shots need Google Chrome (`--chrome` names another
Chromium, `--no-page` leaves them out), and the page's server listens on port
8080 or 8081 while they are taken. The launcher and the page make no requests
outside this computer for the captures. A browser's frames are not quite
repeatable: a shadow can differ by a few pixels from run to run, and the
upload's percentage by its timing.

Then rebuild the device poster and the static site:

```sh
blender --background --python tools/build-pixel2-model.py -- --render
sh mk/site.sh
```
