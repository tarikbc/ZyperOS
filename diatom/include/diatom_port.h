/* Diatom port interface.
 *
 * THIS HEADER MUST NOT INCLUDE libretro.h, AND NEITHER MAY ANY PORT.
 * That is the mechanical test for whether the seam holds: a port that needs a
 * core to build cannot be developed on the desktop, which destroys the
 * iteration loop the desktop backend exists to provide.
 *
 * The port deals in pixels, samples, buttons and time. Nothing else. If a
 * function name here grows a domain noun - game, save, core, menu - it is in
 * the wrong layer.
 *
 * Port selection is compile-time: one binary per device, no plugin mechanism.
 * Nobody swaps a device backend at runtime on a handheld.
 */
#ifndef DIATOM_PORT_H
#define DIATOM_PORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The level bar's look (osdbar_look), which the host hands the port to draw:
 * ZyperOS's own header, vendored in port/, for its type and sizes alone. It
 * declares functions only a port with a bar links (port/osdbar.c). */
#include "../port/osdbar.h"

/* Cores may emit 0RGB1555 too; Diatom refuses it. Measured 2026-08-23: all six
 * cores tested then chose RGB565 when offered both. XRGB8888 is in use since:
 * fceumm declares it (src/env.c, diatom_env_pixfmt_begin), and a GPU core's
 * frame is read back in it for previews. */
typedef enum {
	DIATOM_PIX_RGB565,
	DIATOM_PIX_XRGB8888
} diatom_pixfmt;

typedef enum {
	DIATOM_LOG_DEBUG,
	DIATOM_LOG_INFO,
	DIATOM_LOG_WARN,
	DIATOM_LOG_ERROR
} diatom_log_level;

/* How to sample when the destination rect is not an exact integer multiple of
 * the source. Choosing is policy (host); sampling is hardware (port), the same
 * split the port interface applies to the rect itself.
 *
 * SHARP is sharp-bilinear: interpolate only across the one destination pixel
 * that straddles a source-pixel boundary, leaving every interior pixel exact.
 * At an integer factor it is identical to NEAREST by construction.
 *
 * SHIMMERLESS is zadpos's Sharp-Shimmerless (public domain): a box filter.
 * Source and destination pixels are rectangles, and a destination pixel is
 * the mix of the source pixels it covers, by area. A destination pixel inside
 * one source pixel is that pixel exactly; only one straddling a boundary
 * mixes, so a scroll at a fractional factor keeps every column the same
 * width - nearest's columns of 2 and 3 are what shimmer. At an integer factor
 * it is NEAREST too.
 *
 * A port without a way to draw one draws the closest it has (the desktop:
 * SDL's linear); the host never asks which.
 *
 * Passed per frame rather than set once, so the port holds no policy state and
 * cannot be asked to present before it has been told how. */
typedef enum {
	DIATOM_FILTER_NEAREST,
	DIATOM_FILTER_SHARP,
	DIATOM_FILTER_SHIMMERLESS,
	/* Old screens, drawn by a GPU: a CRT's scanlines and phosphors (flat,
	 * and curved), scanlines alone, and a handheld LCD's grid. A port with
	 * no shaders draws them as the closest it has (the Brick:
	 * Sharp-Shimmerless's map), and a host offers them only where they are
	 * drawn (ZyperOS: the Pixel 2). */
	DIATOM_FILTER_CRT,
	DIATOM_FILTER_CRT_CURVED,
	DIATOM_FILTER_SCANLINES,
	DIATOM_FILTER_LCD,
	/* The LCD's grid on a screen with no light behind it, as the Game Boy
	 * Color's and the first Game Boy Advance's were: a light pixel shows
	 * the screen's backing rather than glowing white. Which systems it is
	 * for is the host's to say (ZyperOS: the Game Boy's three). */
	DIATOM_FILTER_LCD_UNLIT,
	DIATOM_FILTER_COUNT
} diatom_filter;

typedef struct { int x, y, w, h; } diatom_rect;

