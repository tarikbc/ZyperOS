# NES multiplayer: a second player in a browser

## Intent

I want a friend to play the second controller of an NES game on my Pixel 2
with nothing but their phone or laptop: no app, no account, no second
handheld. The Pixel hosts it. My friend joins the Pixel's Wi-Fi (or plugs a
laptop into its USB cable), a page opens by itself, and that page shows the
game and is player 2's controller: buttons on the touch screen, or a real
controller paired with their phone, through the browser's Gamepad API.

The NES first, because its games are where two players on one screen
matter most (Contra, Double Dragon II, Bomberman, Tecmo Bowl) and because
its picture is small. The parts are meant to carry later systems with
2D cores (SNES, Genesis, PC Engine) without a redesign.

## Goals

- **One handheld, two players.** Player 1 is the Pixel's own controls, as
  always. Player 2 is the browser.
- **The page opens by itself on the hotspot.** A phone that joins the
  Pixel's network gets the game page without typing an address: the
  modern captive portal API (RFC 8910 and RFC 8908) where the phone uses it,
  the older internet-check answer where it does not.
- **A real controller works.** A pad paired with the phone or the laptop
  drives player 2 through the Gamepad API. That API, the captive portal API
  and Android 12's full Chrome tab for a portal all need a page served
  over HTTPS with a certificate the phone trusts, so the Pixel serves one
  (Certificates, below).
- **The picture on the guest's screen** is the game's own, lossless, at up
  to 60 frames a second, and never more than a few frames behind the
  Pixel's.
- **The Pixel does not slow down.** With player 2 connected, the NES still
  runs at full speed with no added frame drops, measured with px2-perf.
- **USB too.** A laptop on the Pixel's USB cable (its CDC-NCM link at
  10.42.0.1) can be player 2 the same way, with the fallback in section 8
  where its network blocks the name.

## Non-goals (for this version)

- Sound on the guest's device. The Pixel's speaker plays the game, and the
  guest is next to it.
- Play over the internet. The guest is on the Pixel's own hotspot or cable.
- More than two players (the NES Four Score), watchers, or two guests.
- Systems other than the NES. The design keeps them possible; it does not
  ship them.
- GPU cores (N64, Dreamcast, PSP): their frames are on the GPU.
- The guest touching the Pixel's menus, saves or settings. Player 2 has the
  NES pad's buttons and nothing else.

## Anti-goals

- **A dropped frame on the Pixel because of player 2.** Streaming waits for
  the game, never the other way round.
- **An app to install, or an account.**
- **A shared certificate key in the firmware.** A key everyone can extract is
  revoked, and then every Pixel breaks at once.
- **The Pixel phoning home when multiplayer is not used.** It contacts the
  certificate service only to get or renew its own certificate, and says so.

## What exists today

- **The hotspot** (src/hotspot.c, src/captive.c): with a USB Wi-Fi adapter
  in, File Transfer turns it into an access point (hostapd, WPA2 with a
  fresh password, a QR code to join), udhcpd gives each phone an address in
  10.42.1.0/24, and the launcher answers every DNS name with 10.42.1.1, so a
  phone's internet check reaches the Pixel and the phone opens the page in
  its sign-in sheet. It runs only while File Transfer's screen is open, and
  the Wi-Fi the player had comes back after.
