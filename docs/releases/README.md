# Releases

Every ZyperOS version is one release on GitHub, tagged `v<version>`, with:

| File | What |
|---|---|
| `ZyperOS-v<version>-pixel2.img.xz` | The GKD Pixel 2's card image |
| `ZyperOS-v<version>-pixel2-sources.tar` | The licenses and sources of everything in it |
| `ZyperOS-v<version>-pixel2-update.zpk` | System Update's pack: every file of the image's ZyperOS folder (docs/updates.md) |
| `ZyperOS-pixel2-update.txt` | System Update's list of those files, signed; no version in its name, so `releases/latest/download/` finds the newest |
| `ZyperOS-Installer-<version>-macOS.zip` | ZyperOS Installer for macOS (Apple Silicon and Intel) |
| `ZyperOS-Installer-<version>-Windows.exe` | ZyperOS Installer for Windows 10 and 11 |
| `ZyperOS-Installer-<version>-amd64.deb` | ZyperOS Installer for Debian, Ubuntu and Mint |
| `ZyperOS-Installer-<version>-x86_64.rpm` | ZyperOS Installer for Fedora |
| `SHA256SUMS` | The checksums of all of them |

The installer has ZyperOS's version and downloads the newest published
release's image, checked against its `SHA256SUMS`. A draft or a pre-release
(a version with a hyphen, `0.2.0-beta.1`) is never offered. The TrimUI Brick's
image joins once the Mac that makes releases has the Brick's GPU files
(`BRICK_GPU` in plastron).

## Making one

1. Set the version: `tools/set-version.sh 0.2.0` (the Makefile's `VERSION`,
   and the installer's `Cargo.toml` and `Cargo.lock`; `make check-installer`
   says when they disagree). Merge it to main.
2. Write the release notes in `docs/releases/v<version>.md` and merge them.
3. On the Mac, in the main checkout on main, with plastron's checkout on its
   main: `make release` builds the Pixel 2's image from this commit with
   plastron's release build (clean, so it takes a while, holding
   `/tmp/plastron-build-lock`) into `out/release/v<version>/`, and checks it.
   Nothing leaves the Mac. Try the image if you like.
4. `make release PUBLISH=1` builds it again the same way, signs System
   Update's list with the key in `~/.config/zyperos/update-signing-key.pem`
   (`ZYPEROS_UPDATE_KEY`; it refuses without it once `res/update/keys` has a
   key), makes a **draft** release `v<version>` with the image, its sources,
   the update's pack and list and `SHA256SUMS`, and pushes the tag. If the
   ZyperOS folder needs something new from the system image (a library, a
   driver), raise `res/update/min-system` to this version first: devices with
   an older image are then told to use the installer.
5. The tag starts **Release** on GitHub (`.github/workflows/release.yml`): it
   builds the installer for macOS, Windows and Linux with its tests, adds the
   four files to the draft, puts them in `SHA256SUMS`, and publishes the
   release. A tag pushed with no draft waiting is refused.

Why the image is built on the Mac and the installer on GitHub: the image's
inputs are on the Mac (the cores in `vendor/`, which `mk/fetch-vendor.sh` can
no longer fetch exactly as they are, and plastron's build volume), and the
installer builds anywhere, so GitHub's machines build it on all three
systems, the Mac's own included.