typedef struct {
	/* Logical and ALWAYS landscape. A port whose panel is physically rotated
	 * - the Miniloong's framebuffer is 720x960 portrait - hides that here.
	 * Nothing above the port may learn the panel's true orientation. */
	int  surface_w, surface_h;

	int  audio_rate;            /* what the device actually runs at */
	int  audio_buffer_frames;   /* capacity; DRC needs this, not just queued */
	bool present_blocks;        /* does present() wait for vblank? */
	bool gpu;                   /* can draw a core's GPU frames (diatom_port_gpu_begin) */
	bool rumble;                /* has a motor diatom_port_rumble can run */
	/* The panel's refresh rate from its mode, Hz; 0 where the port does not
	 * know it. A core within 1% of it runs at it, one frame a refresh (the
	 * frame loop's display lock, src/pace.h). */
	double refresh_hz;
} diatom_port_caps;

/* Canonical Diatom buttons. Digital only, no analog axes anywhere.
 * Bit positions within diatom_port_input_state(). Deliberately NOT libretro's
 * RETRO_DEVICE_ID_JOYPAD_*: the near-identity mapping is what keeps libretro.h
 * out of the port. Device quirks - the Brick reporting its front keys as
 * L3/R3, say - are resolved here and never travel upward. */
enum {
	DIATOM_BTN_UP = 0, DIATOM_BTN_DOWN, DIATOM_BTN_LEFT, DIATOM_BTN_RIGHT,
	DIATOM_BTN_A, DIATOM_BTN_B, DIATOM_BTN_X, DIATOM_BTN_Y,
	DIATOM_BTN_L1, DIATOM_BTN_R1, DIATOM_BTN_L2, DIATOM_BTN_R2,
	DIATOM_BTN_SELECT, DIATOM_BTN_START,
	/* Diatom's own key: never forwarded to a core. The port REPORTS it like
	 * any other button and does not act on it - what MENU means is host
	 * policy, and it differs by mode. Standalone it ends the session; under
	 * the launcher protocol it hands the display over for a menu. A port that
	 * decided this itself would make that impossible. */
	DIATOM_BTN_MENU,
	/* The sticks' clicks, on a handheld with sticks (the R36S's BTN_THUMBL
	 * and BTN_THUMBR): the RetroPad's L3 and R3. After MENU, so every
	 * other button keeps its bit. */
	DIATOM_BTN_L3, DIATOM_BTN_R3,
	DIATOM_BTN_COUNT
};
#define DIATOM_BIT(b) (1u << (b))

bool diatom_port_init(diatom_port_caps *out);
void diatom_port_shutdown(void);

/* Called AFTER retro_run returns, never from inside the core's video callback.
 * The host owns `src` and has already computed `dst`; the port blits.
 * src == NULL means "repeat the previous frame" (the core signaled a dupe).
 *
 * `dst` may extend past the surface: a fill or overscale mode deliberately
 * crops. Ports clip; they never refuse the frame.
 *
 * Seven parameters is at the edge of reasonable. If this list grows again it
 * wants a struct, not an eighth argument. */
void diatom_port_present(const void *src, int w, int h, size_t pitch,
                         diatom_pixfmt fmt, diatom_rect dst,
                         diatom_filter filter);

/* Parts of the frame drawn to places of their own: a Nintendo DS frame holds
 * two screens, top over bottom, and a layout puts each somewhere else on the
 * panel (src/screens.h). With planes set, present draws each plane's `src`
 * rect of the frame into its `dst` rect, in order, with the filter it is
 * given, and ignores its own `dst`; n == 0 goes back to drawing the whole
 * frame into `dst`.
 *
 * Geometry, not policy: the host computed every rect, as it computes `dst`,
 * and the port only samples. Held until the next call rather than passed per
 * frame because present's argument list is at its limit (see above) and the
 * planes change with a layout, not with a frame. `src` rects lie inside the
 * frame; `dst` rects may reach past the surface and are clipped like `dst`.
 *
 * Copied, not borrowed. A port that draws only one rect (the Brick, which
 * has no DS here) draws the largest plane and is correct enough to be
 * honest about: one screen, never a garbled two. */
typedef struct { diatom_rect src, dst; } diatom_plane;
#define DIATOM_PLANES_MAX 2
void diatom_port_planes(const diatom_plane *planes, int n);

/* How strongly an old screen's filter (CRT, CRT_CURVED, SCANLINES, LCD) is
 * drawn, in percent: 100 is the filter as it is, and less mixes its lines and
 * grille toward the plain picture under them, 0 being that picture alone.
 * The other filters have no effect to weaken and ignore it.
 *
 * Held until the next call, as the planes are, and for their reason: present's
 * list is at its limit, and this changes with a menu row, not with a frame.
 * The value in force when a frame is presented is the one it is drawn with.
 * Clamped to 0..100; 100 until told otherwise. A port that draws no old
 * screens (the Brick, the desktop) ignores it. */
