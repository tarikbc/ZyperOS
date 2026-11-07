# System Update

How a ZyperOS device updates itself. Two layers, because the card holds two
different things:

1. **The card's ZyperOS folder** (the launcher, Diatom, the cores, the art,
   `launch.sh`): files on the games partition. Built and shipped here.
2. **The system image** (kernel, root, bootloader): partitions on the card.
   A design waiting for my approval, at the end. Until it lands, a new
   system image goes on with the ZyperOS Installer's Update.

And a third way in for layer 1 (and layer 2 on a card with slots): [over the
USB cable](#over-the-usb-cable), through the File Transfer page's Settings,
with the same signed lists.

## Layer 1: the ZyperOS folder

### What a release carries

Next to the card image, the installers and `SHA256SUMS`, every release from
`make release` has two more files (Pixel 2 only for now):

| File | What it is |
|---|---|
| `ZyperOS-v<ver>-pixel2-update.zpk` | Every file of the image's own ZyperOS folder, one after the other: a gzip member where that is at least 5% smaller, else the bytes as they are. |
| `ZyperOS-pixel2-update.txt` | The list: for each file its path, size, SHA-256, and place and length in the pack. Signed. The name has no version, so `releases/latest/download/` always finds the newest. |

The list (format 1, read by `update/manifest.c`):

```
zyperos-update 1
device pixel2
version 0.2.0
min-system 0.2.0
pack ZyperOS-v0.2.0-pixel2-update.zpk <size> <sha256 of the pack>
dir <path>                                   an empty folder to make
file <sha256> <size> <offset> <stored> <g|r> <path>
end <number of file lines>
sign <key id> <base64 Ed25519 signature>
```

- Text, one record a line, so the device needs no JSON parser and the
  installer, the shell and C read it the same way.
- The signature covers every byte up to and including the `end` line. One
  file holds list and signature, so the two can never be out of step.
- Paths are relative to the ZyperOS folder; `.card/` in front means the
  card's root (`Licenses/`, the empty `Roms/<system>` folders for a new
  system; never a dot folder such as `.media`, which whatever puts the first
  cover there makes), which the switch adds to and never removes from, as the
  image's first copy does.
- `min-system` is the oldest system image the folder runs on
  (`res/update/min-system`). Raise it in the commit that makes the folder
  need something new from the image (a library, a kernel driver); a device
  with an older image is then told to use the installer.
- Unknown keywords before `end` are read past, so a later format can add a
  line. A change an old updater must not read past changes the first line.

### Keys

- Ed25519. The private key is made once on my Mac
  (`tools/update-key.sh new`, into `~/.config/zyperos/update-signing-key.pem`)
  and never goes into a repository, a container or GitHub Actions. Keep a
  copy somewhere safe: without it no device takes another update.
- Its public half goes in `res/update/keys/<key id>.pem`, committed. The key
  id is the first 16 hex digits of the SHA-256 of the raw 32-byte key.
- plastron puts those files in the system image at
  `/etc/zyperos/update-keys/`, where the card cannot change them. A device
  trusts every key there; a new key is trusted only from the first image that
  carries it, so the old key keeps signing until those images are out.
- An image with no key has System Update switched off (the program says
  "use the ZyperOS Installer"). Every image built before the first key is
  such an image.

### On the device

`zyperos-update` (`update/`, shipped in the folder, linked to the image's
libcurl, OpenSSL and zlib) does the work beside the launcher, so a download
goes on when the player leaves the screen. The launcher starts it and reads
its status file (`.userdata/pixel2/update/status`).

