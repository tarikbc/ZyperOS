# NES Player 2 over HTTP: Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A second player for NES games in a browser, on the Pixel 2's hotspot or its USB cable: the game's picture streamed to the page, the page's buttons back as player 2, with a touch pad and the keyboard. Plain HTTP (the design's section 9 path); HTTPS, certificates and the Gamepad API are Plans 2 and 3.

**Architecture:** Diatom copies each frame into a shared memory file and reads player 2's buttons from a word in it (Tasks 1-2). A new process, p2d, serves the play page and one WebSocket: frames out (palette indices, XOR delta, raw deflate), buttons and acks in (Tasks 3-6). The launcher adds a Player 2 row to NES games' in-game menu, starts p2d with the hotspot or the cable, and shows how to join (Tasks 7-8). Task 9 ships it; Task 10 is the device test.

**Tech Stack:** C (gnu11) for Diatom, p2d and the launcher; zlib (already linked by the launcher and the image); plain JavaScript in res/web/play (no framework), `DecompressionStream('deflate-raw')` in the browser; Python 3 tests driving headless Chrome as tools/portal-check.py does.

**Spec:** docs/superpowers/specs/2026-11-12-nes-multiplayer-design.md (approved 2026-11-12). Sections 1-5, 8 (the cable without HTTPS) and 9 (fallback) are this plan; 6, 7 and the HTTPS half of 8 are Plans 2 and 3.

**One deviation from the spec, on purpose:** the spec says LZ4. This plan uses raw deflate (zlib, level 1): zlib is already linked everywhere, and browsers decode it natively, so the page ships no decoder. Task 4's test measures the encode time; if it is over 2 ms a frame on the Pixel, LZ4 replaces it behind the same `p2c_*` interface.

## Global Constraints

- Every new file starts with `/* SPDX-License-Identifier: GPL-3.0-only */` (C) or the same in the file's comment form.
- The NES only: the Player 2 row shows when the game's core is `fceumm`; nothing changes for any other system.
- The Pixel never drops a frame because of player 2: Diatom's added work is a frame copy and a word read; nothing on Diatom's thread waits on p2d.
- The launcher's loop never blocks (src/httpd.h's rule): everything Player 2 adds to on_game_tick is bounded work.
- Player 2 has the NES pad's buttons only: RetroPad B, SELECT, START, UP, DOWN, LEFT, RIGHT, A (mask `0x01FD`, libretro bit ids). Never Diatom's hotkeys, never MENU.
- Every player-facing string is in res/strings with all of the project's languages (en, zh-Hans, es, pt-BR, fr, de, ja, it), drawn in a shot (tools/i18n-shots.py) or listed in tools/i18n-unshot.txt with a reason; `make check-strings` and the fit check pass.
- README and site get the feature in the same work (DESIGN.md section and guide too), in my voice ("I"), light-theme shots.
- No runtime change to usb0, its address or its udhcpd (it has stranded the Pixel before).
- `CHECK_JOBS=3 make check` passes, and `make -C diatom check`.

## Review Focus

1. **p2d dies or is killed mid-game:** player 2's buttons must not stay held. Diatom reads the pad only while p2d's heartbeat moved in the last 500 ms (Task 2 test `pad_dropped_when_beat_stops`).
2. **The guest's phone locks or walks out of range mid-press:** the pad clears when the WebSocket closes, or after 1 s with no pad message while buttons are held (Task 5 test `pad_clears_on_silence`).
3. **A second browser opens the page:** it is told "Player 2 is taken" and the first keeps player 2 (Task 5 test `second_guest_is_taken`).
4. **The game ends any way at all (Quit, a crash, the power button, the battery):** p2d stops, the share file goes, the hotspot stops and the Wi-Fi comes back (Task 8 test `p2_stop_after_any_end`).
5. **A browser without `DecompressionStream('deflate-raw')` (Safari before 16.4):** the page says the browser is too old instead of showing a black canvas (Task 6 test `old_browser_message`).

---

### Task 1: The shared file (src/p2share.h)

**Files:**
- Create: `src/p2share.h`
- Test: `tools/p2share-check.c`, a `check-p2share` target in `Makefile` (added to `CHECKS`)

**Interfaces:**
- Produces, header-only like src/mp4frame.h:

```c
#define P2S_PATH       "/tmp/zyperos-p2"
#define P2S_MAGIC      0x50325348u      /* "P2SH" */
#define P2S_VERSION    1u
#define P2S_SLOTS      3
#define P2S_MAX_W      512
#define P2S_MAX_H      480
#define P2S_HEAD       4096u
#define P2S_SLOT_BYTES ((uint32_t)P2S_MAX_W * P2S_MAX_H * 4u)   /* XRGB8888 */
#define P2S_SIZE       (P2S_HEAD + (uint32_t)P2S_SLOTS * P2S_SLOT_BYTES)
#define P2S_PAD_MASK   0x01FDu          /* B SELECT START UP DOWN LEFT RIGHT A */

typedef struct { uint32_t seq, w, h, frame; } p2s_slot;    /* seq odd while written */
typedef struct {
	uint32_t magic, version;
	uint32_t latest;     /* Diatom: the slot with the newest whole frame */
	uint32_t frame;      /* Diatom: frames published, 0 = none yet */
	uint32_t pad;        /* p2d: player 2's RetroPad bits */
	uint32_t pad_beat;   /* p2d: bumped at least every 100 ms while it runs */
	uint32_t guests;     /* p2d: 1 while a browser is player 2 */
	p2s_slot slot[P2S_SLOTS];
} p2s_head;

static inline uint8_t *p2s_slot_data(void *base, int s);
/* Diatom: copy a w x h frame (fmt: 0 = 0RGB1555, 1 = XRGB8888, 2 = RGB565,
 * libretro's numbering) into the slot after `latest` as XRGB8888, seqlocked */
static inline void p2s_write(void *base, const void *px, unsigned w, unsigned h,
                             size_t pitch, int fmt);
/* p2d: the newest whole frame into dst (w*h*4 bytes at most cap); false when
 * none, when torn twice in a row, or when it does not fit */
static inline bool p2s_read(void *base, uint8_t *dst, size_t cap,
                            unsigned *w, unsigned *h, uint32_t *frame);
```

- [ ] **Step 1: Write the failing test** `tools/p2share-check.c`, on a malloc'd P2S_SIZE block:
  - `write_then_read`: write a 256x240 XRGB8888 pattern; `p2s_read` gives w 256, h 240, frame 1, the same bytes.
  - `rgb565_converts`: an RGB565 frame of 0xF800 reads as 0x00FF0000 (red), 0x07E0 as 0x0000FF00, 0x001F as 0x000000FF.
  - `torn_slot_skipped`: set the latest slot's seq odd; `p2s_read` returns false; even again, true.
  - `three_slots_rotate`: four writes leave `latest` = 0, 1, 2, 0 and `frame` = 4.
  - `too_big_refused`: a 513-wide frame is not written (`frame` unchanged).
- [ ] **Step 2: Run** `make check-p2share`. Expected: FAIL (no header).
- [ ] **Step 3: Implement** `src/p2share.h` as above; the seqlock as mp4frame.h's (seq odd, `__atomic_thread_fence`, data, seq even, then `latest` and `frame`).
- [ ] **Step 4: Run** `make check-p2share`. Expected: `p2share: ok`.
- [ ] **Step 5: Commit** `git commit -m "Player 2: the frames' and the pad's shared file"`

### Task 2: Diatom: SETPLAYER2, the frame copy, port 1

**Files:**
- Create: `diatom/src/player2.c`, `diatom/src/player2.h`
- Modify: `diatom/src/diatom.h` (two message kinds), `diatom/src/proto.c` (parse `PLAYER2`, `SETPLAYER2`), `diatom/src/main.c` (dispatch; off at STOP and at every game end), `diatom/src/env.c` (cb_video, cb_input_state), `diatom/test/stubcore.c` (log port 1 too), `diatom/Makefile` (`check-player2`, in `check`)
- Test: `diatom/test/player2.py`