void diatom_port_strength(int percent);

/* An image composited over the presented frame for a while, in SCREEN space -
 * the same job the port already does for its own level bar, which is why it
 * lives here rather than in the host: the OSD is drawn after scaling, and a
 * host that wanted to draw it would have to copy and scale the frame itself.
 *
 * Pixels and a duration. The port is not told what the image means, and it
 * must not care: `overlay` is a pixel word, and a parameter named for an
 * achievement or a message would be this header's own rule being broken.
 *
 * `bgra` is 8-bit BGRA, straight (not premultiplied), `w * 4` bytes per row.
 * THE POINTER IS BORROWED, not copied: the host guarantees it stays valid and
 * unchanged until the next call or until the duration elapses. That keeps the
 * buffer in one place instead of one per port, which matters on a device where
 * a notice is 150KB and the whole process is meant to hold 8MB.
 *
 * In ZyperOS's 1024-unit screen, as the port's level bar is drawn: a port
 * whose surface is narrower scales it by the same factor (the Pixel 2's 640,
 * 0.625), and one whose surface is that screen (the Brick's) draws it as it
 * is.
 *
 * ms == 0, or bgra == NULL, clears it. An image larger than the surface, so
 * scaled, is refused rather than clipped: something drawn half off the
 * screen is a bug that looks like a design choice. */
void diatom_port_overlay(const uint8_t *bgra, int w, int h, unsigned ms);

/* The Border: a picture drawn under the game, filling the surface where the
 * game's picture does not (src/border.h). Stretched to the whole surface,
 * smoothly (a small one, the blur's 32x24, is soft by being stretched),
 * and opaque whatever its alpha: under the game and under every overlay,
 * which stay on top. 8-bit BGRA, `w * 4` bytes a row.
 *
 * COPIED, unlike the overlay: it stays for the whole game, and a port that
 * draws on the CPU keeps its own copy at the surface's size anyway. Uploaded
 * or painted once a call, so a picture held costs nothing a frame but its
 * pixels drawn where black was. Held until the next call; NULL clears it to
 * black, as before any. */
void diatom_port_border(const uint8_t *bgra, int w, int h);

/* Stop presenting, without tearing down. Returns when nothing is pending and
 * nothing is in flight, so a SECOND presenter may take the display safely.
 * The port stays initialized and must serve the next diatom_port_present.
 *
 * This exists because presenting lifetime and process lifetime came apart.
 * init/shutdown bracket the process and present() is per frame; nothing
 * bracketed one GAME's presenting, which was fine while a frontend ran one
 * game and exited. Then the process became long-lived and the display was
 * handed to the launcher per game, and the seam was never grown to
 * match - so `EXIT` and `PAUSED` were sent while this port was still panning,
 * and for a few milliseconds two processes drove the same framebuffer.
 * Measured from the launcher side as a boundary sweeping down the panel
 * across several frames: a tear, not a composite.
 *
 * `park` says what to LEAVE on glass, because the port cannot know and the
 * answer differs by handover:
 *
 *   KEEP  - the last frame. Right for a pause, where the launcher draws its
 *           menu over the frame the player stopped on and wants continuity.
 *   BLANK - opaque black. Right for an exit, where the launcher's first act
 *           is to fade the shelf up from black, so black is the state it is
 *           about to assume anyway.
 *
 * Getting that backwards is visible. Parking KEEP at exit replaces whatever
 * was on screen - the launcher's own menu, if the player quit from it - with
 * a bare game frame for the few tens of milliseconds before the fade starts,
 * which reads as a flash. It is not a tear: it is the wrong picture, shown
 * cleanly.
 *
 * Not diatom_port_shutdown - that joins the flip thread, and the thread has
 * to survive to serve the next game. */
typedef enum {
	DIATOM_PARK_KEEP,
	DIATOM_PARK_BLANK
} diatom_park;

void diatom_port_present_stop(diatom_park park);

