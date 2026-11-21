CHECKS += check-portlibs
#| the libraries PortMaster's ports and their runtimes load from the system (plastron's
#| `board/zyperos/ports-libs.txt`: the LOVE runtime's, Stardew Valley's OpenAL, and what the
#| Westonpack runtime takes beyond its own folder, libevdev and libbz2 among them) asked for in the
#| image of each device whose ZyperOS runs ports (`HAS_PORTS=1` in its `sd/<device>/device.sh`):
#| each option set in its Buildroot config and each package in its package list; then plastron's
#| `scripts/ports-libs.sh`, which its release runs over the built Pixel 2 image, over made-up
#| roots: all there passes, one missing or a link to nothing fails and names it, 1.1.0-rc.6's image
#| without libevdev.so.2 and libbz2.so.1.0 fails on those two (no image built)