**Interfaces:**
- Consumes: `src/p2share.h` (include path `../src`).
- Produces:
  - Protocol: `SETPLAYER2\ton=1|0` (launcher to Diatom), answered with `PLAYER2\ton=1|0`; `PLAYER2` asks. Off is the default for every RUN.
  - `bool p2_set(bool on)` (creates P2S_PATH O_RDWR|O_CREAT 0600, ftruncate P2S_SIZE, maps it, writes magic and version last; off unmaps and unlinks), `bool p2_on(void)`, `void p2_frame(const void *data, unsigned w, unsigned h, size_t pitch, int fmt)` (NULL data or a hardware frame: nothing), `uint32_t p2_pad(unsigned now_ms)` (the pad & P2S_PAD_MASK, or 0 when `pad_beat` has not changed for 500 ms).
  - cb_input_state: port 1, RETRO_DEVICE_JOYPAD, `id <= 15` returns bit `id` of `p2_pad()`; `RETRO_DEVICE_ID_JOYPAD_MASK` returns the whole mask; every other port 1 query 0, as today.
  - stubcore's STUBCORE_INPUT_LOG line becomes `"%u %u %u\n"`: frame, port 0 bits, port 1 bits (update test/hotkeys.py's parser for the third column).

- [ ] **Step 1: Write the failing test** `diatom/test/player2.py` (the shape of test/hotkeys.py: build/desktop's diatom and stubcore.so, SDL's dummy drivers, SKIP when unbuilt):
  - `off_by_default`: after RUN, `PLAYER2` answers `on=0` and P2S_PATH does not exist.
  - `frames_arrive`: `SETPLAYER2 on=1`; within 1 s the file has P2S_MAGIC and `frame` grows; the slot's w and h are the stub's geometry.
  - `port1_reads_pad`: write pad = (1<<8) (A) and bump `pad_beat` every 50 ms; the stub's log shows port 1 bits 256 within 10 frames, and port 0 unchanged.
  - `hotkeys_never_through`: pad = 0xFFFF; port 1 bits read 0x01FD.
  - `pad_dropped_when_beat_stops`: stop bumping `pad_beat`; port 1 reads 0 within 0.7 s.
  - `off_cleans_up`: `SETPLAYER2 on=0` removes P2S_PATH; so does STOP with it on.
- [ ] **Step 2: Run** `make -C diatom check-player2`. Expected: FAIL.
- [ ] **Step 3: Implement** player2.c/.h, the two kinds, the parse, the dispatch, the two callbacks and the stub's third column. `p2_frame` is called at the end of cb_video's software path, after the frame is drawn, never before.
- [ ] **Step 4: Run** `make -C diatom check`. Expected: every check passes, check-hotkeys included.
- [ ] **Step 5: Commit** `git commit -m "Diatom: player 2's buttons and the frames, on SETPLAYER2"`

### Task 3: SHA-1 and the WebSocket framing (src/p2d/ws.c)

**Files:**
- Create: `src/p2d/sha1.c`, `src/p2d/sha1.h`, `src/p2d/ws.c`, `src/p2d/ws.h`
- Test: `tools/p2ws-check.c`, target `check-p2ws`

