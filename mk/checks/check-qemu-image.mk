CHECKS += check-qemu-image
#| a release's QEMU image on its way to a developer, offline: `tools/qemu-run.sh fetch` from a
#| stand-in release folder (a release without the image says so; the image goes into image/ with
#| FETCHED once its sha256 matches the release's SHA256SUMS; again, nothing is downloaded; a
#| download cut short goes on; another sha256, any other file in the archive, two images in one
#| SHA256SUMS or another `--version` change nothing; an image/ fetch did not put there stays unless
#| --force), `tools/release.sh` putting the image and its sources tar in the release's files and
#| SHA256SUMS, the sources tar (plastron's `scripts/qemu-sources.sh` on a stand-in legal-info lists
#| px2sim's source, license and package and the QEMU kernel's and U-Boot's configuration, and
#| refuses without px2sim's source; `tools/qemu-sources-check.sh` passes it beside a Pixel 2
#| sources tar with the same packages and names a copyleft package at another version), and the SSH
#| key: start hands QEMU the key's public half as the fw_cfg file the image's S49sshkey reads,
#| making the VMs' own key when there is no other (a stand-in QEMU, macOS only), S49sshkey writes
#| it where `/root/.ssh` points before dropbear starts, and the kernel has fw_cfg in sysfs
