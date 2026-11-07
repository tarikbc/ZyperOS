# Vendored decoders

Two single-file audio decoders for the menu music (src/bgm_dec.c). They are
compiled only through that file, never on their own: the builds take
`src/*.c`, which does not reach into this folder. Copied unchanged.

| File | What | License | Source |
|------|------|---------|--------|
| `minimp3.h` | MP3 decoder | CC0 1.0 (`minimp3-LICENSE.txt`) | https://github.com/lieff/minimp3 at ea99364 |
| `stb_vorbis.c` | Ogg Vorbis decoder, v1.22 | public domain, or MIT (its last lines) | https://github.com/nothings/stb at d14d46f |