**Interfaces:**
- Produces:
  - `void sha1(const void *data, size_t n, uint8_t out[20])`
  - `bool ws_accept_key(const char *client_key, char out[29])` (RFC 6455 4.2.2: base64(SHA-1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11")))
  - `size_t ws_frame_head(uint8_t *out, int opcode, size_t len)` (server to client, unmasked; returns 2, 4 or 10)
  - `typedef struct { int opcode; size_t len; uint8_t data[125]; } ws_msg;` and `int ws_parse(const uint8_t *in, size_t n, ws_msg *m)`: bytes used, 0 for "need more", -1 for a protocol error (an unmasked client frame, a payload over 125 bytes, a fragmented frame, a reserved bit). Unmasks into `m->data`.
- [ ] **Step 1: Write the failing test** `tools/p2ws-check.c`:
  - `sha1_vectors`: "abc" gives a9993e364706816aba3e25717850c26c9cd0d89d; "" gives da39a3ee5e6b4b0d3255bfef95601890afd80709.
  - `accept_rfc_example`: key "dGhlIHNhbXBsZSBub25jZQ==" gives "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=".
  - `head_sizes`: 125 bytes -> 2-byte head, 126 -> 4, 65536 -> 10, with 0x82 for binary.
  - `parse_masked_binary`: a masked 3-byte binary frame parses to opcode 2, its bytes unmasked; a 2-byte prefix of it returns 0.
  - `parse_refuses`: unmasked, 126-byte, FIN-less and RSV-set frames return -1.
  - `parse_close_and_ping`: opcodes 8 and 9 parse.
- [ ] **Step 2: Run** `make check-p2ws`. Expected: FAIL.
- [ ] **Step 3: Implement** the four functions.
- [ ] **Step 4: Run** `make check-p2ws`. Expected: `p2ws: ok`.
- [ ] **Step 5: Commit** `git commit -m "Player 2: SHA-1 and WebSocket frames"`

### Task 4: The frame codec (src/p2d/codec.c)

**Files:**
- Create: `src/p2d/codec.c`, `src/p2d/codec.h`
- Test: `tools/p2codec-check.c`, target `check-p2codec`; fixtures written by the test into build-native

**Interfaces:**
- Produces the wire format of a frame message, which Task 6's page decodes byte for byte (little-endian):

```
byte 0      'F'
byte 1      flags: bit 0 KEY (no delta), bit 1 PAL (a palette follows)
bytes 2-3   width, u16
bytes 4-5   height, u16
bytes 6-9   frame number, u32
[PAL]       u16 count (1..256), then count x 3 bytes R, G, B
rest        raw deflate (zlib level 1, windowBits -15) of width x height
            bytes: palette indices, XORed with the previous frame's unless KEY
```

  - `typedef struct p2c_enc p2c_enc;` `p2c_enc *p2c_new(void);` `void p2c_free(p2c_enc *);` `void p2c_reset(p2c_enc *)` (the next frame is KEY with PAL).
  - `long p2c_encode(p2c_enc *e, const uint8_t *xrgb, unsigned w, unsigned h, uint32_t frame, uint8_t *out, size_t cap)`: the message's length, or -1 when the frame has more than 256 colors or `cap` is too small. KEY when reset, when the size changed, when a new color was added to the palette (then PAL too), and every 120 frames. Indices are stable: a color keeps its index until a reset.
  - `typedef struct p2c_dec p2c_dec;` `p2c_dec *p2c_dec_new(void);` `void p2c_dec_free(p2c_dec *);` `long p2c_decode(p2c_dec *d, const uint8_t *msg, size_t n, uint8_t *xrgb_out, unsigned *w, unsigned *h)`: the frame number, or -1 for a bad message; the test's mirror of the page's decoder.
- [ ] **Step 1: Write the failing test** `tools/p2codec-check.c`:
  - `round_trip_100_frames`: 100 synthetic 256x240 frames of 40 colors, a moving box; every decoded frame equals its source.
  - `first_is_key_with_palette`: flags of message 1 = 3.
  - `still_frame_is_small`: the second of two identical frames encodes under 200 bytes.
  - `new_color_sends_palette`: a frame adding a 41st color has PAL and KEY, and decodes.
  - `too_many_colors`: a frame of 300 colors returns -1.
  - `size_change_is_key`: 256x240 then 256x224: the second is KEY and decodes at 256x224.
  - `encode_time`: prints the mean encode time of the 100 frames (`p2codec: 0.42 ms a frame`), for Task 10 to compare on the Pixel.
  - Writes `build-native/p2codec-fixture.bin` (the 100 messages, each prefixed by its u32 length) and `build-native/p2codec-fixture.rgba` (the 100 decoded frames), for Task 6.
- [ ] **Step 2: Run** `make check-p2codec`. Expected: FAIL.
- [ ] **Step 3: Implement** codec.c with zlib's deflate (`deflateInit2(..., 1, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY)`, `deflateReset` per frame).
- [ ] **Step 4: Run** `make check-p2codec`. Expected: `p2codec: ok` and the time line.
- [ ] **Step 5: Commit** `git commit -m "Player 2: the frame codec"`

### Task 5: p2d, the server

**Files:**
- Create: `src/p2d/main.c`, `src/p2d/guest.c`, `src/p2d/guest.h`
- Modify: `src/httpd.c`, `src/httpd.h` (hand a connection over after a 101), `Makefile` (`build-native/p2d`, `check-p2d`), `mk/cross.mk` (`$(BUILD)/p2d`, `-lz`)
- Test: `tools/p2d-check.py`, `tools/p2d-feed.c` (writes frames into a share file, as Diatom would)

**Interfaces:**
- Consumes: Task 1's share, Task 3's `ws_*`, Task 4's `p2c_*`, src/captive.c's `captive_dns_answer(q, n, ip, out, cap)` and `captive_host_is(host, addr)`, src/httpd.c.
- Produces:
  - httpd: `typedef void (*httpd_taken)(int fd, void *ctx);` and `void httpd_reply_upgrade(httpd_req *r, const char *extra_headers, httpd_taken fn, void *ctx)`: writes `HTTP/1.1 101 Switching Protocols` with the headers, and once written calls `fn` with the socket, which httpd then forgets (no close, no timeout).
  - The command line: `p2d --root DIR --share PATH --addr IP [--port N] [--dns]` (`--port` defaults to 80; `--dns` answers UDP 53 on `--addr` as the hotspot's DNS, with captive.c's rule).
  - Routes: `GET /` 302 to `/play/`; `/play/` and its files from `DIR/play` with `httpd_reply_static`; `GET /p2ws` the WebSocket; any request whose Host is not `--addr` (or `zyperos.local`) 302 to `http://<addr>/play/` (the old captive check).
  - The WebSocket: the first guest is player 2 and gets status `S{"state":"player2"}` (a text message, opcode 1); a second gets `S{"state":"taken"}` and is closed. Client messages: `'A'` + u32 frame (ack), `'P'` + u16 mask. A frame goes only when sent minus acked is under 2; the first frame after a connect is KEY. On close, or 1 s with no `'P'` while the pad is not 0, the pad is set to 0. When the share's file goes (the game ended), every guest gets `S{"state":"ended"}` and p2d exits 0.
  - The share: `pad = mask & P2S_PAD_MASK`, `pad_beat++` every 100 ms or sooner, `guests` 0 or 1.
  - Runs at `nice(5)`; SIGTERM: closes the guests with `ended` and exits 0.
- [ ] **Step 1: Write the failing test** `tools/p2d-check.py` (starts build-native/p2d on 127.0.0.1 and a free port, a share file in a temp dir fed by build-native/p2d-feed at 60 fps; a minimal WebSocket client in the test itself, about 40 lines of socket code):
  - `serves_the_page`: GET /play/ is 200 text/html; GET / is 302 to /play/.
  - `captive_redirect`: GET / with `Host: captive.apple.com` is 302 to `http://127.0.0.1:<port>/play/`.
  - `frames_flow_with_acks`: the guest gets `S{"state":"player2"}`, then a KEY frame; acking each, 30 frames arrive in 1 s; not acking, no more than 2.
  - `pad_reaches_share`: `'P'` with 0x0101 sets the share's pad to 0x0101 and `guests` to 1; 0xFFFF sets 0x01FD.
  - `pad_clears_on_silence`: after `'P'` 0x0100 and 1.2 s of nothing, pad is 0; on close, pad 0 and guests 0.
  - `second_guest_is_taken`: a second client gets `taken` and is closed; the first still gets frames.
  - `ended_when_share_goes`: remove the share file; the guest gets `ended` and p2d exits 0 within 1 s.
  - `dns_answers`: with `--dns` on a test port, an A query for `example.com` answers the `--addr`.
- [ ] **Step 2: Run** `make check-p2d`. Expected: FAIL.
- [ ] **Step 3: Implement** httpd_reply_upgrade, guest.c (one guest's socket, its ws_parse buffer, its send queue of at most one frame: a newer frame replaces an unsent one) and main.c (the loop: httpd_poll, the guests, the share, the DNS socket, a 2 ms sleep when nothing moved).
- [ ] **Step 4: Run** `make check-p2d` and `make check-httpd`. Expected: both ok.
- [ ] **Step 5: Commit** `git commit -m "p2d: the play page, the WebSocket and the pad"`

### Task 6: The play page (res/web/play/)

**Files:**
- Create: `res/web/play/index.html`, `res/web/play/play.js`, `res/web/play/play.css`; strings in `res/strings/play.txt` (the web strings, written to res/web/i18n by tools/i18n.py)
- Test: `tools/play-check.py` (headless Chrome over DevTools, as tools/portal-check.py), target `check-play`

**Interfaces:**
- Consumes: Task 4's wire format, Task 5's routes and messages.
- Produces the page: a `<canvas id="screen">` at the frame's size, CSS-scaled by whole pixels to fit (`image-rendering: pixelated`), over an NES pad for touch: d-pad left, B and A right, SELECT and START between; multi-touch with Pointer Events; `navigator.vibrate(10)` on press where it exists. The keyboard: arrows, Z = B, X = A, Shift = SELECT, Enter = START. The mask sent on every change and every 100 ms while not 0. An ack after each frame is drawn. A "Picture off" toggle (no drawing, the pad full screen; frames still acked). Status from `S` messages: "Connecting", "Player 2", "Player 2 is taken", "The game ended", and on a lost socket a retry every 2 s. The page's words from res/web/i18n in the handheld's language, as the File Transfer page does.
- [ ] **Step 1: Write the failing test** `tools/play-check.py` against build-native/p2d and p2d-feed replaying `build-native/p2codec-fixture.bin`'s frames:
  - `draws_the_frames`: after 1 s, the canvas's pixels equal the matching frame of `p2codec-fixture.rgba` (read with `getImageData`, compared by frame number exposed as `window.p2.frame`).
  - `keyboard_to_pad`: holding X and Right sets the share's pad to 0x0180; releasing, 0.
  - `touch_to_pad`: a pointer down on the A button's center sets bit 8; two pointers (A and Left) set 0x0140.
  - `taken_message`: a second tab shows the "Player 2 is taken" string.
  - `ended_message`: removing the share shows "The game ended".
  - `old_browser_message`: with `DecompressionStream` deleted before the page's script runs, the page shows the "too old" string and opens no socket.
  - `picture_off`: toggled, no canvas updates for 1 s and acks continue.
- [ ] **Step 2: Run** `make check-play`. Expected: FAIL.
- [ ] **Step 3: Implement** the three files and the strings (every language).
- [ ] **Step 4: Run** `make check-play check-strings`. Expected: both ok.
- [ ] **Step 5: Commit** `git commit -m "Player 2: the play page"`

### Task 7: The hotspot for a game (src/hotspot.c)

**Files:**
- Modify: `src/hotspot.c`, `src/hotspot.h`
- Test: extend `tools/captive-check.c` only if the DNS rule moves; otherwise the change is held by Task 8's check

**Interfaces:**
- Produces: `bool hotspot_start_dns(bool own_dns)`: `hotspot_start()` becomes `hotspot_start_dns(true)`; with false, the launcher binds no UDP 53 (p2d's `--dns` does) and everything else is the same (hostapd, udhcpd, the supplicant stopped and brought back by `hotspot_stop`). `hotspot_poll` stays bounded and safe to call from on_game_tick.
- [ ] **Step 1:** Add `hotspot_start_dns`, keep `hotspot_start` as the File Transfer call.
- [ ] **Step 2: Run** `make check-captive check-portal`. Expected: ok, File Transfer unchanged.
- [ ] **Step 3: Commit** `git commit -m "Hotspot: started without its own DNS, for p2d"`

### Task 8: The launcher: the Player 2 row and page

**Files:**
- Create: `src/p2.c`, `src/p2.h` (the launcher's side, no SDL), `res/strings/player2.txt`
- Modify: `src/game_menu.h` (`GM_PLAYER2` after `GM_PAK`; `gm_ui.player2`, `gm_ui.player2_value`), `src/game_menu.c` (the row after Shader's group when `u->player2`), `src/main.c` (the row's handler, the page, `p2_tick` from on_game_tick, `p2_stop` at every game end), `tools/menu-check.c`, `tools/i18n-shots.py` (shots), `tools/readme-shots.py` if the README shows it
- Test: `tools/p2-check.c`, target `check-p2`; menu-check's new cases

**Interfaces:**
- Consumes: Task 2's protocol through `plat_resident_line`, Task 5's command line, Task 7's `hotspot_start_dns`, `usblink_for_transfer` / `usblink_after_transfer`, `mdns_start` / `mdns_poll` / `mdns_stop`, `qr_encode`, `notice_queue`.
- Produces:
  - `typedef enum { P2_OFF, P2_WAITING, P2_CONNECTED, P2_FAILED } p2_state;` `typedef enum { P2_HOTSPOT, P2_CABLE, P2_NO_LINK } p2_link;`
  - `bool p2_offered(const char *core)` (true for `fceumm`), `p2_link p2_link_now(bool adapter_in, bool computer_on_cable)` (the cable when a computer is on it, else the hotspot when an adapter is in, else none), `bool p2_start(p2_link l)`, `void p2_tick(unsigned now_ms)`, `void p2_stop(void)` (idempotent), `p2_state p2_state_now(void)`, `const char *p2_join_url(void)` (`http://10.42.1.1/play/` or `http://zyperos.local/play/`).
  - Start: `SETPLAYER2\ton=1`, then p2d (`--root <the launcher's res/web on the card> --share /tmp/zyperos-p2 --addr 10.42.1.1 --dns` on the hotspot, `--addr 10.42.0.1` on the cable), then `hotspot_start_dns(false)` or `usblink_for_transfer()` and mDNS. Stop, in reverse: p2d SIGTERM (SIGKILL after 1 s), `SETPLAYER2\ton=0`, `hotspot_stop()` or `usblink_after_transfer()`.
  - The page (a panel over the paused game): on the hotspot the Wi-Fi QR code (`captive_wifi_qr`'s text, as File Transfer draws it), the network's name and password, and "scan, and the game opens"; on the cable "open zyperos.local/play on the computer"; with neither, "plug in a Wi-Fi adapter or a computer"; the state; A starts or stops, B closes the page and Continue goes back to the game with Player 2 still on.
  - The row's value: "Off", "Waiting", "Connected". Notices "Player 2 joined" and "Player 2 left" from `p2_tick` when `guests` changes.
- [ ] **Step 1: Write the failing tests**
  - `tools/p2-check.c`: `offered_only_on_nes` (`fceumm` true; `snes9x2010`, `mgba` false); `link_choice` (cable wins over adapter; neither is P2_NO_LINK); `join_urls`; `p2_stop_after_any_end` (with a fake spawner and a fake resident line: start then stop sends `SETPLAYER2 on=1`, then SIGTERM, `on=0`, hotspot stop, in that order; a second stop does nothing).
  - `tools/menu-check.c`: `player2_row_on_nes_only` (gm_row_ids has GM_PLAYER2 with `u.player2`, after Shader's group, and not without), its three values.
- [ ] **Step 2: Run** `make check-p2 check-menus`. Expected: FAIL.
- [ ] **Step 3: Implement** p2.c, the row, the page, the strings (every language), and the shots `player2-hotspot`, `player2-cable`, `player2-connected` (a host shot flag `--player2 hotspot|cable|connected`).
- [ ] **Step 4: Run** `CHECK_JOBS=3 make check`. Expected: all pass, check-i18n-shots included.
- [ ] **Step 5: Commit** `git commit -m "Player 2 in NES games' in-game menu"`

### Task 9: Ship it: the build, the card, the docs

**Files:**
- Modify: `plastron/buildroot/package/zyperos/zyperos.mk` (`$(ZYPEROS_BUILD_DIR)/p2d` with zyperos.elf and muse, and `P2D_ELF` to the payload), `mk/payload.sh` (`P2D_ELF`, copied to `$P/p2d`, `chmod +x`; res/web/play goes with res/web), `DESIGN.md` (a new 11.x section: Player 2), `docs/guide.md`, `README.md` (feature line, how to, light-theme shots), `site/index.html` (a feature card and the comparison table's row)
- Test: `sh mk/payload.sh` into a temp dir (with `P2D_ELF` set to build-native's p2d) lists `p2d`, executable, and `web/play/`

- [ ] **Step 1:** Add p2d to the device build and the payload.
- [ ] **Step 2: Run** `sh mk/payload.sh` into a temp dir. Expected: p2d and web/play present, p2d executable.
- [ ] **Step 3:** Write the docs, in my voice; the README and site say plainly that a game controller on the phone comes with a later update (Plan 3).
- [ ] **Step 4: Run** `CHECK_JOBS=3 make check`. Expected: all pass.
- [ ] **Step 5: Commit** `git commit -m "Player 2: on the card, and in the docs"`

### Task 10: On the Pixel (by me)

- [ ] **Step 1:** Build and push to the inactive slot (`make push`, with my OK for the card), or px2-deploy for the ZyperOS folder alone.
- [ ] **Step 2:** On the hotspot: a phone joins by the QR code; the play page opens in its sign-in sheet; Contra's first stage, two players, five minutes.
- [ ] **Step 3:** On the cable: a Mac opens zyperos.local/play; the keyboard is player 2.
- [ ] **Step 4:** px2-perf on the same stage with Player 2 on and off: the frame drops must match. p2d's CPU (`top`) and Task 4's encode time on the Pixel go in the PR.
- [ ] **Step 5:** Quit, power off and pull the adapter mid-game: no stuck button, the hotspot gone, the Wi-Fi back.