**check** reads the list from
`https://github.com/tarikbc/ZyperOS/releases/latest/download/ZyperOS-pixel2-update.txt`
(the folder's `cacert.pem` for TLS), checks the signature against the image's
keys and the device, and compares:

| Result | When |
|---|---|
| `uptodate` | the list's version is not newer than `ZyperOS/VERSION` |
| `needs-system` | `min-system` is newer than the image (`/etc/zyperos/system`) |
| `available` | with the bytes to download: the files whose SHA-256 the installed folder's index (`ZyperOS/.files`) does not have |
| `ready` | that version is already staged and checked |
| `unsupported` | an image without System Update (no `/etc/zyperos/system` or no key) |

**download**, in order:

1. Refuses below 25% battery without a charger (`battery`), and when the card
   has less free space than the new folder needs (`space`).
2. Stages the new folder in `/mnt/SDCARD/ZyperOS.next/`, never touching the
   running one. Every file the installed folder already has (by the index) is
   copied from it, hashed on the way, so only changed files download.
3. Downloads the rest from the pack with HTTP ranges, one connection, the
   members sorted by place and joined into one request across gaps under
   512 KB. The pack's address is the exact release's, never `latest`, so a
   download resumed the next day cannot mix in the next release.
4. Each member is written to `<file>.zpart` as it arrives, then decoded to
   `<file>.tmp` while its SHA-256 is taken, compared with the list, synced,
   and renamed into place; a journal (`ZyperOS.next/.staged`) records it.
5. Cut short (Wi-Fi gone, power off, B and the device off): the next
   download keeps every journaled file and goes on from the byte the cut
   `.zpart` ended at (`stopped`).
6. Every file again, read back off the card (the page cache dropped), against
   the signed list. A file that does not match is fetched again once, then
   the update is refused (`error`).
7. Writes the new folder's index (`.files`), syncs, then writes `.ready`
   with the version, and syncs again (`ready`).

**The switch** happens at the next start, before anything from the card runs:
`/etc/init.d/zyperos` calls `update/apply.sh` (in the image as
`/usr/lib/zyperos/update-apply.sh`) when there is work, a check of a few
files otherwise. It lives in the image because the card's folder is what it
swaps, `launch.sh` included. Every step is a rename, so a power cut at any
point leaves a state the next start finishes:

1. The launcher's own state in its folder (`.last`, `.playing`: where it was,
   what was playing) is copied into the new folder.
2. `ZyperOS.prev/` (the version before the last) goes, `ZyperOS/` becomes
   `ZyperOS.prev/`, `ZyperOS.next/` becomes `ZyperOS/`. `.card/` files are
   added to the card's root.
3. The new folder starts on trial (`ZyperOS/.trial`, a count of starts). The
   launcher deletes the file once its shelf has been up for 10 seconds. A
   version that reaches a third start without that goes back by itself to
   `ZyperOS.prev/`, and `update/result` says `failed <version>`.

**Roll Back** (one step) asks the next start to swap `ZyperOS/` and
`ZyperOS.prev/`, so a step forward is the same again. Refused when the
previous version is older than the image's own ZyperOS, which the init
script would copy straight back over it.

What it costs: the download is the changed files only (a launcher-only
release is a few MB; a release that changes cores, more). The card holds up
to three folders for a moment (running, staged, previous), about 200 MB each
today; at rest, two. Downtime is one restart, about 10 seconds. The check
of the staged folder reads it all back: about 10 to 20 seconds on the Pixel 2
for the cores, shown as "Checking files".

### The release flow

- plastron's `scripts/release.sh` (`make release-pixel2`) packs the image's
  own ZyperOS folder with `tools/update-pack.py`: the pack and the unsigned
  list. The container never sees the key.
- ZyperOS's `tools/release.sh` signs the list on the Mac
  (`tools/update-key.sh sign`), adds both files to `SHA256SUMS` and the draft
  release. `--publish` refuses without the key. The tag's workflow adds the
  installers and publishes, as before.
- A test release is any folder with the two files: `.userdata/pixel2/update/
  source` on the card, a URL on its first line (and `cacert <file>` on a
  second for a server with its own certificate), points the device at it. The
  signature is checked all the same, so a test release needs a list signed by
  a key the image trusts.

### Checks

`make check-update` (in `make check`): two fake releases packed and signed
with a throwaway key, a fake card, and every path above, from `file://`
sources. On the device, `zyperos-update check|download|rollback|status` runs
over SSH too.

## Over the USB cable

For a handheld on the USB cable, which has no internet of its own, through
the File Transfer page's Settings (DESIGN.md 11.15, my pick in the File
Transfer 2 lab, 2026-11-06: "Download, then drop"). The computer only carries
the files: nothing it sends is trusted without the signature and the hashes.

**What a web page can get from GitHub** (checked 2026-11-06): the release
list, `api.github.com/repos/tarikbc/ZyperOS/releases/latest`, answers a page
(`Access-Control-Allow-Origin: *`); the release's files do not.
`github.com/.../releases/download/...` and the host it sends to,
`release-assets.githubusercontent.com`, send no CORS header, so the browser
keeps their bytes from any page; the asset API's redirect goes to the same
host. So:

