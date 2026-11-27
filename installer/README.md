# ZyperOS Installer

Puts [ZyperOS](../README.md) on a microSD card for the
GKD Pixel 2 or the TrimUI Brick, or updates a card that already has it
without touching the games, music and saves on it. For macOS, Windows and
Linux.

Its window is in ZyperOS's design ([DESIGN.md](../DESIGN.md): Lexend, the
Beard and Salt & Pepper themes, Zyper).

## Download

Every ZyperOS release on the
[releases page](https://github.com/tarikbc/ZyperOS/releases/latest), from
1.0.0 on, carries the installer with ZyperOS's card images, at ZyperOS's
version: one for each device the release is for (`config/devices`). The GKD
Pixel 2's image is there; the TrimUI Brick's comes later.

| | |
|---|---|
| **macOS** (Apple Silicon and Intel) | `ZyperOS-Installer-<version>-macOS.zip` |
| **Windows** 10 and 11 | `ZyperOS-Installer-<version>-Windows.exe` |
| **Linux** (Debian, Ubuntu, Mint) | `ZyperOS-Installer-<version>-amd64.deb` |
| **Linux** (Fedora) | `ZyperOS-Installer-<version>-x86_64.rpm` |

The release's other installer files (`-macOS-update.tar.gz`, the `.sig`
files and `latest.json`) are for the installer's own updates.

The first time it opens:

- **macOS:** unzip it and open the app. It isn't signed by Apple, so macOS
  stops it the first time: open *System Settings > Privacy & Security*,
  scroll down, and choose *Open Anyway*. Writing a card asks for your
  password, and macOS may ask to let the installer use removable volumes.
- **Windows:** the exe needs nothing installed. If Windows says it protected
  your PC, choose *More info* and *Run anyway*. It asks to run as
  administrator, which writing a card needs.
- **Linux:** install the package, `sudo apt install ./ZyperOS-Installer-*.deb`
  or `sudo dnf install ./ZyperOS-Installer-*.rpm`, and open ZyperOS
  Installer from your apps. Writing a card asks for your password.

## Using it

1. **Device:** each device the newest release has a card image for
   (`ZyperOS-v<version>-<id>.img.xz`): the GKD Pixel 2 today, the TrimUI
   Brick once a release has its image. A device with no image there is not
   offered, unless an image file you choose is for it. A card that already
   has ZyperOS picks its device by itself, when that device is offered.
   The R36S is a test device: it is offered only for an image file that is
   for it (boot partition `R36SBOOT`), as the TrimUI Smart Pro is.
   - **Board** (the R36S only): the name printed on the board, under the
     back cover, for the panel its card drives: the most common boards
     first (R36S-V12, V21, V22, V30), then the others, then each panel by
     its own name. Each board's panel is the one its stock device tree has
     (dArkOSen-R36S's trees, `src-tauri/src/r36s.rs` says how each was
     read). The installer puts `zy_panel=<panel>` in `r36s-boot.txt` on the
     image's boot partition on its way to the card, reads it back there, and
     the card is read back whole after the write, as always. *Not sure*
     writes nothing: the card drives panel 4, its own default. If the
     screen stays black, pick another board and install again (or edit
     `r36s-boot.txt` on a computer, as the file says). The choice is a menu
     of the installer's own, over the page, not the system's drop-down.
2. **ZyperOS:** the latest release, downloaded from GitHub and checked
   against the release's `SHA256SUMS`; or an image file on your computer
   (`ZyperOS-v<version>-<device>.img.xz`, or a plain `.img`). Choose one, or
   drop it on the window. A file with a `SHA256SUMS` beside it that names it,
   as `make release` in plastron leaves them, is checked against it first. An
   image that isn't ZyperOS's, or is for the other device, is refused.
3. **Card:** put the card in. It shows up by itself, with what is on it.
4. **PICO-8**, if you want it: your own PICO-8, the "PICO-8 for Raspberry
   Pi" zip you bought from Lexaloffle (`pico-8_<version>_raspi.zip`).
   Choose it, or drop it on the window. The installer never downloads
   PICO-8, and keeps no copy of it: it reads the zip on this computer,
   checks that it is the Raspberry Pi build (it has `pico8_64`, the 64-bit
   ARM build the handheld runs), and copies `pico8_64`, `pico8.dat`, the
   zip's `license.txt` and a `version.txt` to this card only, in
   `Bios/PICO-8` on its ZYPEROS partition. The zips for Windows, Mac and
   Linux on a PC are refused, as are those too old to have `pico8_64`, and
   versions 0.2.5d and 0.2.5e, whose `pico8_64` shows a black screen on the
   handheld; the row says which, and nothing is written until the zip is
   removed or another is chosen. Each file is written to a temporary name,
   read back and compared with the zip's, then renamed into place.