/* Frames drawn by the GPU: a core that renders with OpenGL ES draws into a
 * framebuffer the port owns instead of handing over pixels. Pixels would mean
 * reading every frame back from the GPU and uploading it again, which on the
 * Pixel 2's Mali-G31 costs more than the frame; a Dreamcast is not playable
 * any other way here.
 *
 * Still pixels and time, not cores: the port hands out a GL context, a
 * framebuffer name and function pointers, and libretro's half of the bargain
 * (SET_HW_RENDER, context_reset) is the host's. A port with no GPU of its own
 * returns false from begin, and the host refuses such a core cleanly.
 *
 * Every call but present_gpu's queueing happens on the host's thread, the one
 * that runs the core: begin makes the core's context current THERE and it
 * stays current until end. */
typedef struct {
	int  major;             /* the OpenGL ES version asked for: 2 or 3 */
	int  max_w, max_h;      /* the largest frame the core will draw */
	bool depth, stencil;
	bool top_down;          /* its first row at the top, not GL's way up */
} diatom_gpu_spec;

bool  diatom_port_gpu_begin(const diatom_gpu_spec *spec);
void  diatom_port_gpu_end(void);
/* The framebuffer to draw into, as a GL name: always the same one, with the
 * frame's own texture put under it here. The host calls it before every
 * frame, on its thread, since a core may read the name once and keep it; a
 * core that asks itself is given the name the host's last call returned
 * (src/hw.c), as it may ask from a thread with no context. */
uintptr_t diatom_port_gpu_target(void);
void *diatom_port_gpu_proc(const char *name);
/* The frame just drawn: w x h at the framebuffer's bottom-left corner, GL's
 * way up (bottom row first) unless the spec said top_down. Shown like a
 * present's, into `dst`. */
void  diatom_port_present_gpu(int w, int h, diatom_rect dst, diatom_filter filter);
/* The last frame presented, read back as XRGB8888, top row first, w * 4 bytes
 * a row, for the host's previews and shots. False with no GPU frame yet. */
bool  diatom_port_gpu_read(void *xrgb, int *w, int *h, size_t cap);

/* Interleaved stereo S16 at caps.audio_rate. NEVER blocks; drops on overflow.
 * A blocking write is a legitimate sync strategy but is incompatible with
 * dynamic rate control, which the measured spread of core rates makes
 * mandatory: 32040 / 32768 / 44100 / 48000 / 65536 Hz across six cores.
 *
 * RETURNS the number of frames accepted, which may be fewer than `n` and may
 * be zero. Returning void made "drops on overflow" unobservable: the host
 * could not tell a dropped frame from a written one, so the only evidence of
 * trouble was the queue depth - and the queue was allowed to exceed the
 * capacity reported in caps, which hid it there too. A port must never accept
 * more than caps.audio_buffer_frames; partial acceptance is how it says no. */
size_t diatom_port_audio_write(const int16_t *frames, size_t n);
size_t diatom_port_audio_queued(void);

/* WHERE the sound goes, named the way this port's audio system names a device -
 * an ALSA device string on the Brick, an SDL device name on the desktop.
 *
 * The host says WHICH, the port knows HOW. The string crosses here
 * exactly as a core path does, and the port never learns what kind of thing it
 * names. "bluetooth" is not a word this interface knows, for the same reason
 * Diatom keeps no core list - which output to use is the host application's
 * decision, and a port that answered it would be holding a device list.
 *
 * NULL or empty is the default device.
 *
 * FALLS BACK rather than failing. A named device that will not open leaves the
 * port on the default and still running, because a sink that can be set while a
 * game runs can fail while a game runs, and ending the game is never the better
 * answer. Returns true when it opened what was asked for and false when it fell
 * back, filling `actual` either way - the host reports where the sound IS, not
 * where it asked for it to be.
 *
 * Reopening does not change caps.audio_rate: the port asks SDL for a fixed rate
 * and lets it convert, so a sink that runs at another rate is the port's problem
 * and never the resampler's. */
bool diatom_port_audio_set(const char *name, char *actual, size_t cap);

/* The default device, opened if nothing is open yet; true when one is open
 * after. A second call changes nothing, so it never throws away what is
 * queued.
 *
 * NOT at init, and that is the point of it. Opened at init, the stream was
 * running before the host had drawn its first frame, and the host's own
 * writes to the codec (on the Brick, its headphone stage and speaker
 * switches) then powered those paths up on the spot, with the driver's waits:
 * 124 ms of the launcher's start, on two boots of three, depending on which
 * process got there first (measured 2026-10-18). Opened after, those writes
 * only record a setting, and the power-up happens here. The host calls this
 * when the launcher has drawn (PREMAP) and at the start of every session,
 * whichever comes first; a device set by diatom_port_audio_set is open
 * already.
 *
 * caps.audio_buffer_frames is the port's buffer, open or not yet. When this
 * fails the host configures the session with 0, as for a port with no
 * sound. */
