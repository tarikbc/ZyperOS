# LZ4, vendored

Yann Collet's LZ4 block compressor, carried in to pack save states
(`src/statefile.c`).

- Upstream: https://github.com/lz4/lz4
- Version: v1.10.0, commit `ebb370c` (2024-07-22): `lib/lz4.c`, `lib/lz4.h`
  and `lib/LICENSE`, byte for byte
- **BSD 2-Clause licensed** - see `LICENSE`. It sits beside Diatom's own
  license, the Mozilla Public License 2.0, with no copyleft of its own, as
  rcheevos does. Its notice reaches the card in `THIRD-PARTY.md`, which
  plastron puts there as `Licenses/Diatom.md`: the license asks a binary to
  carry it.

## Why it is here

A save state is mostly zeros, and the card is the slow half of writing one: a
PSP state was 40 MB for every quit (PPSSPP pads its size up to the next 8 MB)
and an N64 one 16 MB, 87% of it zeros. Diatom already links zlib, for zipped
games, so zlib was the first thing tried. On one of this Mac's cores, with
real states from a ZyperOS card (2026-11-12):

| state | bytes | zlib level 1 | LZ4 |
|---|---|---|---|
| Mario Kart 64 (Mupen64Plus-Next) | 16.0 MB | 1.18 MB in 42 ms, back in 7 | 1.46 MB in 5 ms, back in 1 |
| Metal Gear Solid (PCSX ReARMed) | 4.25 MB | 1.70 MB in 50 ms, back in 8 | 2.11 MB in 5 ms, back in 1 |
| Mario Kart DS (DSperate) | 6.05 MB | 2.41 MB in 65 ms, back in 10 | 3.00 MB in 6 ms, back in 1 |
| Crazy Taxi 2 (Flycast 2021) | 26.9 MB | 10.5 MB in 271 ms, back in 38 | 12.4 MB in 23 ms, back in 4 |

zlib's files are a fifth smaller and take ten times as long. The Pixel 2's
Cortex-A35 is several times slower than this Mac per core, so zlib's time
would come back as the quit's and the resume's own; LZ4's is a small part of
the write it saves. zstd at its fast levels sits between the two, and is a
library of dozens of files where LZ4's block format is two.

Only the block format is used (`LZ4_compress_default`, `LZ4_decompress_safe`):
no frames, no streaming, no `lz4hc`, no `xxhash`. A state's own header says
how long its block is and how long it unpacks to (`src/statefile.h`).

## Building it

Listed by name in the Makefile, as rcheevos is, and built on its own terms:
`-O3`, as LZ4's own Makefile builds it, the block compressor being the whole
of a state's packing time; without `-Wall -Wextra`, which is Diatom's rule
for its own code. 67 KB of machine code in the desktop build (`size`,
2026-11-12).

## Updating

Replace the three files from a new upstream tag, re-run `make check` (its
check-statefile damages packed states a few thousand ways under
AddressSanitizer), and change the version above. Do not patch anything here -
a local fix becomes invisible at the next update.