1. The page asks the handheld what it has (`GET /api/update`: whether updates
   work here and why not, the device, `ZyperOS/VERSION`, the system's version,
   whether the card has two system slots, and what the update program is
   doing), and GitHub which release is newest.
2. When it is newer, the page names the files this handheld needs - the list
   and the pack, and on a card with slots the system's list and pack when the
   release has them - and its button downloads exactly those to the computer
   (each an attachment, so the page stays).
3. They are dropped on the page, or chosen. A browser's "name (1).zpk" for a
   second copy is taken as the name; a file with another name is refused.
4. The page checks the card's room and sends them (`PUT
   /api/update/file?name=...`, a `.part` renamed when whole). The launcher
   takes only this device's release names and keeps them in
   `.userdata/<device>/update/cable/`.
5. `POST /api/update/check`, then `POST /api/update/install`: the launcher
   runs `zyperos-update check` and `download` with `--source
   file://.../cable/`, the same program and the same checks as over Wi-Fi -
   the list's signature against the image's keys, the device, the version,
   every file's SHA-256 - and the page shows the program's own count until
   it says `ready`.
6. "Restart the handheld" (`POST /api/update/restart`): the launcher answers,
   closes File Transfer and restarts as System Update's Restart does; the
   image's switch installs it.

The files in `cable/` go when File Transfer closes, or at the next start,
whenever the update program is not at work. An image with no release key
(every image until the first key) takes no update this way either: `GET
/api/update` says `"on": false` and why, and the page says updates through
the computer are off and to use the ZyperOS Installer.

A fully automatic button (the page getting the files itself) would need the
release flow to copy the update's files to a host that answers a page, such
as ZyperOS's web page, then on GitHub Pages (about 0.5 GB a release on a
1 GB site); the lab showed it, and it was not picked.

`tools/portal-check.py` (`make check-portal`) sends a release's files through
the page to a stand-in for the launcher (`tools/portal-serve.c update=on`),
and `make check-update` installs from a `cable/` folder with the program.

## Layer 2: the system image (two slots)

**Approved by me on 2026-11-05** in the system image lab
(`docs/design/mock/system-update-lab.html`): two full root slots, the
layout on new installs only, the bootloader through the installer only.
Built and tested in QEMU and on the Pixel 2 (`docs/device-test-ab.md`).
**512 MB slots since 2026-11-06**: the root with ZyperOS's payload
(ScummVM, OpenBOR, J2ME, the Saturn core) reached 302 MB in LZ4, past the
first 256 MB. plastron's `post-image-slots.sh` stops a build whose root
does not fit, before genimage, and says how full the slot is on every
build.

### The card

| Area | Where | Size |
|---|---|---|
| Rockchip loader | raw, sector 64 to 16 MB | 16 MB, single (not a slot) |
| p1 `PX2BOOT`, FAT | 16 MB | 64 MB: `boot.scr`, `bootstate.txt`, `a/Image`, `a/<dtb>`, and `b/` once a system update has written it |
| p2 root A, squashfs | 80 MB (sector 163840) | 512 MB |
| p4 root B, squashfs | 592 MB (sector 1212416) | 512 MB, empty on a new card |
| p3 games, exFAT | 1104 MB (sector 2260992) to the end | made at first boot, after root B |

- genimage writes root B third, and plastron's
  `board/zyperos/post-image-slots.sh` moves it to the table's fourth entry,
  so the games partition stays partition 3 everywhere (`datapart`, the
  installer, every script). `datapart` starts it after the last system
  partition, on a 4 MB boundary, with `sfdisk -N 3`.
- The two slots take 1 GB, 768 MB more than the single 256 MB root before
  them (2.4% of a 32 GB card).
- A card keeps the slot size it was laid out with: making the slots bigger
  moves the games partition, which only a fresh install does. So a system
  whose root is bigger than this card's slot is refused by `check` before
  anything is fetched (state `too-big`), and the page says a fresh install
  with the installer is the way. The installer's Update refuses an image
  whose root A is not where and how big the card's is, and says the same.

### Existing cards: new installs only

