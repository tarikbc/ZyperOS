# Device test: two system slots on the GKD Pixel 2

The test for layer 2 of System Update (docs/updates.md) on real hardware. It
needs me: it is a fresh install on his **32 GB test card**, which erases
it. Never on the ROGUE card, and never on a card someone uses.

QEMU has run the same steps (the ZyperOS and plastron PRs say how they
went). What only the device can show: the Pixel 2's own U-Boot running
`boot.scr` (FAT write on its card, `setexpr`, `env import/export`), the SD
card's partition names (`mmcblk0p2`, `mmcblk0p4`), the first boot's games
partition after root B, and the real start times.

## Before

1. Copy off anything wanted from the test card's games partition (saves in
   `Saves/`, states in `.userdata/`): the fresh install erases the card.
2. A signing key the image trusts. There is no release key yet (I make
   it at the first release), so the test image carries a **throwaway** one:
   `tools/update-key.sh new <scratch>/key.pem` with
   `ZYPEROS_UPDATE_KEYS=res/update/keys`, and the public file is **never
   committed** (`git status` before any commit; delete it after).
3. Build the image from the `ab-slots` branches of ZyperOS and plastron
   (`make` in plastron with `ZYPEROS_SRC` pointing at the ZyperOS worktree;
   `zyperos-dirclean diatom-dirclean all`, so the shared volume builds this
   tree's files, and check the launcher's checksum before writing it).
4. Two test releases from the same build, packed and signed with the
   throwaway key (as `tools/update-check.sh` does):
   - **good**: the image's root with `/etc/zyperos/system` at a higher
     version (unsquashfs, edit, mksquashfs with the image's options), the
     same kernel and device tree, `--kind system`, version e.g. 0.1.1;
   - **bad**: a root that does not mount (a few MB of random bytes as
     `root.squashfs`), version 0.1.2.
   Served over HTTPS from the Mac on the USB link with a throwaway CA, as in
   the layer 1 test (`.userdata/pixel2/update/source`: the URL, then
   `cacert <file>`).
5. Take `/tmp/px2-lock` for each step on the device; the Pixel is shared.

## Steps

| # | Do | Expect |
|---|---|---|
| 1 | Fresh install of the image with the ZyperOS Installer (card in the Mac's reader) | The installer writes it and reads it back |
| 2 | First boot | Boots; `cat /proc/cmdline` has `zyperos.slot=a zyperos.trial=0`; `/proc/partitions` has p1 64 MB, p2 512 MB, p4 512 MB, and p3 (games) over the rest; `sfdisk -d /dev/mmcblk0` shows p2 at 163840, p4 at 1212416, and p3 starting after p4 ends (sector 2260992 or later) |
| 3 | `mount /dev/mmcblk0p1 /mnt && cat /mnt/bootstate.txt; ls /mnt/a; umount /mnt` | `slot=a trial=0 try=0 fell=- a=<version> b=-`; `/a` has `Image` and the DTB; no `/b` |
| 4 | Boot time | `boot: first frame` in the launcher's log within ~100 ms of today's main image (boot.scr reads one small file more and writes nothing on a normal start) |
| 5 | MENU > System Update with the **good** release as source | "System and ZyperOS · N MB", A Download |
| 6 | A Download | Downloading, then Installing (the root written to p4 and read back), then "Installed and checked", A Restart Now. p2 untouched (`sha256sum` of its first squashfs bytes before and after) |
| 7 | A Restart Now | Boots slot b: `zyperos.slot=b zyperos.trial=1`; bootstate `slot=b trial=1 try=1`; `/etc/zyperos/system` says the good version; the image's newer ZyperOS folder went in through the switch (`ZyperOS.prev` kept) |
| 8 | Wait 10 s on the shelf | `zyperos-update confirm` ran: bootstate `trial=0 try=0`; the folder off trial |
| 9 | System Update page | "Updated from …" for the system; Y "Roll Back to <slot a's version>" |
| 10 | Y Roll Back, confirm, Restart Now | Boots slot a (`zyperos.slot=a`); the folder went back with it (`update/result` says rolled-back) |
| 11 | The **bad** release: Download, Restart Now | Slot b does not mount its root: the kernel panics and restarts after 30 s, twice (U-Boot counts `try` 1, 2); the third start is slot a, with `fell=b` in bootstate. About 2 minutes in all |
| 12 | 10 s after that start | `update/result` says `failed 0.1.2`, bootstate `fell=- b=-` (slot b holds no system to roll back to), the status from before the restart gone; the page says it in full, with no Y Roll Back |
| 13 | Installer's **Update** on this card (card in the reader, the same image) | Writes boot and root A only; the games partition and root B untouched (`sha256sum` of p4's first bytes the same); boots slot a |
| 14 | Pull the power during step 6's Installing | The next start boots slot a (bootstate says `b=-` from the moment the write began); the page says Stopped, and A Continue installs the kept download without fetching it again |
| 15 | Pull the power during step 11's first restart (mid U-Boot) | No harm: bootstate is rewritten whole by `fatwrite`; the count goes on |

## After

- Delete the throwaway key's public file from `res/update/keys`.
- The card keeps the A/B layout: it is the layout new installs get.
- Note the start times from step 4 and any step that differs from QEMU in
  the PRs.