5. **What to do:**
   - **Update** a card that already has ZyperOS (or TortOS) for that
     device: the system part is written, and the games, music, saves and
     settings stay. Your PICO-8, if chosen, is copied first, while the
     ZYPEROS partition is still mounted.
   - **Fresh install** on any card: it is erased, and ZyperOS goes on. A
     card has no ZYPEROS partition until its first start makes it, so your
     PICO-8, if chosen, goes in `pico8/` on the image's boot partition, on
     its way to the card, and the first start moves it to `Bios/PICO-8`. An
     update of a card that never started does the same. When the boot
     partition has no room for it, the installer says so and writes
     nothing.
   - **Add PICO-8** on a card that has started in the handheld: your
     PICO-8 only, copied to its ZYPEROS partition. No system is written,
     and no password is asked.

**The Brick's graphics and Wi-Fi files.** The Brick needs closed files for
its graphics and its Wi-Fi and Bluetooth: Imagination's PowerVR driver and
GPU firmware, and XRadio's firmware, which come with its chip and which
ZyperOS does not pass on. For a Brick card, Install and Update first get
them from where they are published, Creality's Sonic Pad SDK on GitHub (at
commit 7f37a7f), check each against the sums plastron's build checks, and
put them only on that card, in `closed/` on its boot partition, where System
Update never touches them. They are fetched before anything is written:
when one can't be had, the installer says which and why, and writes nothing.
A copy is kept on the computer, so the next Brick card needs no download.

Before anything is written, a sheet names the card, its reader, its disk and
its size; a fresh install also asks you to tick that you want that card
erased. Everything written is read back and compared. When it's done, the
card is ejected, ready to go in the handheld.

### What it never writes

- Only removable media is listed: an SD card in a reader (the Mac's built-in
  reader included) or a flash drive. Fixed disks are never offered, external
  ones included.
- The disk the computer runs from is never offered, removable or not: a Mac
  or a PC started from a USB drive, a Linux live stick.
- Just before the disk is opened, the card is looked for again: it must be
  the one confirmed, at the size confirmed. A card swapped in the same reader
  meanwhile is refused.
- A card with its lock switch down is shown, but can't be chosen.

### Its own updates

When it opens, the installer looks for a newer version of itself. If one is
out, a chip in the window's header says so:

- **macOS and Windows:** *Update to 1.0.7* downloads the new installer and
  checks its signature against the key this one carries; then *Restart to
  update* puts it in this one's place and opens it. Nothing downloads before
  the click, it never restarts by itself, and never while a card is being
  written: the chip waits for the write to end. The new one opens with no
  first-time question from macOS, as the installer downloaded it itself. On
  Windows the exe keeps its name, so shortcuts still work. A Mac app in a
  folder your account can't write to asks for an administrator's password.
- **Linux:** *Installer 1.0.7 is out* opens the releases page: update the
  package as you installed it.
- An update that didn't work says so, and its click opens the releases page.

A Mac app opened straight from Downloads runs from a read-only copy macOS
makes of it, even after *Open Anyway*: the update replaces the app in
Downloads and opens that one, and from then on macOS runs it where it is.

Installers up to 1.0.5 can't update themselves: their chip opens the releases
page.

### How each system writes

- **macOS:** the card's volumes are unmounted, and its raw disk is opened
  through `authopen`, Apple's tool for this: it asks for the password and
  hands the open disk back, so the installer never runs as root.
- **Windows:** the installer runs as administrator (its manifest asks UAC),
  and every volume on the card is locked or dismounted first.
- **Linux:** the window never runs as root: it runs itself again through
  `pkexec` (polkit asks for the password), in a helper mode with no window
  that writes the card and reports its progress.

## Building

Everything is built on a Mac: the macOS app natively, the Windows exe and the
Linux packages in Docker.

1. Install Rust with rustup, add the Intel target for the macOS app
   (`rustup target add x86_64-apple-darwin`), and either install the Tauri CLI
   (`cargo install tauri-cli --version "^2"`) or have Node, for
   `npx @tauri-apps/cli@2`.
2. Run `tools/release.sh` in this folder. It checks the page against
   ZyperOS's design (`tools/check-design.sh`: the colors against
   `res/web/app.css`, the fonts and marks against `res/`), checks that
   `THIRD-PARTY.md` is the list `tools/third-party-list.py` makes now and is
   committed, runs the tests, then builds all four files into `dist/<version>/` with their
   `SHA256SUMS`. Nothing is published. The first run takes a while: it
   builds the Linux image from `tools/linux.Dockerfile` and fetches the
   Windows SDK for `cargo-xwin`.

The installer is released with ZyperOS, at ZyperOS's version
(`tools/set-version.sh` at the top sets both): `make release PUBLISH=1` makes
the release with the card image, and GitHub builds these four files for it
on the tag (`.github/workflows/release.yml`; see
[docs/releases](../docs/releases/README.md)). GitHub also builds the Linux
packages and the Windows exe, with the tests, for every pull request that
changes this folder (`.github/workflows/installer.yml`).

For a quick look while working:

```bash
cd src-tauri && cargo test
```

```bash
cd src-tauri && npx --yes @tauri-apps/cli@2 dev
```