bool diatom_port_audio_start(void);

/* MUTE, which this port OBEYS rather than owns.
 *
 * The launcher reads a hardware switch and cuts the analog stage, which sits
 * below the mixer and silences every producer at once. Nothing about the pin,
 * the polarity, or what the switch MEANS belongs here.
 *
 * What the port must do is narrow: while this is set, never turn the output
 * back ON. It may still cut the path for its OWN silence - on this codec the
 * gain control's minimum is about -74 dB rather than nothing, so level 0 has
 * to switch the stage off as well - and that stays the port's business.
 *
 * The fault this exists for was demonstrated, not guessed: with the launcher
 * muted and a game running, a volume press brought the sound straight back,
 * because applying a level writes the same control. A port with no such stage
 * implements this as a no-op and is correct. */
void diatom_port_mute_set(bool on);
bool diatom_port_mute_get(void);

/* Where the sound actually is now.
 *
 * Polled, like the level pair below and for the same reason: the port must not
 * know the launcher protocol exists, so it cannot announce anything.
 * A sink that dies under the port - a headset switched off, or walking out of
 * range - is noticed here, because the port falls back on its own and the answer
 * changes. The host reads it beside the input bitfield it already polls. */
void diatom_port_audio_get(char *out, size_t cap);

void     diatom_port_input_poll(void);
uint32_t diatom_port_input_state(void);

/* The handheld's own sticks, where it has them (the TrimUI Smart Pro's two,
 * from ZyperOS's tools/sppad.c): left X and Y, then right X and Y, in
 * libretro's range (-0x7fff..0x7fff), + right and down, the centre and the
 * dead zone already taken out by the system's reader. Read at the last poll.
 * False, and nothing written, on a handheld with none: there the left stick
 * is the d-pad's, where RUN stick=dpad says so (stick.h). */
bool diatom_port_input_axes(int16_t axes[4]);

/* The buttons that went down during the last poll, whether they are held now
 * or were let go again before it ended: a tap that went down and up between
 * two polls is here and not in diatom_port_input_state. A slow game polls
 * ten times a second (a Saturn game at 10 fps, 2026-11-20: an 80 ms press of
 * MENU never opened the menu), and a quick press fits between two polls.
 * The host reads it for its own keys alone (MENU, a hotkey under SELECT, the
 * stick's turn, the DS's screens), as the level keys are latched below; a
 * core reads the state only, as libretro polls it. Each poll starts it again
 * and diatom_port_input_reset clears it. */
uint32_t diatom_port_input_pressed(void);

/* Called when a pause ends, before the next poll. While paused nothing polls,
 * and the launcher's menu was time somebody spent pressing buttons: a port
 * whose input queues (evdev, SDL's event queue) would read it all back on the
 * next poll and act on it again - a volume key pressed three times in the menu
 * was three more steps on resume, measured on the GKD Pixel 2 2026-10-02. So
 * the queue is thrown away, and what is held is read from the device as it is
 * now. */
void     diatom_port_input_reset(void);

/* The rumble motor. `strong` and `weak` are the two motors a pad of the
 * DualShock kind has, 0 (still) to 65535 (full), as the core asks; a device
 * with one motor runs it from whichever is stronger. `scale`, 0 to 1, is how
 * much of that reaches the hand: the player's Rumble level (MENU > Sound, or
 * the game's own: Off 0, Low 0.35, Medium 0.65, Full 1) times the system's
 * share (systems.cfg's rumble| lines, Doom's 0.35). Each port makes its
 * motor's sense of it: the Pixel 2's force feedback runs at the strength
 * times the scale; a motor whose strength is a voltage (the Brick's) may take
 * the scale as its step. Both strengths 0, or a scale of 0, stops it. The
 * whole state each time, not a change, so a call is never read against one
 * that was missed.
 *
 * May be called from a core's own thread (flycast2021 rumbles from its
 * emulation thread), so a port serializes it. A port with no motor says so in
 * its caps (rumble = false) and returns false here. */
bool     diatom_port_rumble(uint16_t strong, uint16_t weak, float scale);