- **The USB link** (plastron's S40usbgadget, src/usblink.c): CDC-NCM, the
  Pixel at 10.42.0.1/30, zyperos.local by mDNS.
- **The web server** (src/httpd.c): one thread, non-blocking, in the
  launcher. It has no TLS and no WebSocket, on purpose: its comment says it
  should not grow into a general web server.
- **Diatom** reads player 1 only: cb_input_state in diatom/src/env.c returns
  nothing for any port but 0. It has no export of a game's frames, but MP4
  already shares frames with the launcher through a file in /tmp mapped by
  both (src/mp4frame.h, a few slots and a sequence number).
- **The launcher during a game** runs on_game_tick (src/main.c), which already
  keeps Developer Mode's SSH and the notices going over a game.
- **TLS on the image:** OpenSSL (with libcurl). LZ4 is in the tree too.
- **The NES core** is FCEUmm, a software core: a 256 x 240 picture in
  XRGB8888, in memory, every frame.

## Approaches

**How the picture gets to the guest.**

1. **Stream the picture (picked).** Diatom copies each frame into shared
   memory; a server sends the frames to the browser over a WebSocket, each
   as what changed since the last one, LZ4-compressed; the browser draws it
   on a canvas and sends the buttons back. The guest needs only a browser,
   no ROM leaves the Pixel, and any software core can use it later. It costs
   a few Mbps and puts the guest's picture a frame or two behind the Pixel's.
2. **Run the game in the guest's browser too, kept in step.** The browser
   runs a WebAssembly build of the same core, and only buttons travel. Very
   little network, but the ROM has to go to the guest's browser, and the two
   cores must agree frame for frame forever (rollback, save states, the same
   commit of FCEUmm, the same settings). Too much to keep right.
3. **WebRTC with the hardware H.264 encoder.** Lossy for pixel art, and a
   DTLS, SRTP and ICE stack on the Pixel for a 256 x 240 picture. Too much.

**How the page is served.**

1. **Plain HTTP.** Works with what exists, but no Gamepad API in Chrome
   (86 and later) or Firefox (91 and later), and no captive portal API on
   either phone: both want HTTPS with a valid certificate for it.
2. **HTTPS with a certificate per Pixel (picked).** Each Pixel gets its own
   Let's Encrypt certificate for its own names, through a service on my
   Cloudflare account, the first time it is online. This turns on the
   Gamepad API, the captive portal API and Android's full Chrome tab for the
   portal. It costs a small cloud service, renewal while the Pixel is
   online, and a TLS server on the Pixel.
3. **A self-signed certificate.** Phones refuse it for the captive portal
   API, and every browser warns.

I picked the stream and HTTPS on 2026-11-12.

## The design

### The parts

```
 Pixel 2                                                    guest's browser
 ┌───────────────────────────────────────────────────┐
 │ Diatom (the game)                                 │
 │   cb_video ──copy──▶ /dev/shm frame slots ──┐     │
 │   port 1 input ◀── /dev/shm pad word ◀──┐   │     │
 │                                         │   ▼     │       HTTPS + WebSocket
 │ p2d (new process, its own core)  ───────┴── read ─┼────▶ canvas, controller
 │   TLS (OpenSSL), the page, the WebSocket,         │◀──── buttons
 │   the captive portal API, the DNS answers         │
 │                                                   │
 │ launcher: in-game menu ▶ Player 2 page, starts    │
 │   and stops p2d, the hotspot, shows the QR codes  │
 └───────────────────────────────────────────────────┘
        │ once, and to renew
        ▼
 certificate service (Cloudflare Worker, my account): ACME DNS-01
```

**1. Diatom.** Two small additions, both off unless the launcher asks for
them while the game runs (a proto command, since the Player 2 row is in the
in-game menu, after RUN):

- In cb_video, after the frame is drawn, copy it into the next of a few
  slots in a shared memory file and bump its sequence number. One memcpy of
  about 240 KB (XRGB8888); nothing else on Diatom's thread. Format, size, pitch and
  the frame number go in the slot's header, as mp4frame.h does it.
- cb_input_state answers port 1 with the joypad mask in a shared word that
  p2d writes. No syscall per frame. Only the NES pad's buttons (d-pad, A, B,
  SELECT, START), never Diatom's own hotkeys.

**2. p2d, the player-2 server.** A new small C process, started by the
launcher when player 2 is invited and stopped when the game ends or the
host turns it off. Its own process keeps TLS and the network off both the
game's thread and the launcher's frame loop. It:

- serves the play page (res/web/play/) and its files over HTTPS on 443,
  with the Pixel's certificate, and over HTTP on 80 only to redirect (or
  to serve the whole page in the fallback, below);
- runs the WebSocket for the picture and the buttons;
- answers the captive portal API (RFC 8908) as `application/captive+json`;
- answers the older internet checks (Apple's, Android's, Windows') with a
  redirect to the play page, for phones that do not use the API;
- answers DNS on the hotspot while it runs: the Pixel's own names with
  their address, every other name with the Pixel too (captive.c's rule).
  (The launcher answers DNS for File Transfer today in its frame loop; one
  owner at a time.)

**3. The launcher.**

- An in-game menu row on NES games, **Player 2**, opens a page: how to join
  (the Wi-Fi QR code and the network's name and password, or "plug a
  laptop into the cable"), then who is connected, with **Stop**.
- Starts the hotspot for the length of the game when an adapter is in and
  no computer is on the cable; on the cable, uses the USB link as it is.
- Starts p2d and tells Diatom to start the frame copy and port 1. On the
  cable, binds the USB link for the game, as USB File Transfer does (on a
  release image it is off during a game unless Developer Mode is on). On quit, both stop, the hotspot
  stops, and the Wi-Fi the player had comes back (as File Transfer does).
- A notice when player 2 joins and when they leave.

**4. The play page (res/web/play/).** Plain JavaScript, no framework, like
res/web:

- A canvas at the NES's 256 x 240, scaled by whole pixels where it fits
  (CSS `image-rendering: pixelated`), above an NES pad drawn for touch:
  d-pad left, B and A right, SELECT and START between. Multi-touch, with a
  short vibration on press where the phone allows it.
- The Gamepad API: the first pad pressed is player 2's, mapped as a
  standard layout, and the touch pad fades away while it is in use.
- The keyboard for laptops: arrows, Z and X for B and A, Shift and Enter for
  SELECT and START.
- Fullscreen and the Screen Wake Lock API (both HTTPS-only), so the phone's
  screen does not sleep during a game.
- A picture-off mode: the guest looks at the Pixel and uses the phone only
  as a pad.
- Status: "Connecting", "Player 2", "The game ended", "Player 2 is taken".

**5. The wire.** One WebSocket, binary messages:

- **Frame (Pixel to browser):** a small header (frame number, width,
  height, format, keyframe or delta) and an LZ4 block. A delta is the frame
  XORed with the previous one the browser has, so unchanged pixels are
  zeros that LZ4 crushes; a keyframe is the frame itself. For the NES the
  XRGB8888 frame can go as one byte a pixel, through the 64-color palette
  the core draws with, for a quarter of the bytes before LZ4.
- **Ack (browser to Pixel):** the last frame number drawn. p2d sends the
  newest frame only when at most one sent frame is not acked, so a slow
  link gets fewer frames, never a growing queue, and Diatom never waits.
- **Pad (browser to Pixel):** the button mask, sent on every change and
  every 100 ms while held, so a lost message heals itself.
- At 60 frames a second the NES should take about 2 to 7 Mbps; the
  RTL8188 adapter's hotspot carries far more.

**6. Setting up: the certificate, once.**

The certificate is opt-in, and asked for only when the owner wants a
controller to work; the Pixel never contacts the certificate service
before that. The first time the owner opens **Player 2** (the in-game
row, or **MENU > Connections > Multiplayer**), three short pages, with
Zyper, as the tutorial's:

1. **Play with a friend.** A friend plays player 2 on their phone or
   laptop, with no app: the touch pad, the keyboard, or their own
   controller.
2. **One-time setup.** "So that phones trust your Pixel and controllers
   work, it needs the internet once." Three ways, the first that fits
   shown first:
   - **With Wi-Fi:** an adapter is in and a saved network has internet: it
     connects, gets the certificate (a progress bar, 10 to 30 seconds), and
     the Wi-Fi goes back as it was.
   - **With a computer:** the Pixel on the cable, the computer on the
     internet. The page at http://zyperos.local does the internet part: its
     script takes the Pixel's request, sends it to the certificate service
     (which allows that one origin), and gives the certificate back to the
     Pixel. For an owner with no Wi-Fi adapter, and the computer needs
     nothing installed.
   - **Later:** play now with the touch pad and the keyboard (section 8),
     and the setup finishes by itself the next time the Pixel is on the
     internet.
3. **Ready.** "Next time, open Player 2 in a game." MENU > Connections >
   Multiplayer shows the state from then on: "Ready, renews by itself", or
   what is missing.

After that, renewal is automatic and silent whenever the Pixel is on a
network with internet.

**The names.** Each Pixel has a random device ID, made on first setup
(never its serial or MAC: the names are public in Certificate Transparency
logs), and one name: `<id>.play.zyperos.com`. On the hotspot the Pixel is
the DNS and answers its own name with 10.42.1.1; for the cable, public DNS
has one wildcard record, `*.play.zyperos.com A 10.42.0.1`, for every Pixel
at once. One name, one certificate (not a wildcard), the same address bar
on both links, and no DNS record per device.

**The certificate service.** A Cloudflare Worker on my account, in this
repository (like discord/'s), with its own deploy:

- The Pixel makes its private key and never sends it. It sends a
  certificate request (CSR) for its name; the Worker runs ACME with Let's
  Encrypt by DNS-01: it writes `_acme-challenge.<id>.play.zyperos.com`
  through Cloudflare's API, waits for the order, deletes the record, and
  returns the certificate. The order is held in a Durable Object, so the
  Pixel can come back for it rather than hold a connection open.
- The first request for an ID registers a secret only that Pixel holds;
  every later one for that ID must carry it. Requests are rate limited per
  address.
- Its Cloudflare API token can edit zyperos.com's DNS (tokens are per
  zone). It lives only in the Worker's secrets, and the Worker writes only
  `_acme-challenge.*.play.zyperos.com` names.

**Scale.** The Worker does a few requests per Pixel every two months, which
is nothing for Workers. The limit is Let's Encrypt's: 50 new certificates
for zyperos.com in any 7 days, refilled at one every 202 minutes (about 7 a
day). Renewals through ACME Renewal Info do not count. So:

- Before release, I ask Let's Encrypt for an override for zyperos.com,
  saying what this is (they grant overrides per registered domain). The
  Public Suffix List is not a way round it: it refuses entries made only
  for Let's Encrypt's limits.
- The Worker keeps a count, and a request over the week's budget waits in
  line ("Your setup finishes by itself; nothing to do") instead of failing.
- A second free ACME CA (Google Trust Services or ZeroSSL) behind the same
  Worker, used only when Let's Encrypt says no.
- Lifetimes: 90 days now and getting shorter (about 45 days by 2028).
  Renewal at a third of the life left, so a Pixel offline for a month
  still renews in time.

**7. Captive portal.**

- udhcpd on the hotspot sends DHCP option 114 with
  `https://<id>.play.zyperos.com/capport`. The Pixel answers that name itself
  (it is the hotspot's DNS), with its certificate.
- The API answers `{"captive": true, "user-portal-url":
  "https://<id>.play.zyperos.com/play"}`. Phones that read it open the play
  page in their sign-in sheet (iOS) or a Custom Tab, which is Chrome itself
  (Android 12 and later with Google Play system updates).
- Phones that do not read option 114 still check the internet; p2d answers
  that check with a redirect to the same page, as File Transfer does now.
- Whether the sheet should stay "captive" while the guest plays (so it stays
  open) or flip to `"captive": false` after the page loads (so the phone
  calls the network joined and may close the sheet) is settled by the spike
  below, on real phones.

**8. The laptop on the cable.** The laptop's DNS is its own, not the
Pixel's: the cable gives no DNS and no router, on purpose, so plugging in
never changes the laptop's internet, and that stays. It finds
`<id>.play.zyperos.com` through public DNS (the wildcard record), as Plex
finds its servers (`*.plex.direct`). Two cases fail, and both get the
HTTP page with the keyboard, never an error:

- **The laptop has no internet:** no public DNS at all.
- **A router blocks public names that point to private addresses** (DNS
  rebinding protection: OpenWrt, Fritz!Box, Pi-hole and some others by
  default). The page says how to fix it once: allow `play.zyperos.com` in
  the router's DNS rebinding settings (Plex asks the same for
  `plex.direct`), or turn the laptop's Wi-Fi off while playing.

The laptop opens http://zyperos.local/play first (mDNS, always works on
the cable). That page tries `https://<id>.play.zyperos.com/ping` from the
browser and, when it answers, moves to the HTTPS page by itself; when it
does not, it stays on HTTP and says why. Nothing on the cable's DHCP
changes, at runtime or in the image.

**9. Without a certificate (fallback).** A Pixel not set up yet, or whose
certificate ran out, serves the same page over plain HTTP: the hotspot's
older captive check opens it, the touch pad and the keyboard work, and the
page says that a controller needs the one-time setup. Nothing else
changes.

### Performance budget

- Diatom's added work: one frame copy and one shared word read a frame,
  well under 0.2 ms on the Pixel.
- p2d: the delta, LZ4 and TLS for 60 frames a second of 256 x 240 should be
  a few percent of one A35 core. It runs at a lower priority than Diatom and
  never on Diatom's core.
- Measured with px2-perf on a busy NES game (Contra's first stage) with
  player 2 connected and the picture on: no frame drops that the same game
  does not have without it.

### Spike first (phase 0)

Before the full build, a throwaway test on a current iPhone and a current
Android phone, with a test certificate:

1. Does each phone read option 114 on first join, open the user-portal-url,
   and keep the network joined with no internet behind it?
2. In iOS's sign-in sheet and in Android's portal tab: do the WebSocket,
   the Gamepad API, fullscreen and the wake lock work, and when does each
   close the sheet (captive true or false, the guest pressing Done)?
3. If the sheet is too limited on iOS: can the page hand over to Safari (a
   link, or a second QR code on the Pixel), and what is the least typing?

The answers decide section 7's last point and the join steps on the Player
2 page. The spike's code is thrown away.

### Testing

- Host checks (make check): the frame codec (delta, LZ4, the browser's
  decoder against the C encoder on recorded frames), the pad mask both
  ways, the captive API's JSON, the DNS answers, the WebSocket framing, the
  certificate state machine (none, requesting, ready, renew, expired).
- The Worker: a test against Let's Encrypt's staging server.
- QEMU (tools/qemu-run.sh): p2d serving a browser on the Mac, with a recorded
  NES game.
- On the Pixel, by me: an iPhone and an Android phone on the hotspot, a Mac
  on the cable, each with touch and with a paired controller; px2-perf with
  player 2 on and off.

## Decided on 2026-11-12

- HTTPS from the start, with a certificate per Pixel.
- Setup is opt-in, from the first Player 2, with Wi-Fi or a computer, or
  later (section 6).
- The names are under a subdomain: `<id>.play.zyperos.com`.
- The cable works as Plex does: public DNS, an automatic HTTPS check, the
  HTTP page with a fix when a router blocks it (section 8).

## Left to the builder

The names (p2d is a working name), the shared memory layout, the WebSocket
code (RFC 6455) and its TLS reads and writes on OpenSSL, the exact message
header, how p2d learns of a new frame (a futex on the shared page, a pipe,
or a short poll), the ACME client in the Worker, the page's look within the
launcher's design tokens, and how the launcher and p2d hand DNS over.