`ZYPEROS_IMAGE` (Pixel 2), `ZYPEROS_BRICK_IMAGE` (Brick),
`ZYPEROS_SMARTPRO_IMAGE` (Smart Pro) and `ZYPEROS_R36S_IMAGE` (R36S) name a local
image the release row uses in place of a download, for trying a build before
it is published; its device is offered even when the release has no image
for it. The page also opens in a plain browser, on made-up cards and a
made-up write, to look at the design: serve `ui/` and open it (add
`?r36s` for an image file that is the R36S's, which shows its board choice,
`?release` for a release to download, the Pixel 2's only, `&brick` for the
Brick's too, `?nocard` for no card, and
`?update=available` for a newer installer to update to; `downloading`,
`ready`, `installing`, `failed` and `page` show the chip's other states;
`?zip=windows` makes the PICO-8 row's Choose take a zip it refuses).

### The installer's own updates

Tauri's updater ([tauri-plugin-updater](https://v2.tauri.app/plugin/updater/))
does the looking, the download and the signature check (`src/selfupdate.rs`):

- It reads `https://github.com/tarikbc/ZyperOS/releases/latest/download/latest.json`
  (`plugins.updater` in `src-tauri/tauri.conf.json`). Each release carries
  one, written by `tools/latest-json.py` in `.github/workflows/release.yml`:
  the macOS app as `ZyperOS-Installer-<version>-macOS-update.tar.gz` for both
  kinds of Mac, and the Windows exe itself, each with its signature. No Linux
  entry. When the latest release has none, or the look fails, the
  installer asks GitHub's releases list instead (`src/release.rs`), and the
  chip opens the releases page.
- The files are signed with the updater key, a minisign key that `tauri
  signer` makes. Its public half is `plugins.updater.pubkey`; the signature
  must also name the version `latest.json` announces (`requireSignedVersion`),
  so an older release can't be passed off as a newer one.
- macOS: the updater's own install swaps the new app in for the old one. An
  app macOS runs translocated (from a read-only copy, as it runs a download
  opened where it was saved) has the original replaced instead, found as
  that copy's mount source (`statfs`), and the original opened.
  Windows: the updater only installs Windows installers, and this exe is
  portable, so the installer renames itself to `<name>.old`, puts the new exe
  in its place, starts it, and the new one removes the `.old` file.

`tools/try-selfupdate.sh` tries it end to end on a Mac, with nothing
published: two debug builds signed with a throwaway key, a `latest.json` on
`127.0.0.1`, and the old app updating itself three times: as it is, marked
as a download the user allowed and moved, and marked as one allowed but left
where it was saved (which macOS translocates). Each time it must open again
as the new one, where the user has it, with no quarantine mark. Debug builds update with no click when
`ZYPEROS_INSTALLER_SELFUPDATE_TEST` names a file to log the steps to.

#### The updater key

The private key is on the Mac that makes releases, in
`~/.config/zyperos/installer-updater.key` (no password; never in the
repository), and in the repository's `INSTALLER_UPDATER_KEY` secret, which
only a release tag's build reads (a pull request signs with a throwaway key):

```bash
gh secret set INSTALLER_UPDATER_KEY --repo tarikbc/ZyperOS < ~/.config/zyperos/installer-updater.key
```

A release whose files don't check out against `plugins.updater.pubkey` stops
before anything is uploaded. Keep a copy of the key somewhere safe. If it is
ever lost or leaked, make a new one
(`npx --yes @tauri-apps/cli@2 signer generate --ci -p "" -w <file>`), put its
`.pub` file's text in `plugins.updater.pubkey`, and set the secret again: the
installers already out there refuse the next release's files, so for that one
update their chip opens the releases page, and from then on they use the new
key.

## Thanks

The disk writer, the card listing and the way each system opens a disk
started in Eric Reinsmidt's
[TortOS Installer](https://github.com/ericreinsmidt/TortOS-Installer), whose
history this folder keeps (a subtree, as `diatom/` is). Thank you, Eric.

## License

ZyperOS Installer is under the GNU General Public License, version 3
(`LICENSE`), with the additional terms in the repository's `NOTICE`. The name
and Zyper are trademarks; see the repository's `TRADEMARK.md`. It started from TortOS Installer by
Eric Reinsmidt, in its MIT-licensed versions; that code keeps his MIT notice,
in the repository's `LICENSES/MIT-TortOS-Installer.txt`.

The fonts in `ui/fonts` are Lexend, under the SIL Open Font License
(`ui/fonts/OFL.txt`).

What the apps are built from is listed in [THIRD-PARTY.md](THIRD-PARTY.md):
every Rust crate compiled into them, with its license, its repository and
which apps carry it, and Tauri, the system web views and the fonts.
`tools/third-party-list.py` makes it from `src-tauri/Cargo.lock` and the font
files; never edit it by hand. After changing a dependency, make it again with
`python3 tools/third-party-list.py` in this folder and commit it:
`tools/release.sh` stops when it is out of date.