/* Levels the user can change with the device's own keys while a game runs -
 * volume and brightness on the Brick and the GKD Pixel 2, nothing at all on
 * the desktop.
 *
 * The LEVEL is the host's (src/levels.c): which position each is at,
 * stepping it, holding a key to repeat, the shared scale it crosses the socket
 * on. The port is the hardware under it: how many positions a control has on
 * this device, what position the hardware is at, writing one, which level keys
 * are held, and drawing the bar. Raw device units (mixer registers, backlight
 * duty) never leave the port.
 *
 * A position is 0-based over `0 .. positions - 1`, and `positions` is the
 * number of distinct positions rather than a maximum index - the two differ by
 * one, and the state plane pins it because a shared scale that is off by one
 * produces a silent disagreement instead of an error. */
typedef enum {
	DIATOM_LEVEL_VOLUME = 0,
	DIATOM_LEVEL_BRIGHTNESS,
	DIATOM_LEVEL_COUNT
} diatom_level_kind;

/* How many positions this device has for `kind`, or 0 if it has no control
 * over it. */
int  diatom_port_level_positions(diatom_level_kind kind);

/* Where the hardware is now, as a position, or -1 if it cannot be read. A
 * register left outside the port's window by another program reads as the
 * nearer end. */
int  diatom_port_level_read(diatom_level_kind kind);

/* Put the hardware at `pos`. What position 0 means (a cut, on a volume control
 * whose minimum is not silence), the launcher's mute and re-mapping the volume
 * when the headphone jack changes are all the port's: it keeps what it last
 * wrote for that. */
void diatom_port_level_write(diatom_level_kind kind, int pos);

/* The level keys held right now, or pressed since the last call, as
 * DIATOM_LEVEL_KEY_* bits - the second so a tap that goes down and up between
 * two frames still counts. Which physical key is which is the port's: on the
 * GKD Pixel 2 a volume key pressed while MENU is held is a brightness key
 * until it is let go. The host steps on a press and repeats while held; the
 * port only reports. diatom_port_input_reset clears what is latched. */
enum {
	DIATOM_LEVEL_KEY_VOLUME_UP       = 1u << 0,
	DIATOM_LEVEL_KEY_VOLUME_DOWN     = 1u << 1,
	DIATOM_LEVEL_KEY_BRIGHTNESS_UP   = 1u << 2,
	DIATOM_LEVEL_KEY_BRIGHTNESS_DOWN = 1u << 3,
};
uint32_t diatom_port_level_keys(void);

/* Show the level bar for `kind` at `pos` of `positions`, for a moment from now.
 * Drawn by the port, in its own pixels; the host calls this on every step, so
 * a held key keeps it up. */
void diatom_port_level_shown(diatom_level_kind kind, int pos, int positions);

/* What the level bar looks like now, as the host's boost past the top works
 * it out (port/boost.h, DESIGN.md 11.6): the level, the mark moving to it, the
 * boost's cells, its charge, the flash as it opens. Said before each
 * diatom_port_level_shown and again each frame it moves while the bar is up;
 * a copy is kept until said again. The port draws it (osdbar_render_look)
 * while the bar is up for the look's kind, and the plain bar for any other
 * (POWER's hold, a kind the host has not given a look for). Keeps the bar's
 * life as it was: only diatom_port_level_shown brings it up or holds it. A
 * port with no bar implements it as a no-op and is correct. */
void diatom_port_level_look(const osdbar_look *look);

/* The brightness boost's lift of the mid-tones in the display's gamma table
 * (panelgamma.h's `lift`): 0 none, up to PANELGAMMA_LIFT_MAX, kept until said
 * again, through every load of the table after (Screen Colors, Night Mode,
 * the display taken back). The top rung is the backlight's own maximum, so
 * this is all there is past it. A port with no table ignores it. */
void diatom_port_level_lift(int lift);

/* True once after the headphone jack's plug went in or out under a game: a
 * change the port saw on its own poll, not its first look after a handover.
 * The host closes the volume boost then, so the new output starts at the top
 * without it, as the launcher does on its side. A port with no jack returns
 * false. */
bool diatom_port_level_plugged(void);