Moving to this layout moves the games partition on, which a device cannot
do to itself. A card with the old layout (one 256 MB root) keeps it: layer 1
works on it as before, and the update program sees no partition 4 and never
offers a system update. With the 512 MB slots, its root A is too small for
the image's, so the installer's Update refuses it as well and a fresh
install is what moves it on (installer `plan_update`; its tests hold both
layouts). The same goes for a card laid out with the first 256 MB slots.

### Boot

`boot.scr` is built from the board's `boot-prelude.cmd` (the root device,
the device tree's file, the kernel's arguments) and the shared
`board/zyperos/boot-slots.cmd`. It reads `/bootstate.txt` (`slot`, `trial`,
`try`, `fell`, and `a`/`b`, each slot's system version). On `trial=1` it
adds one to `try` and writes the file back with `fatwrite`; when that
makes `try=3` (two trial starts went unconfirmed) it boots the other slot
instead and records `fell=<slot>`. A normal start only
reads. It boots `/<slot>/Image` with `root=<card>2` or `<card>4` and tells
the kernel `zyperos.slot=` and `zyperos.trial=`. A card without
`bootstate.txt` boots slot a.

The launcher, ten seconds after it starts (the same moment it takes a new
ZyperOS folder off trial), runs `zyperos-update confirm` when the kernel
says `zyperos.slot=`: a system on trial is kept (`trial=0`), and a system
U-Boot gave up on is written to `update/result` (`failed <version>`) for the
page, and its slot's version becomes `-`: it holds no system Roll Back may
go to. Either way the status from before the restart (a "ready") goes, so
the page looks again.

### A system update

- A second signed list per release, `ZyperOS-pixel2-system.txt` (`kind
  system`, layer 1's format), with its pack `ZyperOS-v<ver>-pixel2-system.zpk`:
  `root.squashfs`, `boot/Image`, `boot/<dtb>`. plastron's `release.sh` packs
  them; ZyperOS's `release.sh` signs the list with the same key. A list
  whose `kind` is not the one asked for is refused.
- `check` on a card with slots looks at the system's list first: a newer
  system is offered as "System and ZyperOS", since the image carries its own
  ZyperOS folder. A release with no system list, or none newer, leaves the
  folder's check to say what there is.
- `download` stages the three files in `.zyperos-system/` on the games
  partition with layer 1's code (ranges, resume, every file checked). Before
  it writes, bootstate gives the other slot's version as `-` and names the
  running slot to start next (a Roll Back waiting for a restart is off), and
  `ZyperOS.prev` goes if it is the folder of the system being overwritten:
  a cut write leaves nothing to roll back to there. Then it writes the root
  over the other slot's partition and reads it back off the card, copies
  the kernel and device tree into the other slot's folder on p1, and sets
  `slot=<other> trial=1 try=0` and that slot's version. The running slot is
  never written. A staged system whose install stopped is offered again,
  and Download installs it without fetching.
- At the new system's first start, the image's init puts its newer ZyperOS
  folder in through layer 1's switch (staged, `ZyperOS.prev` kept, on
  trial), not by copying over the card's: a system that falls back gets the
  folder that went with it back too.
- **Roll Back** on a card with slots goes to the system in the other slot
  (`rollback`: `slot=<other>`), and the folder that came with the newer
  system goes back with it.
- The loader is never written over the air.

**What it costs** (Pixel 2, measured 2026-11-06 with a 302 MB LZ4 root): a
download of about 280 MB (gzip gains little on LZ4; about a minute on the
USB cable), written to the other slot while the device keeps running (about
30 s of card writes, read back); downtime is one restart (about 10 s); no
space beyond the layout's own slots, plus the staging (about 300 MB) during
the download. A bad system goes back by itself: after two starts that do not
confirm, the third boots the other slot.

### Testing

`make check-update` covers the system path on the Mac with files standing in
for the partitions (`ZYPEROS_UPDATE_PART2/PART4`), a folder for the boot
partition (`ZYPEROS_UPDATE_BOOT`) and a file for `/proc/cmdline`
(`ZYPEROS_UPDATE_CMDLINE`). QEMU boots through U-Boot (plastron's
`scripts/build-qemu-uboot.sh`, `make qemu`), so the VM runs the real
`boot.scr`: `tools/qemu-run.sh` starts it with `-bios u-boot.bin` when the
image folder has one.
