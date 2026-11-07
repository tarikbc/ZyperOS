# ZyperOS Installer

Puts [ZyperOS](../README.md) on a microSD card for the
GKD Pixel 2 or the TrimUI Brick, or updates a card that already has it
without touching the games, music and saves on it. For macOS, Windows and
Linux.

Its window is in ZyperOS's design ([DESIGN.md](../DESIGN.md): Lexend, the
Beard and Salt & Pepper themes, Zyper).

## Download

There is no release yet. When there is, every ZyperOS release on the
[releases page](https://github.com/tarikbc/ZyperOS/releases/latest) carries
the installer with ZyperOS's card images, at ZyperOS's version:

| | |
|---|---|
| **macOS** (Apple Silicon and Intel) | `ZyperOS-Installer-<version>-macOS.zip` |
| **Windows** 10 and 11 | `ZyperOS-Installer-<version>-Windows.exe` |
| **Linux** (Debian, Ubuntu, Mint) | `ZyperOS-Installer-<version>-amd64.deb` |
| **Linux** (Fedora) | `ZyperOS-Installer-<version>-x86_64.rpm` |

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

1. **Device:** the GKD Pixel 2 or the TrimUI Brick. A card that already has
   ZyperOS picks its device by itself.
2. **ZyperOS:** the latest release, downloaded from GitHub and checked
   against the release's `SHA256SUMS`; or an image file on your computer
   (`ZyperOS-v<version>-<device>.img.xz`, or a plain `.img`). Choose one, or
   drop it on the window. A file with a `SHA256SUMS` beside it that names it,
   as `make release` in plastron leaves them, is checked against it first. An
   image that isn't ZyperOS's, or is for the other device, is refused.
3. **Card:** put the card in. It shows up by itself, with what is on it.
4. **What to do:**
   - **Update** a card that already has ZyperOS (or TortOS) for that
     device: the system part is written, and the games, music, saves and
     settings stay.
   - **Fresh install** on any card: it is erased, and ZyperOS goes on.

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
   `res/web/app.css`, the fonts and marks against `res/`), runs the tests,
   then builds all four files into `dist/<version>/` with their
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

`ZYPEROS_IMAGE` (Pixel 2) and `ZYPEROS_BRICK_IMAGE` (Brick) name a local
image the release row uses in place of a download, for trying a build before
it is published. The page also opens in a plain browser, on made-up cards
and a made-up write, to look at the design: serve `ui/` and open it (add
`?release` for a release to download, `?nocard` for no card).

## Thanks

The disk writer, the card listing and the way each system opens a disk
started in Eric Reinsmidt's
[TortOS Installer](https://github.com/ericreinsmidt/TortOS-Installer), whose
history this folder keeps (a subtree, as `diatom/` is). Thank you, Eric.

## License

ZyperOS Installer is under the PolyForm Shield License 1.0.0 (`LICENSE`), with
the notice in the repository's `NOTICE`. It started from TortOS Installer by
Eric Reinsmidt, in its MIT-licensed versions; that code keeps his MIT notice,
in the repository's `LICENSES/MIT-TortOS-Installer.txt`.

The fonts in `ui/fonts` are Lexend, under the SIL Open Font License
(`ui/fonts/OFL.txt`). What else it is built from (Tauri, every Rust crate in
the apps, with their licenses) is in `THIRD-PARTY.md`, which
`tools/third-party.py` makes from `src-tauri/Cargo.lock`
(`python3 tools/third-party.py > THIRD-PARTY.md`, here); `tools/release.sh`
stops when the two differ.