/* Show the low-battery disc over the game, or take it off. The launcher
 * decides what low and critical mean; the port draws the disc the launcher
 * draws on its own screens, in the same place: radius 9, centered 34 px from
 * the top and right edges, red (224, 72, 72), on every frame until told
 * otherwise. `blink` (critical): shown in the even seconds of the port's
 * clock and not in the odd ones, the launcher's rhythm. A port with no screen
 * of its own implements this as a no-op and is correct. */
void diatom_port_low_battery(bool on, bool blink);

/* On-Screen Stats over the game (ZyperOS's src/hud.h, the launcher's
 * in-game menu): its style, as the launcher's SETHUD words say it - the
 * items ("fps,speed,temp"), corner, edge, bg, opacity, color and size, and
 * the clock's 24 hours - kept by the port until said again, a word it does
 * not know leaving its default. */
typedef struct {
	char items[64], corner[8], edge[8], bg[8], opacity[8], color[8], size[8];
	int  clock24;
} diatom_hud_words;
void diatom_port_hud_style(const diatom_hud_words *w);

/* ...and the line shown with these numbers, the pictures the core made a
 * second and the frames it emulated against the game's own rate (1 is full
 * speed; src/emumeter.h says how each is counted), until the next call; or
 * taken off. The port reads what else its items say (the
 * chip's temperature, load and clock, the battery, the time) itself, then,
 * and only those picked. The host calls it twice a second while it is on,
 * from the emulation thread. A port with no screen of its own implements
 * these as no-ops and is correct. */
void diatom_port_hud(bool on, double fps, double speed);

/* POWER held, with the launcher's Power Button on Hold (SETPOWERHOLD): the
 * level bar's power kind (osdbar.h, OSDBAR_POWER) over the game, its
 * segments filling on the port's own clock over `ms`, `at` of which have
 * gone already, until it is full; then it holds and fades as the level bar
 * does. on=false takes it off: at once, or, `done`, full and fading as
 * after a level step. A port with no screen of its own implements this as a
 * no-op and is correct. */
void diatom_port_power_hold(bool on, unsigned ms, unsigned at, bool done);

/* Hold the CPU at its top clock (`cpu`), and the GPU at its top with it
 * (`gpu`), or give each clock back to its governor. The host says true while
 * a game runs fast, and false at 1x, in the menu, at the end of a session and
 * once at start, which clears a hold a crashed Diatom left behind. It may say
 * it every turn; the port acts on a change.
 *
 * Fast forward is the one time a game needs the top clock and ondemand does
 * not give it: a fast turn waits out what is left of its periods, so the CPU
 * looks idle enough to be clocked down, and a turn that no longer fits drops
 * the speed by a whole step, not a little. Measured on the Pixel 2,
 * 2026-10-28, a heavy GBA game at 4x for a minute and more: 2.36x with the
 * clock pinned at 1296 MHz, 1.71x pinned at 1008, and 1.48x to 2.17x left to
 * ondemand, which moved between the two. A port with no clock of its own to
 * set implements this as a no-op and is correct.
 *
 * A core that draws with the GPU (src/hw.c) is the other time, at 1x: its
 * frame goes back and forth between the CPU and the GPU, each idle while the
 * other works, so each governor sees half a load and neither climbs. The host
 * says true for the whole of such a game, at full power, and a port holds its
 * GPU's clock at the top with the CPU's. Measured on the Pixel 2, 2026-11-04,
 * Mario Kart 64: the CPU at 600 MHz and the GPU at 400 of 520 under their
 * governors, 38.5 fps; both at the top, 48.9.
 *
 * A game that leaves its late frames undrawn (RUN frameskip=late, a DS game)
 * is the third, at 1x and at full power, the CPU alone: such a core runs at
 * the edge of its frame time, and the moment it has a little to spare, the
 * sleep between frames reads to ondemand as an idle core, which steps down,
 * and the frames after it come late. Measured on the Pixel 2, 2026-11-07,
 * Mario Kart DS on DSperate with its thread layout: left to ondemand, the CPU
 * spent 7 s of 30 at 1008 or 1200 MHz, 4.3% of the frames undrawn and 57.3
 * fps at the panel, a second at 37; held at the top, 2.0% and 59.6, no
 * second under 58. Its GPU only puts the picture on the panel, so it is left
 * to its governor. */
void diatom_port_fast_clock(bool cpu, bool gpu);

/* How many frames since the port started were ready in time and still
 * reached the panel a refresh late: asked of the display and shown more than
 * a refresh and a quarter after, where a frame asked of it in time is shown
 * at the next refresh. A frame that comes after a refresh is shown at the
 * one after and is not one: that is the core's lateness, which the frame
 * loop sees for itself. What such a miss costs is the display's own work,
 * a shader on a GPU kept at a low clock (src/flipwatch.h), which no ceiling
 * the frame loop knows of lifts by itself. A port that cannot tell returns
 * 0. The frame loop asks once a turn. */
unsigned diatom_port_refreshes_missed(void);

/* How much clock a game gets. FULL is the device's top clocks; MEDIUM and
 * LIGHT are lower ceilings for systems that keep their speed under them
 * (RUN power=, from the launcher's systems.cfg). Ceilings, not settings:
 * under each, the governors still clock down when there is little to do,
 * and a lower clock also means a lower voltage, which is the battery saved.
 * The host steps a game back up at the first frame it could not keep, says
 * FULL while a game runs fast or sits in the menu, at the end of a session
 * and once at start, and may say the same level again; the port acts on a
 * change, or on the first level after diatom_port_power_invalidate. A port
 * with no clocks of its own implements this as a no-op. */
enum { DIATOM_POWER_FULL, DIATOM_POWER_MEDIUM, DIATOM_POWER_LIGHT };
void diatom_port_power(int level);

/* Forget the level written last, so the next one is written whatever it is.
 * The clock files are the launcher's whenever Diatom is not running a game:
 * between games and while its menu is up. ZyperOS on the Pixel 2 puts the
 * DDR's ceiling down on a screen left still (plat_ram_idle), and a resident
 * Diatom that remembered FULL across that ran a game started with no press
 * under it. Called
 * at the same handovers as diatom_port_level_invalidate: a game's start and a
 * RESUME. */
void diatom_port_power_invalidate(void);

/* Forget everything the port remembers about the output state.
 *
 * Needed because Diatom is RESIDENT: the port keeps what it last wrote, to
 * re-apply it on a jack change or a mute, and the launcher owns levels whenever
 * Diatom is not presenting. So between games, and across a menu, the hardware
 * can be changed underneath it, and re-applying a stale write would put back a
 * level nobody is at.
 *
 * That includes the LAST HEADPHONE JACK STATE the port acted on, and forgetting
 * it is not an extra: a port re-maps the level between the speaker and
 * headphone windows on a transition IT observes, and it observes none while the
 * launcher is driving. A cable pulled out between games therefore leaves a
 * headphone-window value in the register that neither side re-maps, because
 * each sees its own remembered state already agreeing with the hardware. On
 * this device that reads as a working speaker at almost no volume. Measured
 * 2026-09-05: the register held 29 with a speaker window whose quiet end is 39.
 *
 * So this is called at every handover (the host forgets its own positions at
 * the same moment), and nothing is re-applied until the next write rather than
 * trusting a memory formed while something else was driving. */
void diatom_port_level_invalidate(void);

/* True once the port's surface has gone away - a closed window on desktop.
 * Not in the original design; surfaced during implementation. It concerns the
 * port's own viability, not anything about games, so it belongs here. */
bool diatom_port_should_quit(void);

/* Write what was last presented. Pixels and a path, no domain nouns, and both
 * backends want it - on desktop to see what happened, on device because a
 * screenshot is otherwise unobtainable. Also not in the original design: it
 * specified ten functions and implementation has made it twelve within a day,
 * which is worth noticing even though both additions look justified. */
bool diatom_port_capture(const char *path);

uint64_t diatom_port_now_us(void);
/* Wait `us` microseconds on that clock: the frame loop's sleep to its next
 * turn and a held push's wait for room (src/audio.c). A nanosleep on every
 * device port (port_clock.h). The desktop port's test clock
 * (DIATOM_TEST_CLOCK) moves its own time on instead, so a check of the
 * pacing runs exactly the same whatever else the computer is doing. */
void diatom_port_sleep_us(double us);

/* The last refresh a frame was flipped at, in diatom_port_now_us's time, and
 * the refresh period measured from the flips, for the display lock to keep
 * its frames half a refresh from the boundary. False where the port has no
 * flips to time (the loop then runs at refresh_hz alone). */
bool diatom_port_vblank(uint64_t *at_us, double *period_us);
void     diatom_port_log(diatom_log_level lvl, const char *msg);

#endif /* DIATOM_PORT_H */
