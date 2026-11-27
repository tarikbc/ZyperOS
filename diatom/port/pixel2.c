/* GKD Pixel 2 port.
 *
 * The Pixel 2 is an RK3326S with a Mali-G31 and a 640x480 panel that is
 * physically 480x640 portrait, mounted turned. Its system is plastron:
 * mainline Linux, Mesa's Panfrost, SDL2 with ALSA, no udev, no fbdev.
 *
 * Presentation is OpenGL ES 3 on a GBM surface, page-flipped through KMS by a
 * thread of its own. present() copies the core's frame into a latest-wins
 * mailbox and returns; the thread uploads it as a texture, draws it scaled and
 * turned in one quad, and flips at the panel's rate. That is the Brick's flip
 * thread with the blit moved to the GPU, because the CPU blit measured 7.7 ms
 * a frame here against a SNES core that already takes 10.5.
 *
 * The panel's orientation never leaves this file: the surface is reported as
 * 640x480, and the turn is in the quad's vertices.
 *
 * The display is shared with the launcher by DRM master, held only between the
 * first present after a stop and the end of the next present_stop.
 *
 * SDL2 provides audio and the clock only. Buttons are read straight from the
 * kernel's input devices, found by what they report.
 *
 * This file does not include libretro.h and must never need to.
 */
/* What tells this port's RK3326 handhelds apart, for the ones that build it
 * with their own (port/r36s.c): the name its log lines start with, the
 * card's folder, and the codes the A and B caps and the MENU key send. The
 * Pixel 2's here. */
#ifndef RK_PORT
#define RK_PORT          "pixel2"
#define RK_CODE_A        BTN_SOUTH
#define RK_CODE_B        BTN_EAST
#define RK_CODE_FUNCTION BTN_TRIGGER_HAPPY1
#endif
/* Whether the handheld has sticks to read (the R36S's two, and their clicks
 * as L3 and R3). None on the Pixel 2: its pad is read as it always was. */
#ifndef RK_STICKS
#define RK_STICKS 0
#endif

#include <SDL.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <drm_fourcc.h>
#include <gbm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <math.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "diatom_port.h"
#include "ladder.h"
#include "hud.h"
#include "osdbar.h"
#include "panelgamma.h"
#include "port_clock.h"
#include "port_cpufreq.h"
#include "port_power.h"
#include "rumble_evdev.h"
#include "themefile.h"

#define AUDIO_RATE 48000
/* Capacity in FRAMES, the Brick's figure: 4096 at 48 kHz is about 85 ms, with
 * rate control holding it near half. Measured here 2026-10-01 through the
 * desktop port and the system's dmix: 1,200 frames of Contra, nothing dropped,
 * the queue between 131 and 2912. */
#define AUDIO_BUFFER_FRAMES 4096
#define AUDIO_FRAME_BYTES   (2 * (int)sizeof(int16_t))

/* 2 dB down for the RK817, the speaker and headphone jack. Sound near full
 * scale is clipped past Diatom on its way out: Metroid Zero Mission's intro
 * peaks at -1.8 dB and crackled on and off on the speaker, 20 clicks in 17 s
 * on a recording, none of them in what Diatom handed over, no underruns, the
 * Brick clean with the same game. Two volume steps lower still crackled, so
 * the clipping comes before the volume; 2 dB less going in did not crackle
 * (2026-10-30). MP4 plays everything 2 dB down too (for its own overs, in
 * October), so a game and a song sound alike. Not a USB-C DAC, which has its
 * own path and has not been heard to clip. */
#define RK817_HEADROOM 0.7943f

/* ---------- the display ---------------------------------------------------- */

static int                 g_drm_fd = -1;
static struct gbm_device  *g_gbm;
static struct gbm_surface *g_gbm_surface;
static struct gbm_bo      *g_shown_bo;      /* on glass, released after the next flip */
static uint32_t            g_crtc_id, g_connector_id;
static int                 g_crtc_pipe;     /* index in the resources, for vblank waits */
static drmModeModeInfo     g_mode;
static bool                g_crtc_lit;      /* something already scans out on the CRTC */
static bool                g_have_master;
/* The backlight's value whose gamma table belongs on the display
 * (port/panelgamma.h): a brightness step stores it from any thread, and the
 * present thread loads the table between flips (show_frame), where none is
 * pending. -1 until a step: the value in sysfs, read once. g_gamma_have is
 * the present thread's own: what it last put there, -2 for not known, as
 * after another program had the display. */
static int                 g_gamma_want = -1;
static int                 g_gamma_have = -2;
/* The player's Screen Colors, as the launcher left them in
 * PANELGAMMA_COLOR_FILE: read again each time the display comes back
 * (g_gamma_have -2), since they change only in the launcher's MENU. And
 * Night Mode's warmth and nights there: the clock looked at once a second,
 * and the table loaded at a night's start and end (g_gamma_color.night). */
static panelgamma_color    g_gamma_color;
static panelgamma_night    g_gamma_night;
static time_t              g_night_looked;
/* The brightness boost's lift (diatom_port_level_lift): the host's word,
 * put on g_gamma_color by display_gamma, which loads the table again when it
 * moves */
static int                 g_gamma_lift;

/* The port's name, as the shared GL code's log lines start with it
 * (port/gles_state.h, port/gles_draw.h) */
#define PORT_NAME RK_PORT

#include "gles_state.h"

/* ---------- the overlay and the level bar ----------------------------------- */

/* The overlay's scale: ZyperOS's 1024-unit screen to the surface, as the
 * level bar's (draw_level_bar); 1 where the surface is that screen */
static float ov_scale(void);

/* The overlay from diatom_port_overlay: a borrowed pointer, not a copy (see
 * diatom_port.h). The thread uploads it under g_mx, and the host's next call
 * takes g_mx first, so the buffer is never read after the host lets go of it. */
static const uint8_t *g_ov;
static int            g_ov_w, g_ov_h;
static uint64_t       g_ov_until;
static bool           g_ov_dirty;           /* a new image to upload */

/* The level bar: when it came up and when it last changed, which is all its
 * slide and fade need (osdbar_envelope), and the theme it draws in, which the
 * launcher says in THEME_FILE and osd_show reads on each press. */
static uint64_t g_osd_shown, g_osd_changed;
static bool     g_osd_on;
static int      g_osd_level, g_osd_max, g_osd_kind;   /* kind 0 volume, 1 brightness,
                                                       * 2 POWER's hold */
/* POWER held, with the launcher's Power Button on Hold (SETPOWERHOLD): the
 * bar's power kind filling on this clock from g_hold_t0 over g_hold_ms,
 * under g_mx; hold_tick turns it into the bar's level each frame */
static bool     g_hold_on;
static uint64_t g_hold_t0;
static unsigned g_hold_ms;
static int      g_osd_theme;                          /* DS_LIGHT or DS_DARK */
/* The bar's colors in that theme, as the player's look has them (the theme
 * file's second line, themefile_read_bar), and a count that moves when they
 * do, for the render thread to hand them to osdbar_colors */
static ds_theme g_osd_colors;
static unsigned g_osd_colors_gen;
/* The level bar's look as the host's boost has it (diatom_port_level_look),
 * under g_mx: drawn for the kind it is of, osd_look_now says */
static osdbar_look g_osd_look;
static bool     g_lowbat, g_lowbat_blink;   /* the low-battery disc; under g_mx */
/* On-Screen Stats (diatom_port_hud): its line, its style and the look's
 * colors as the host last said them, and whether the render thread has
 * painted them yet; under g_mx. The style is the emulation thread's too
 * (g_hud_style_now), which makes the line from it. */
static bool      g_hud_on, g_hud_dirty;
static char      g_hud_text[160];
static ds_theme  g_hud_colors;
static hud_style g_hud_style;

/* ---------- levels ---------------------------------------------------------- */

/* Output gain, by ioctl on /dev/snd/controlC0, with the kernel's struct
 * vendored the way the Brick port does it (see port/brick.c for why a raw
 * ioctl and not alsa-lib). The RK817's "Master Playback Volume" is 0-255, two
 * channels, and not inverted: 255 is 0 dB, 0 is -95 dB (the driver's own
 * scale, read with amixer 2026-10-01). It has no switch; speaker or headphones
 * is "Playback Mux", which the system's jackswitch daemon drives, not us. */
struct dm_ctl_elem_id {
	unsigned int numid; int iface; unsigned int device, subdevice;
	unsigned char name[44]; unsigned int index;
};
struct dm_aes_iec958 {
	unsigned char status[24], subcode[147], pad, dig_subframe[4];
};
struct dm_ctl_elem_value {
	struct dm_ctl_elem_id id;
	unsigned int indirect: 1;
	union {
		union { long value[128]; long *value_ptr; } integer;
		union { long long value[64]; long long *value_ptr; } integer64;
		union { unsigned int item[128]; unsigned int *item_ptr; } enumerated;
		union { unsigned char data[512]; unsigned char *data_ptr; } bytes;
		struct dm_aes_iec958 iec958;
	} value;
	unsigned char reserved[128];
};
#define DM_CTL_ELEM_READ   _IOWR('U', 0x12, struct dm_ctl_elem_value)
#define DM_CTL_ELEM_WRITE  _IOWR('U', 0x13, struct dm_ctl_elem_value)

#define GAIN_CTL     "Master Playback Volume"
#define GAIN_LEVELS  20         /* what the USER moves in: 20 steps of 5% */

/* The windows the twenty levels spread over, as ATTENUATION from the
 * register's top: 0 is 255, 0 dB.
 *
 * Set by ear on 2026-10-02 with a game playing, the way the Brick's were; the
 * steps taken are in ZyperOS's src/device/pixel2.c. Speaker: level 20 on raw
 * 255, level 1 on raw 163, about 34 dB (172 and 31 dB until 1.0.6, too loud
 * at night for players). Headphones (Apple EarPods): level 20
 * on raw 244, level 1 on raw 76, about 63 dB. The windows below are the ones
 * that land levels 1 and 20 exactly there with the rounding both sides share.
 *
 * Level 0 is raw 0, -95 dB, as the cut: the control has no switch. Whether
 * that is silent by ear is also unmeasured.
 *
 * Attenuation, not raw, because that is how ZyperOS's launcher holds it (its
 * src/device/pixel2.c): its shared ladder arithmetic was written for the
 * Brick's inverted register, loud end low, and attenuation is that shape. The
 * same numbers and the same rounding on both sides, for the reason brick.c
 * gives: a level that crosses the socket has to mean the same thing on both.
 * Change one, change the other. */
#define GAIN_RAW_MAX    255
#define SPK_ATT_TOP     0
#define SPK_ATT_BOTTOM  97   /* 87 until 1.0.6: two old steps under the old floor */
#define HP_ATT_TOP      11
#define HP_ATT_BOTTOM   188

/* A USB-C DAC's volume: "USB Playback Volume" on the codec's card, a software
 * volume that plastron's ALSA config puts in front of the DAC (its pcm.usb),
 * the DAC's own control set to full by the system. ZyperOS makes the control
 * at startup (its src/device/pixel2.c says why it can't be left to alsa-lib)
 * and writes it with every level; so does this, both controls each time, so a
 * DAC plugged in mid-game needs nothing re-applied. 0-255, 0.375 dB a step,
 * the codec's step; 0 is silence. The window is ZyperOS's, a starting guess
 * from the EarPods' headphone window. Change one, change the other. */
#define USB_CTL         "USB Playback Volume"
#define USB_RAW_MAX     255
#define USB_ATT_TOP     11
#define USB_ATT_BOTTOM  188

static bool g_muted;                /* the launcher's */
static int  g_mixer_fd = -1;
static int  g_level = -1;           /* the position last read or written; -1 none */

/* Backlight, /sys/class/backlight/backlight, 0-255, 0 is off. The same rungs
 * as ZyperOS's src/device/pixel2_levels.h, which says how they were found:
 * the device tree's brightness-levels table sends 1 to 17 all to the same
 * output, and 17 is the backlight's floor, the panel black below it. The two
 * rungs under it, 9 and 13, keep the backlight at the floor, and the
 * display's gamma table takes the light to 9/17 and 13/17 of it
 * (port/panelgamma.h; display_gamma sets it). The rungs are equal ratios from
 * 9 to 255, since brightness is perceived in ratios (spike 2026-08-26). */
#define BACKLIGHT "/sys/class/backlight/backlight/brightness"
static const unsigned char bright_ladder[] = {
	9, 13, 17, 23, 31, 42, 57, 77, 103, 140, 189, 255
};
#define BRIGHT_LEVELS ((int)(sizeof bright_ladder / sizeof bright_ladder[0]) - 1)

/* ---------- input ----------------------------------------------------------- */

#define BITS_PER_LONG   (8 * (int)sizeof(long))
#define NLONGS(max)     (((max) + BITS_PER_LONG) / BITS_PER_LONG)
#define BIT_IS_SET(a,b) (((a)[(b) / BITS_PER_LONG] >> ((b) % BITS_PER_LONG)) & 1UL)

static int      g_pad_fd = -1;      /* the gamepad: "gkd_pixel2_joypad" */
static char     g_pad_path[32];     /* its node, opened again to rumble */
static int      g_keys_fd = -1;     /* the volume keys: "gpio-keys" */
static int      g_jack_fd = -1;     /* the codec's jack switch */
static uint32_t g_buttons;
static uint32_t g_pressed;          /* went down during the last poll */
static bool     g_input_debug;

/* Kernel code -> Diatom button.
 *
 * The codes are the device tree's (ROCKNIX's rk3326s-gkd-pixel2.dts), and the
 * labels are MEASURED: every key pressed in a known order on 2026-10-01 with a
 * logger reading the kernel's events. The caps marked A, B, X, Y send SOUTH,
 * EAST, NORTH, WEST - not the Brick's arrangement, where X and Y were the
 * other way round from position. The codes are named for the labels, not for
 * where the caps sit: A is on the right and B at the bottom, so the A cap
 * sends SOUTH. Mapping by label is what puts the right-hand button on the
 * game's A, as on a SNES pad. L2 and R2 are real keys, the d-pad is four
 * keys rather than a hat, and the key the device calls FUNCTION
 * (BTN_TRIGGER_HAPPY1) is MENU. */
static const struct { int code; int btn; } keymap[] = {
	{ BTN_DPAD_UP,    DIATOM_BTN_UP },    { BTN_DPAD_DOWN,  DIATOM_BTN_DOWN },
	{ BTN_DPAD_LEFT,  DIATOM_BTN_LEFT },  { BTN_DPAD_RIGHT, DIATOM_BTN_RIGHT },
	{ RK_CODE_A,      DIATOM_BTN_A },     { RK_CODE_B,      DIATOM_BTN_B },
	{ BTN_NORTH,      DIATOM_BTN_X },     { BTN_WEST,       DIATOM_BTN_Y },
	{ BTN_TL,         DIATOM_BTN_L1 },    { BTN_TR,         DIATOM_BTN_R1 },
	{ BTN_TL2,        DIATOM_BTN_L2 },    { BTN_TR2,        DIATOM_BTN_R2 },
	{ BTN_SELECT,     DIATOM_BTN_SELECT },{ BTN_START,      DIATOM_BTN_START },
	{ RK_CODE_FUNCTION, DIATOM_BTN_MENU },
#if RK_STICKS
	/* The sticks' clicks, on a handheld with sticks (the R36S's) */
	{ BTN_THUMBL,     DIATOM_BTN_L3 },    { BTN_THUMBR,     DIATOM_BTN_R3 },
#endif
};

/* ---------- audio ----------------------------------------------------------- */

static SDL_AudioDeviceID g_audio;
static char              g_audio_dev[128];
/* The device is opened from the host's thread (SETAUDIO, a session's start)
 * and, since a core may make its sound on a thread of its own, written from
 * that thread too, where a write that finds the sink gone reopens the
 * default. So g_audio and g_audio_dev change under this, one at a time. */
static pthread_mutex_t   g_audio_dev_mx = PTHREAD_MUTEX_INITIALIZER;

static void port_logf(diatom_log_level lvl, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));

static void port_logf(diatom_log_level lvl, const char *fmt, ...)
{
	char msg[256];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(msg, sizeof msg, fmt, ap);
	va_end(ap);
	diatom_port_log(lvl, msg);
}

/* ==================== levels ================================================ */

/* The gamepad, the volume keys and the headphone jack, found by what each
 * reports, not by number - there is no udev here, and enumeration order is not
 * a promise.
 *
 * In one pass, keeping the nodes it wants and closing only the rest (the power
 * key, which the launcher watches). Closing an evdev descriptor waits out an
 * RCU grace period in the kernel (evdev_detach_client), 10 to 50 ms each on
 * this device while the boot is busy; ZyperOS's scan of the same kind spent
 * about 190 ms of its startup in close(), measured 2026-10-02, and this port's
 * scan was the same shape - one per device, closing everything passed over. */
static void input_scan(void)
{
	unsigned long keys[NLONGS(KEY_MAX)], sw[NLONGS(SW_MAX)];
	char path[32];
	int i, fd;

	for (i = 0; i < 32; i++) {
		bool kept = true;

		snprintf(path, sizeof path, "/dev/input/event%d", i);
		if ((fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC)) < 0) continue;
		memset(keys, 0, sizeof keys);
		memset(sw, 0, sizeof sw);
		ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys);
		ioctl(fd, EVIOCGBIT(EV_SW, sizeof sw), sw);
		if      (g_pad_fd < 0  && BIT_IS_SET(keys, BTN_SOUTH)) {
			g_pad_fd = fd;
			snprintf(g_pad_path, sizeof g_pad_path, "%s", path);
		}
		else if (g_keys_fd < 0 && BIT_IS_SET(keys, KEY_VOLUMEUP))       g_keys_fd = fd;
		else if (g_jack_fd < 0 && BIT_IS_SET(sw, SW_HEADPHONE_INSERT))  g_jack_fd = fd;
		else kept = false;
		if (!kept) close(fd);
	}
}

/* Is there a plug in the headphone jack? SW_HEADPHONE_INSERT on the codec's
 * input device ("rk817_int Headphones"), found by input_scan. */
static void jack_open(void)
{
	if (g_jack_fd < 0) input_scan();
	if (g_jack_fd < 0)
		diatom_port_log(DIATOM_LOG_WARN, "pixel2: no headphone jack input; "
		                "volume uses the speaker window");
}

static int jack_present(void)
{
	unsigned long bits[NLONGS(SW_MAX)];

	if (g_jack_fd < 0) return 0;
	memset(bits, 0, sizeof bits);
	if (ioctl(g_jack_fd, EVIOCGSW(sizeof bits), bits) < 0) return 0;
	return BIT_IS_SET(bits, SW_HEADPHONE_INSERT) ? 1 : 0;
}

static int att_top(void) { return jack_present() ? HP_ATT_TOP    : SPK_ATT_TOP; }
static int att_bot(void) { return jack_present() ? HP_ATT_BOTTOM : SPK_ATT_BOTTOM; }

/* The window is attenuation from the register's top (port/ladder.h), and
 * position 0 is then replaced by the cut. */
static int level_to_raw(int lv)
{
	if (lv <= 0) return 0;
	return GAIN_RAW_MAX - ladder_window_raw(lv, GAIN_LEVELS, att_top(), att_bot());
}

static int raw_to_level(int raw)
{
	return ladder_window_pos(GAIN_RAW_MAX - raw, GAIN_LEVELS, att_top(), att_bot());
}

static int ctl_io(const char *name, long *val, int write)
{
	struct dm_ctl_elem_value v;

	if (g_mixer_fd < 0) return -1;
	memset(&v, 0, sizeof v);
	v.id.iface = 2;                                 /* SNDRV_CTL_ELEM_IFACE_MIXER */
	snprintf((char *)v.id.name, sizeof v.id.name, "%s", name);
	if (write) {
		/* Both channels: a stereo control written in one leaves the other
		 * where it was, which reads as a balance problem. */
		v.value.integer.value[0] = *val;
		v.value.integer.value[1] = *val;
		if (ioctl(g_mixer_fd, DM_CTL_ELEM_WRITE, &v) < 0) {
			port_logf(DIATOM_LOG_WARN, "pixel2: mixer rejected '%s' = %ld",
			          name, *val);
			return -1;
		}
		return 0;
	}
	if (ioctl(g_mixer_fd, DM_CTL_ELEM_READ, &v) < 0) return -1;
	*val = v.value.integer.value[0];
	return 0;
}

static int gain_io(long *val, int write) { return ctl_io(GAIN_CTL, val, write); }

static int g_jack_was = -1;         /* jack state at the last write; -1 = never */

static void gain_apply(void)
{
	bool cut = g_level <= 0 || g_muted;
	long v   = cut ? 0 : level_to_raw(g_level);
	long usb = cut ? 0 : USB_RAW_MAX - ladder_window_raw(g_level, GAIN_LEVELS,
	                                                     USB_ATT_TOP, USB_ATT_BOTTOM);

	g_jack_was = jack_present();
	gain_io(&v, 1);
	ctl_io(USB_CTL, &usb, 1);
}

/* A plug in or out seen here, for diatom_port_level_plugged. Atomic: this
 * poll runs in the input poll, which a core with a thread of its own may
 * call from there, and the host asks from the frame loop. */
static bool g_jack_moved;

/* Re-apply when the plug goes in or out, as on the Brick: whoever pumps input
 * owns the level, and the windows differ by jack. Not the first look after a
 * handover (g_jack_was -1), which finds the plug as it was. */
static void gain_jack_poll(void)
{
	if (jack_present() == g_jack_was || g_level < 0) return;
	if (g_jack_was >= 0) __atomic_store_n(&g_jack_moved, true, __ATOMIC_RELAXED);
	gain_apply();
}

static void osd_show(int kind, int level, int max)
{
	/* The theme is the launcher's, in a file: read before g_mx is taken, so
	 * the render thread is never kept waiting on the filesystem, and again
	 * on every press, so a change while the bar is up shows on the next one. */
	ds_theme colors;
	int theme = themefile_read_bar(THEME_FILE, &colors);

	pthread_mutex_lock(&g_mx);
	if (theme != g_osd_theme || memcmp(&colors.ground, &g_osd_colors.ground,
	                                   sizeof colors.ground * 18)) {
		g_osd_colors = colors;
		g_osd_colors_gen++;
	}
	uint64_t now = diatom_port_now_us();
	float a, rise;

	/* Coming in only when it was not already up: a press while it holds or
	 * fades keeps it where it is, as on the shelf. */
	if (!g_osd_on || !osdbar_envelope((uint32_t)((now - g_osd_shown) / 1000),
	                                  (uint32_t)((now - g_osd_changed) / 1000), &a, &rise))
		g_osd_shown = now;
	g_osd_on    = true;
	g_osd_kind  = kind;
	g_osd_level = level;
	g_osd_max   = max;
	g_osd_theme = theme;
	g_osd_changed = now;
	pthread_mutex_unlock(&g_mx);
}

/* What the bar shows now, under g_mx: the host's look while it is the look
 * of the bar's kind, else the plain bar (POWER's hold, a kind it has not
 * given a look for yet) */
static void osd_look_now(osdbar_look *l)
{
	const int kind = g_osd_kind == 2 ? OSDBAR_POWER : g_osd_kind ? OSDBAR_BRIGHT : OSDBAR_VOLUME;

	if (g_osd_kind != 2 && g_osd_look.kind == kind) *l = g_osd_look;
	else osdbar_look_plain(l, kind, g_osd_level, g_osd_max);
}

/* The hold's segments by now, under g_mx: the bar kept up while they fill,
 * then left to hold and fade as after a level step once all are lit */
static void hold_tick(uint64_t now)
{
	uint64_t t;
	int lit;

	if (!g_hold_on) return;
	t = (now - g_hold_t0) / 1000;
	lit = t >= g_hold_ms ? OSDBAR_POWER_STEPS
	                     : (int)(t * OSDBAR_POWER_STEPS / g_hold_ms);
	g_osd_on = true;
	g_osd_kind = 2;
	g_osd_max = OSDBAR_POWER_STEPS;
	g_osd_level = lit;
	g_osd_changed = now;
	if (lit >= OSDBAR_POWER_STEPS) g_hold_on = false;
}

void diatom_port_power_hold(bool on, unsigned ms, unsigned at, bool done)
{
	/* The theme, read before g_mx as osd_show reads it */
	ds_theme colors;
	int theme = themefile_read_bar(THEME_FILE, &colors);
	uint64_t now;
	float a, rise;

	pthread_mutex_lock(&g_mx);
	now = diatom_port_now_us();
	if (on) {
		if (theme != g_osd_theme || memcmp(&colors.ground, &g_osd_colors.ground,
		                                   sizeof colors.ground * 18)) {
			g_osd_colors = colors;
			g_osd_colors_gen++;
		}
		if (!g_osd_on || !osdbar_envelope((uint32_t)((now - g_osd_shown) / 1000),
		                                  (uint32_t)((now - g_osd_changed) / 1000), &a, &rise))
			g_osd_shown = now;
		g_osd_theme = theme;
		g_hold_ms = ms ? ms : 2000;
		g_hold_t0 = now - (uint64_t)(at < g_hold_ms ? at : g_hold_ms) * 1000;
		g_hold_on = true;
		hold_tick(now);
	} else {
		g_hold_on = false;
		if (g_osd_on && g_osd_kind == 2) {
			if (done) {
				g_osd_level = g_osd_max;
				g_osd_changed = now;
			} else if (now - g_osd_changed < (uint64_t)OSDBAR_HOLD_MS * 1000) {
				g_osd_changed = now - (uint64_t)OSDBAR_HOLD_MS * 1000;
			}
		}
	}
	pthread_mutex_unlock(&g_mx);
}

static int backlight_read(void)
{
	char buf[16];
	ssize_t n;
	int fd = open(BACKLIGHT, O_RDONLY | O_CLOEXEC);

	if (fd < 0) return -1;
	n = read(fd, buf, sizeof buf - 1);
	close(fd);
	if (n <= 0) return -1;
	buf[n] = '\0';
	return atoi(buf);
}

static void backlight_write(int raw)
{
	char buf[16];
	int fd = open(BACKLIGHT, O_WRONLY | O_CLOEXEC);
	int n = snprintf(buf, sizeof buf, "%d\n", raw);

	if (fd < 0) return;
	if (write(fd, buf, (size_t)n) != n)
		diatom_port_log(DIATOM_LOG_WARN, "pixel2: backlight write failed");
	close(fd);
}

/* ==================== GL ==================================================== */

#include "gles_draw.h"

/* ==================== KMS =================================================== */

/* The format the frames are scanned out as: ARGB8888 (bo_fb says why), or
 * XRGB8888, the same bytes with the alpha byte ignored, on a display whose
 * primary plane takes no ARGB8888 - QEMU's virtio-gpu, where plastron's
 * qemu_defconfig stands in for the Pixel 2 (show_frame finds out). */
static uint32_t g_fb_format = DRM_FORMAT_ARGB8888;

/* A GBM buffer's framebuffer, and the format it was made in */
struct bo_fb_info {
	uint32_t fb, format;
};

static void bo_destroyed(struct gbm_bo *bo, void *data)
{
	struct bo_fb_info *info = data;

	if (info && info->fb) drmModeRmFB(gbm_device_get_fd(gbm_bo_get_device(bo)), info->fb);
	free(info);
}

/* The framebuffer for a GBM buffer, made once and kept with it. ARGB8888,
 * because a page flip cannot change format and the launcher's SDL scans out
 * ARGB8888 (measured as EINVAL otherwise); made again if g_fb_format has
 * changed since. */
static uint32_t bo_fb(struct gbm_bo *bo)
{
	struct bo_fb_info *info = gbm_bo_get_user_data(bo);
	uint32_t handles[4] = { gbm_bo_get_handle(bo).u32 };
	uint32_t pitches[4] = { gbm_bo_get_stride(bo) };
	uint32_t offsets[4] = { 0 };
	uint32_t fb;

	if (info && info->format == g_fb_format) return info->fb;
	if (drmModeAddFB2(g_drm_fd, gbm_bo_get_width(bo), gbm_bo_get_height(bo),
	                  g_fb_format, handles, pitches, offsets, &fb, 0)) {
		port_logf(DIATOM_LOG_ERROR, "pixel2: AddFB2: %s", strerror(errno));
		return 0;
	}
	if (info) {
		drmModeRmFB(g_drm_fd, info->fb);
	} else if ((info = malloc(sizeof *info))) {
		gbm_bo_set_user_data(bo, info, bo_destroyed);
	} else {
		drmModeRmFB(g_drm_fd, fb);
		return 0;
	}
	info->fb = fb;
	info->format = g_fb_format;
	return fb;
}

/* Take the display, if it is not ours already, and find out whether anything
 * is scanning out. With the launcher or the boot splash in front, the CRTC is
 * lit and a flip is all it takes; standalone, nothing is, and the first frame
 * sets the mode. */
static bool take_display(void)
{
	drmModeCrtc *crtc;

	if (g_have_master) return true;
	if (drmSetMaster(g_drm_fd) != 0) {
		port_logf(DIATOM_LOG_ERROR, "pixel2: cannot take the display: %s",
		          strerror(errno));
		return false;
	}
	g_have_master = true;
	crtc = drmModeGetCrtc(g_drm_fd, g_crtc_id);
	g_crtc_lit = crtc && crtc->mode_valid && crtc->buffer_id;
	drmModeFreeCrtc(crtc);
	/* The table on the display is the last program's, and the brightness
	 * may have moved on the shelf since this process last set one:
	 * show_frame reads the backlight again and puts its table there before
	 * this frame goes up (with a game resumed at boot, over the table an
	 * older system image's splash filled) */
	g_gamma_have = -2;
	__atomic_store_n(&g_gamma_want, -1, __ATOMIC_RELAXED);
	return true;
}

static void give_display(void)
{
	if (!g_have_master) return;
	if (drmDropMaster(g_drm_fd) != 0)
		port_logf(DIATOM_LOG_WARN, "pixel2: drop master: %s", strerror(errno));
	g_have_master = false;
}

/* The refreshes, for the frame loop's display lock (diatom_port_vblank):
 * the last flip's, in diatom_port_now_us's time, and the period measured from
 * the flips, which start at the mode's own and move a thirty-second of each
 * flip's difference. Written by the presenting thread, read by the loop: a
 * word each, a read a frame old at worst. */
static volatile uint64_t g_vbl_at;
static volatile double   g_vbl_period, g_vbl_nominal;

static void flip_done(int fd, unsigned seq, unsigned sec, unsigned usec, void *data)
{
	static uint64_t prev;
	struct timespec mono;
	uint64_t flip, mono_us, now, at;

	*(bool *)data = true;
	/* The event's time is the kernel's CLOCK_MONOTONIC at the refresh; the
	 * loop's is SDL's counter, so the refresh is put in it by its distance
	 * from now on each */
	clock_gettime(CLOCK_MONOTONIC, &mono);
	now = diatom_port_now_us();
	flip = (uint64_t)sec * 1000000u + usec;
	mono_us = (uint64_t)mono.tv_sec * 1000000u + (uint64_t)mono.tv_nsec / 1000u;
	at = mono_us >= flip && mono_us - flip < 1000000u ? now - (mono_us - flip) : now;
	if (prev && g_vbl_nominal > 0 && at > prev) {
		double d = (double)(at - prev);
		long k = lround(d / g_vbl_nominal);

		if (k >= 1 && k <= 4) {
			double one = d / (double)k;

			if (fabs(one - g_vbl_nominal) < g_vbl_nominal * 0.02)
				g_vbl_period += (one - g_vbl_period) / 32;
		}
	}
	prev = at;
	g_vbl_at = at;
}

bool diatom_port_vblank(uint64_t *at_us, double *period_us)
{
	uint64_t at = g_vbl_at;

	if (!at || g_vbl_period <= 0) return false;
	*at_us = at;
	*period_us = g_vbl_period;
	return true;
}

static void wait_vblank(void)
{
	drmVBlank vbl;

	memset(&vbl, 0, sizeof vbl);
	vbl.request.type = DRM_VBLANK_RELATIVE;
	if (g_crtc_pipe == 1)
		vbl.request.type |= DRM_VBLANK_SECONDARY;
	else if (g_crtc_pipe > 1)
		vbl.request.type |= (g_crtc_pipe << DRM_VBLANK_HIGH_CRTC_SHIFT) &
		                    DRM_VBLANK_HIGH_CRTC_MASK;
	vbl.request.sequence = 1;
	drmWaitVBlank(g_drm_fd, &vbl);
}

/* Framebuffer fb on the CRTC: 0, or the errno that refused it. *done is
 * true once it is on glass, or a page flip's event will make it so. */
static int put_on_glass(uint32_t fb, bool *done)
{
	int tries = 0, ret, flip_err;

	if (!g_crtc_lit) {
		/* Standalone: nothing was lit, so this frame sets the mode. */
		if (drmModeSetCrtc(g_drm_fd, g_crtc_id, fb, 0, 0, &g_connector_id, 1,
		                   &g_mode) != 0) {
			flip_err = errno;
			port_logf(DIATOM_LOG_ERROR, "pixel2: set mode: %s", strerror(flip_err));
			return flip_err;
		}
		g_crtc_lit = true;
		*done = true;
		return 0;
	}
	/* The previous owner's last frame may still be queued, and the flip
	 * comes back busy until it lands: measured every time, one refresh.
	 * Wait a refresh and retry, a few times at most. */
	while ((ret = drmModePageFlip(g_drm_fd, g_crtc_id, fb,
	                              DRM_MODE_PAGE_FLIP_EVENT, done)) != 0 &&
	       errno == EBUSY && tries++ < 5)
		wait_vblank();
	if (ret == 0) return 0;
	/* What is lit is in another format, and a flip cannot change it: the
	 * boot splash's frame is XRGB8888, and a game resumed at boot comes
	 * before the launcher has put up anything of its own. Every flip came
	 * back EINVAL until the launcher drew its resume menu, 8 to 11 frames,
	 * seen 2026-10-27 on the Pixel 2 and in a player's logs. A mode-set can
	 * change it, as the standalone path's does, and after it the frames flip
	 * as usual. */
	flip_err = errno;
	if (flip_err == EINVAL &&
	    drmModeSetCrtc(g_drm_fd, g_crtc_id, fb, 0, 0, &g_connector_id, 1, &g_mode) == 0) {
		port_logf(DIATOM_LOG_INFO, "pixel2: what was lit would not take "
		          "a flip; set the mode with this frame instead");
		*done = true;
		return 0;
	}
	port_logf(DIATOM_LOG_WARN, "pixel2: page flip: %s", strerror(flip_err));
	return flip_err;
}

/* The gamma table the backlight's value and the Screen Colors ask for
 * (port/panelgamma.h): the dimming one at 9 and 13, none from 17 up in
 * Standard. Present thread, before a flip,
 * with no flip pending: loading it is an atomic commit, which waits a
 * refresh or two, at a brightness step or when the display comes back. */
static void display_gamma(void)
{
	int want = __atomic_load_n(&g_gamma_want, __ATOMIC_RELAXED), n;

	if (want < 0) {
		int unset = -1, now = backlight_read();

		if (now < 0) return;
		/* Unless a step stored its own meanwhile, which is newer */
		want = __atomic_compare_exchange_n(&g_gamma_want, &unset, now, false,
		                                   __ATOMIC_RELAXED, __ATOMIC_RELAXED) ? now : unset;
	}
	if (g_gamma_have == -2) {
		panelgamma_screen_read(PANELGAMMA_COLOR_FILE, &g_gamma_color, &g_gamma_night);
		g_gamma_color.night = panelgamma_night_at(&g_gamma_night, (long long)time(NULL));
		g_night_looked = 0;
	}
	/* The brightness boost's lift, never in the file: the host's, put on
	 * whatever was read, and the table again when it moves */
	{
		int lift = __atomic_load_n(&g_gamma_lift, __ATOMIC_RELAXED);

		if (lift != g_gamma_color.lift) {
			g_gamma_color.lift = lift;
			if (want == g_gamma_have) g_gamma_have = -1;
		}
	}
	/* Night Mode's hour, once a second: the table again at once when the
	 * warmth due now is not the one on (src/nightmode.h: one blink, no
	 * fade) */
	{
		time_t now = time(NULL);

		if (now != g_night_looked) {
			int w = panelgamma_night_at(&g_gamma_night, (long long)now);

			g_night_looked = now;
			if (w != g_gamma_color.night) {
				g_gamma_color.night = w;
				port_logf(DIATOM_LOG_INFO, "pixel2: Night Mode %s", w ? "on" : "off");
				if (want == g_gamma_have) g_gamma_have = -1;
			}
		}
	}
	if (want == g_gamma_have) return;
	n = panelgamma_apply_color(g_drm_fd, want, &g_gamma_color);
	if (n > 0)
		port_logf(DIATOM_LOG_INFO, "pixel2: the display's gamma table set for brightness %d%s",
		          want, g_gamma_color.night ? ", Night Mode on" : "");
	else if (n < 0)
		port_logf(DIATOM_LOG_WARN, "pixel2: could not set the display's gamma table for "
		          "brightness %d", want);
	g_gamma_have = want;               /* once, even refused: not at every frame */
}

/* Refreshes missed with the frame ready (diatom_port_refreshes_missed):
 * counted by the presenting thread, read by the loop */
static volatile unsigned g_refreshes_missed;

unsigned diatom_port_refreshes_missed(void)
{
	return __atomic_load_n(&g_refreshes_missed, __ATOMIC_RELAXED);
}

/* Put what was just swapped on glass, and wait until it is. */
static void show_frame(void)
{
	struct gbm_bo *bo = gbm_surface_lock_front_buffer(g_gbm_surface);
	uint32_t fb;
	bool done = false, flipped;
	int ret;
	uint64_t asked = 0;
	static double refresh_us;

	if (!bo) {
		diatom_port_log(DIATOM_LOG_ERROR, "pixel2: no front buffer after swap");
		return;
	}
	if (!(fb = bo_fb(bo)) || !take_display()) {
		gbm_surface_release_buffer(g_gbm_surface, bo);
		return;
	}
	display_gamma();

	ret = put_on_glass(fb, &done);
	/* Refused even by a mode-set: a display with no ARGB8888 at all, QEMU's
	 * virtio-gpu. The same frames as XRGB8888, from now on. */
	if (ret == EINVAL && g_fb_format == DRM_FORMAT_ARGB8888) {
		g_fb_format = DRM_FORMAT_XRGB8888;
		diatom_port_log(DIATOM_LOG_INFO, "pixel2: the display takes no ARGB8888; "
		                "showing the frames as XRGB8888");
		ret = (fb = bo_fb(bo)) ? put_on_glass(fb, &done) : EINVAL;
	}
	if (ret != 0) {
		gbm_surface_release_buffer(g_gbm_surface, bo);
		return;
	}
	/* A flip asked for (not a mode-set, which is done at once): when, and
	 * the mode's refresh period, to tell a missed refresh by */
	flipped = !done;
	if (flipped) asked = diatom_port_now_us();
	if (!refresh_us && g_mode.clock)
		refresh_us = (double)g_mode.htotal * g_mode.vtotal * 1000.0 / g_mode.clock;

	while (!done) {
		struct pollfd p = { .fd = g_drm_fd, .events = POLLIN };
		drmEventContext ev = { .version = 2, .page_flip_handler = flip_done };

		/* Bounded, so a flip that never completes costs a frame and a log
		 * line rather than the thread. */
		if (poll(&p, 1, 100) <= 0) {
			diatom_port_log(DIATOM_LOG_WARN, "pixel2: flip did not complete");
			break;
		}
		drmHandleEvent(g_drm_fd, &ev);
	}
	/* Shown more than a refresh and a quarter after it was asked for: the
	 * flip waited past the refresh it was in time for, on the GPU's drawing
	 * or the kernel's */
	if (flipped && done && refresh_us > 0 &&
	    (double)(diatom_port_now_us() - asked) > refresh_us * 1.25)
		__atomic_add_fetch(&g_refreshes_missed, 1, __ATOMIC_RELAXED);

	if (g_shown_bo) gbm_surface_release_buffer(g_gbm_surface, g_shown_bo);
	g_shown_bo = bo;
}

/* ==================== the presenting thread ================================= */

static bool gl_start(void)
{
	static const EGLint config_attribs[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
		EGL_NONE
	};
	static const EGLint context_attribs[] = {
		EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE
	};
	PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display =
		(PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
	EGLConfig configs[64], config = NULL;
	EGLint n = 0, i;

	g_egl_display = get_platform_display
		? get_platform_display(EGL_PLATFORM_GBM_KHR, g_gbm, NULL)
		: eglGetDisplay((EGLNativeDisplayType)g_gbm);
	if (g_egl_display == EGL_NO_DISPLAY || !eglInitialize(g_egl_display, NULL, NULL)) {
		diatom_port_log(DIATOM_LOG_ERROR, "pixel2: no EGL display");
		return false;
	}
	eglBindAPI(EGL_OPENGL_ES_API);

	/* The config whose native format is the scanout format exactly. */
	eglChooseConfig(g_egl_display, config_attribs, configs, 64, &n);
	for (i = 0; i < n; i++) {
		EGLint visual = 0;
		eglGetConfigAttrib(g_egl_display, configs[i], EGL_NATIVE_VISUAL_ID, &visual);
		if (visual == GBM_FORMAT_ARGB8888) { config = configs[i]; break; }
	}
	if (!config) {
		diatom_port_log(DIATOM_LOG_ERROR, "pixel2: no ARGB8888 EGL config");
		return false;
	}

	g_gbm_surface = gbm_surface_create(g_gbm, (uint32_t)g_panel_w, (uint32_t)g_panel_h,
	                                   GBM_FORMAT_ARGB8888,
	                                   GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
	if (!g_gbm_surface) {
		diatom_port_log(DIATOM_LOG_ERROR, "pixel2: no GBM surface");
		return false;
	}
	g_egl_config  = config;
	g_egl_context = eglCreateContext(g_egl_display, config, EGL_NO_CONTEXT, context_attribs);
	g_egl_surface = eglCreateWindowSurface(g_egl_display, config,
	                                       (EGLNativeWindowType)g_gbm_surface, NULL);
	if (g_egl_context == EGL_NO_CONTEXT || g_egl_surface == EGL_NO_SURFACE ||
	    !eglMakeCurrent(g_egl_display, g_egl_surface, g_egl_surface, g_egl_context)) {
		diatom_port_log(DIATOM_LOG_ERROR, "pixel2: no GLES 3 context");
		return false;
	}

#include "gles_objects.h"
	return true;
}

static void gl_stop(void)
{
	if (g_egl_display == EGL_NO_DISPLAY) return;
	eglMakeCurrent(g_egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	if (g_egl_surface != EGL_NO_SURFACE) eglDestroySurface(g_egl_display, g_egl_surface);
	g_nuloc = 0;
	/* The programs go with the context; a new one links them again */
	memset(g_prog_filter, 0, sizeof g_prog_filter);
	memset(g_prog_failed, 0, sizeof g_prog_failed);
	memset(g_prog_soft, 0, sizeof g_prog_soft);
	memset(g_soft_failed, 0, sizeof g_soft_failed);
	if (g_egl_context != EGL_NO_CONTEXT) eglDestroyContext(g_egl_display, g_egl_context);
	eglTerminate(g_egl_display);
	g_egl_display = EGL_NO_DISPLAY;
}

/* A capture: the front frame drawn again, unturned, into an offscreen target
 * the size of the surface, and read back. What the host asked for is the
 * picture as the player sees it, which is landscape. */
/* Where the shared presenting code (port/gles_present.h) differs by
 * device: the Pixel 2 draws into its window surface and swaps it */
#define PRESENT_TARGET() do { } while (0)
#define PRESENT_SWAP()   eglSwapBuffers(g_egl_display, g_egl_surface)
#define PORT_FN(n)       diatom_port_##n

#include "gles_present.h"

/* ---------- audio, as on the Brick ------------------------------------------ */

static bool audio_open(const char *name)
{
	SDL_AudioSpec want, have;

	if (g_audio) { SDL_CloseAudioDevice(g_audio); g_audio = 0; }
	if (name && *name) setenv("AUDIODEV", name, 1);
	else               unsetenv("AUDIODEV");

	SDL_memset(&want, 0, sizeof want);
	want.freq     = AUDIO_RATE;
	want.format   = AUDIO_S16SYS;
	want.channels = 2;
	want.samples  = 1024;

	if (SDL_WasInit(SDL_INIT_AUDIO) == 0 && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
		return false;
	g_audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
	if (!g_audio) return false;
	SDL_PauseAudioDevice(g_audio, 0);
	port_logf(DIATOM_LOG_INFO,
	          "audio: opened %s at %d Hz, %d ch, %d frames/period, %u byte buffer",
	          name && *name ? name : "the default device",
	          have.freq, have.channels, have.samples, have.size);
	return true;
}

static void audio_get(char *out, size_t cap)
{
	if (out && cap) snprintf(out, cap, "%s", g_audio_dev);
}

static size_t audio_queued(void)
{
	if (!g_audio) return 0;
	return SDL_GetQueuedAudioSize(g_audio) / AUDIO_FRAME_BYTES;
}

bool diatom_port_audio_set(const char *name, char *actual, size_t cap)
{
	const char *want = name ? name : "";
	bool ok;

	pthread_mutex_lock(&g_audio_dev_mx);
	if (g_audio && !strcmp(want, g_audio_dev)) {
		audio_get(actual, cap);
		pthread_mutex_unlock(&g_audio_dev_mx);
		return true;
	}
	ok = audio_open(name);
	if (!ok) {
		port_logf(DIATOM_LOG_WARN, "audio: %s unavailable (%s); using the default",
		          name && *name ? name : "the default device", SDL_GetError());
		if (audio_open(NULL)) g_audio_dev[0] = '\0';
	} else {
		snprintf(g_audio_dev, sizeof g_audio_dev, "%s", name ? name : "");
	}
	audio_get(actual, cap);
	pthread_mutex_unlock(&g_audio_dev_mx);
	return ok;
}

/* Not at init: see diatom_port_audio_start in diatom_port.h. NOT fatal when
 * it fails. A port that cannot open a sound device can still put pixels where
 * the device wants them, and silence costs far less than no emulator: the
 * write path drops, and the host configures the session with capacity 0, which
 * turns dynamic rate control off rather than leaving it correcting for a
 * buffer that does not exist. Tried again at the next start, which costs
 * nothing when it fails. */
bool diatom_port_audio_start(void)
{
	bool ok = true;

	pthread_mutex_lock(&g_audio_dev_mx);
	if (!g_audio) {
		ok = audio_open(NULL);
		if (ok) g_audio_dev[0] = '\0';
		else    port_logf(DIATOM_LOG_WARN,
		                  "audio unavailable (%s); continuing without sound",
		                  SDL_GetError());
	}
	pthread_mutex_unlock(&g_audio_dev_mx);
	return ok;
}

void diatom_port_audio_get(char *out, size_t cap)
{
	pthread_mutex_lock(&g_audio_dev_mx);
	audio_get(out, cap);
	pthread_mutex_unlock(&g_audio_dev_mx);
}

size_t diatom_port_audio_write(const int16_t *frames, size_t n)
{
	size_t queued, room;

	pthread_mutex_lock(&g_audio_dev_mx);
	if (g_audio && g_audio_dev[0] &&
	    SDL_GetAudioDeviceStatus(g_audio) == SDL_AUDIO_STOPPED) {
		diatom_port_log(DIATOM_LOG_WARN,
		                "audio: the sink went away; falling back to the default");
		g_audio_dev[0] = '\0';
		audio_open(NULL);
	}
	if (!g_audio || !frames || !n) { pthread_mutex_unlock(&g_audio_dev_mx); return 0; }
	queued = audio_queued();
	room   = queued >= (size_t)AUDIO_BUFFER_FRAMES
	       ? 0 : (size_t)AUDIO_BUFFER_FRAMES - queued;
	if (n > room) n = room;
	if (n) {
		if (!g_audio_dev[0]) {
			static int16_t down[AUDIO_BUFFER_FRAMES * 2];
			size_t i;

			for (i = 0; i < n * 2; i++) down[i] = (int16_t)(frames[i] * RK817_HEADROOM);
			frames = down;
		}
		SDL_QueueAudio(g_audio, frames, (Uint32)(n * AUDIO_FRAME_BYTES));
	}
	pthread_mutex_unlock(&g_audio_dev_mx);
	return n;
}

size_t diatom_port_audio_queued(void)
{
	size_t n;

	pthread_mutex_lock(&g_audio_dev_mx);
	n = audio_queued();
	pthread_mutex_unlock(&g_audio_dev_mx);
	return n;
}

/* ---------- input ----------------------------------------------------------- */

/* The pad's sticks, where it has them (the R36S's two, through the
 * joypad's analog mux; the Pixel 2's has none, and RK_STICKS 0 never looks
 * for them): their four axes as evdev reports them, with each one's range
 * and its flat, the dead zone the device tree gives. Read from the pad's
 * own events in the poll. */
static const int stick_codes[4] = { ABS_X, ABS_Y, ABS_RX, ABS_RY };
static struct { int value, min, max, flat; } g_stick[4];
static int g_sticks = -1;        /* -1 not looked yet, 0 none, 1 both */

static void sticks_read(void)
{
	unsigned long abs[NLONGS(ABS_MAX)];
	struct input_absinfo ai;
	int i;

	g_sticks = 0;
	if (!RK_STICKS || g_pad_fd < 0) return;
	memset(abs, 0, sizeof abs);
	if (ioctl(g_pad_fd, EVIOCGBIT(EV_ABS, sizeof abs), abs) < 0) return;
	for (i = 0; i < 4; i++) {
		if (!BIT_IS_SET(abs, stick_codes[i]) || ioctl(g_pad_fd, EVIOCGABS(stick_codes[i]), &ai) < 0 ||
		    ai.maximum <= ai.minimum)
			return;
		g_stick[i].value = ai.value;
		g_stick[i].min = ai.minimum;
		g_stick[i].max = ai.maximum;
		g_stick[i].flat = ai.flat;
	}
	g_sticks = 1;
	port_logf(DIATOM_LOG_INFO, RK_PORT ": sticks, X %d..%d flat %d, RX %d..%d flat %d",
	          g_stick[0].min, g_stick[0].max, g_stick[0].flat, g_stick[2].min, g_stick[2].max, g_stick[2].flat);
}

/* One axis in libretro's range: its middle 0, its flat 0 too, the rest
 * stretched to the ends */
static int16_t stick_axis(int i)
{
	const long mid = ((long)g_stick[i].min + g_stick[i].max) / 2;
	const long half = ((long)g_stick[i].max - g_stick[i].min) / 2;
	long v = (long)g_stick[i].value - mid, room = half - g_stick[i].flat;

	if (v > -g_stick[i].flat && v < g_stick[i].flat) return 0;
	if (room <= 0) return 0;
	v = v > 0 ? v - g_stick[i].flat : v + g_stick[i].flat;
	v = v * 0x7fff / room;
	return (int16_t)(v > 0x7fff ? 0x7fff : v < -0x7fff ? -0x7fff : v);
}

bool diatom_port_input_axes(int16_t axes[4])
{
	int i;

	if (g_sticks < 0) sticks_read();
	if (g_sticks <= 0) return false;
	for (i = 0; i < 4; i++) axes[i] = stick_axis(i);
	return true;
}

/* The pad's keys as they are right now, for a start or after the kernel
 * dropped events (SYN_DROPPED): the held-at-entry rule needs to know about a
 * key that was already down. */
static void pad_resync(void)
{
	unsigned long keys[NLONGS(KEY_MAX)];
	struct input_absinfo ai;
	size_t i;

	if (g_pad_fd < 0) return;
	for (i = 0; g_sticks > 0 && i < 4; i++)
		if (ioctl(g_pad_fd, EVIOCGABS(stick_codes[i]), &ai) == 0) g_stick[i].value = ai.value;
	memset(keys, 0, sizeof keys);
	if (ioctl(g_pad_fd, EVIOCGKEY(sizeof keys), keys) < 0) return;
	g_buttons = 0;
	for (i = 0; i < sizeof keymap / sizeof keymap[0]; i++)
		if (BIT_IS_SET(keys, keymap[i].code))
			g_buttons |= DIATOM_BIT(keymap[i].btn);
}

/* Normally found already, by jack_open's scan, which runs first. */
static void input_open(void)
{
	if (g_pad_fd < 0 || g_keys_fd < 0) input_scan();
	if (g_pad_fd < 0)
		diatom_port_log(DIATOM_LOG_WARN, "pixel2: no gamepad found; no game input");
	if (g_keys_fd < 0)
		diatom_port_log(DIATOM_LOG_WARN, "pixel2: no volume keys found");
	pad_resync();
}

/* The two volume keys, as level keys. What a key is, volume or brightness under
 * MENU, is settled when it goes down and kept while it is held, as ZyperOS keeps
 * it, so letting go of MENU mid-hold does not turn brightness into volume. A
 * press is latched until the host has read it, so a tap between two frames
 * counts; a key the events say is held is checked against the kernel, so a
 * release this port never saw cannot leave the host repeating. The stepping
 * and the repeat are the host's. */
static const int level_codes[2] = { KEY_VOLUMEUP, KEY_VOLUMEDOWN };
static struct {
	bool down;           /* held, by the events */
	bool pressed;        /* went down since the host last asked */
	bool bright;         /* went down with MENU held */
} g_level_key[2];

void diatom_port_input_poll(void)
{
	struct input_event ev[32];
	const uint32_t before = g_buttons;
	ssize_t n;
	size_t i, k;

	gain_jack_poll();

	/* Every press since the last poll, a tap gone up again too
	 * (diatom_port_input_pressed) */
	g_pressed = 0;
	while (g_pad_fd >= 0 && (n = read(g_pad_fd, ev, sizeof ev)) > 0) {
		for (i = 0; i < (size_t)n / sizeof ev[0]; i++) {
			if (ev[i].type == EV_SYN && ev[i].code == SYN_DROPPED) {
				pad_resync();
				continue;
			}
			if (ev[i].type == EV_ABS) {
				for (k = 0; g_sticks > 0 && k < 4; k++)
					if (ev[i].code == stick_codes[k]) g_stick[k].value = ev[i].value;
				continue;
			}
			if (ev[i].type != EV_KEY || ev[i].value == 2) continue;
			if (g_input_debug)
				port_logf(DIATOM_LOG_DEBUG, "key %d -> %d", ev[i].code, ev[i].value);
			for (k = 0; k < sizeof keymap / sizeof keymap[0]; k++) {
				if (keymap[k].code != ev[i].code) continue;
				if (ev[i].value) {
					g_buttons |= DIATOM_BIT(keymap[k].btn);
					g_pressed |= DIATOM_BIT(keymap[k].btn);
				} else {
					g_buttons &= ~DIATOM_BIT(keymap[k].btn);
				}
			}
		}
	}
	/* And a key a resync found down whose press was in the events dropped */
	g_pressed |= g_buttons & ~before;

	/* The volume keys are level keys, never reported as buttons and never
	 * sent to a core. With MENU held they are brightness instead, the
	 * Pixel 2's only way to it; the host sees the level move and treats
	 * MENU's release as the end of a chord. */
	while (g_keys_fd >= 0 && (n = read(g_keys_fd, ev, sizeof ev)) > 0) {
		for (i = 0; i < (size_t)n / sizeof ev[0]; i++) {
			int key;

			if (ev[i].type != EV_KEY || ev[i].value == 2) continue;
			if      (ev[i].code == KEY_VOLUMEUP)   key = 0;
			else if (ev[i].code == KEY_VOLUMEDOWN) key = 1;
			else continue;
			if (!ev[i].value) {
				g_level_key[key].down = false;
				continue;
			}
			if (g_input_debug)
				port_logf(DIATOM_LOG_DEBUG, "level key %d", ev[i].code);
			g_level_key[key].down    = true;
			g_level_key[key].pressed = true;
			g_level_key[key].bright  = (g_buttons & DIATOM_BIT(DIATOM_BTN_MENU)) != 0;
		}
	}
}

uint32_t diatom_port_input_state(void) { return g_buttons; }
uint32_t diatom_port_input_pressed(void) { return g_pressed; }

/* The force feedback's strength is the motor's: the scale multiplies it,
 * rounded, so Full (1) is the core's own number as it always was */
static uint16_t rumble_times(uint16_t v, float scale)
{
	float f;

	if (scale >= 1.0f) return v;
	if (scale <= 0.0f) return 0;
	f = (float)v * scale + 0.5f;
	return (uint16_t)(f > 65535.0f ? 65535.0f : f);
}

bool diatom_port_rumble(uint16_t strong, uint16_t weak, float scale)
{
	return evdev_rumble_set(rumble_times(strong, scale), rumble_times(weak, scale));
}

void diatom_port_input_reset(void)
{
	struct input_event ev[32];

	while (g_pad_fd >= 0 && read(g_pad_fd, ev, sizeof ev) > 0) { }
	while (g_keys_fd >= 0 && read(g_keys_fd, ev, sizeof ev) > 0) { }
	memset(g_level_key, 0, sizeof g_level_key);
	g_pressed = 0;
	pad_resync();
}

/* ---------- levels: the hardware under them --------------------------------- */

void diatom_port_level_invalidate(void)
{
	g_level    = -1;
	g_jack_was = -1;
}

int diatom_port_level_positions(diatom_level_kind kind)
{
	/* Asked every frame, so the backlight is looked for once, not read each
	 * time. */
	static int have_backlight = -1;

	switch (kind) {
	case DIATOM_LEVEL_VOLUME:
		return g_mixer_fd >= 0 ? GAIN_LEVELS + 1 : 0;
	case DIATOM_LEVEL_BRIGHTNESS:
		if (have_backlight < 0) have_backlight = backlight_read() >= 0;
		return have_backlight ? BRIGHT_LEVELS + 1 : 0;
	default:
		return 0;
	}
}

int diatom_port_level_read(diatom_level_kind kind)
{
	long v;
	int raw;

	switch (kind) {
	case DIATOM_LEVEL_VOLUME:
		/* Kept as well as returned: the mute and the jack re-apply act on
		 * the level the hardware is at, read here the first frame after a
		 * handover, before any press writes one. */
		if (gain_io(&v, 0) < 0) return -1;
		g_level = raw_to_level((int)v);
		return g_level;
	case DIATOM_LEVEL_BRIGHTNESS:
		/* Nearest rung, so a level the launcher left between two reads as the
		 * closer one. */
		if ((raw = backlight_read()) < 0) return -1;
		return ladder_nearest(bright_ladder, BRIGHT_LEVELS + 1, raw);
	default:
		return -1;
	}
}

void diatom_port_level_write(diatom_level_kind kind, int pos)
{
	switch (kind) {
	case DIATOM_LEVEL_VOLUME:
		g_level = pos;
		gain_apply();
		break;
	case DIATOM_LEVEL_BRIGHTNESS:
		if (pos < 0)             pos = 0;
		if (pos > BRIGHT_LEVELS) pos = BRIGHT_LEVELS;
		backlight_write(bright_ladder[pos]);
		__atomic_store_n(&g_gamma_want, bright_ladder[pos], __ATOMIC_RELAXED);
		break;
	default:
		break;
	}
}

uint32_t diatom_port_level_keys(void)
{
	static const uint32_t as_volume[2] = { DIATOM_LEVEL_KEY_VOLUME_UP, DIATOM_LEVEL_KEY_VOLUME_DOWN };
	static const uint32_t as_bright[2] = { DIATOM_LEVEL_KEY_BRIGHTNESS_UP, DIATOM_LEVEL_KEY_BRIGHTNESS_DOWN };
	unsigned long keys[NLONGS(KEY_MAX)];
	uint32_t out = 0;
	bool asked = false;
	int k;

	for (k = 0; k < 2; k++) {
		if (g_level_key[k].down) {
			if (!asked) {
				memset(keys, 0, sizeof keys);
				if (g_keys_fd < 0 || ioctl(g_keys_fd, EVIOCGKEY(sizeof keys), keys) < 0)
					memset(keys, 0, sizeof keys);
				asked = true;
			}
			if (!BIT_IS_SET(keys, level_codes[k])) g_level_key[k].down = false;
		}
		if (g_level_key[k].down || g_level_key[k].pressed)
			out |= g_level_key[k].bright ? as_bright[k] : as_volume[k];
		g_level_key[k].pressed = false;
	}
	return out;
}

void diatom_port_level_shown(diatom_level_kind kind, int pos, int positions)
{
	osd_show(kind == DIATOM_LEVEL_BRIGHTNESS ? 1 : 0, pos, positions - 1);
}

void diatom_port_level_look(const osdbar_look *look)
{
	pthread_mutex_lock(&g_mx);
	if (look) g_osd_look = *look;
	else memset(&g_osd_look, 0, sizeof g_osd_look);
	pthread_mutex_unlock(&g_mx);
}

/* On the present thread's next frame, which loads the table (display_gamma):
 * an atomic commit there, never one from this thread beside its flips */
void diatom_port_level_lift(int lift)
{
	if (lift < 0) lift = 0;
	if (lift > PANELGAMMA_LIFT_MAX) lift = PANELGAMMA_LIFT_MAX;
	__atomic_store_n(&g_gamma_lift, lift, __ATOMIC_RELAXED);
}

bool diatom_port_level_plugged(void)
{
	return __atomic_exchange_n(&g_jack_moved, false, __ATOMIC_RELAXED);
}

/* On-Screen Stats' host side (port/hud_host.h), on the Pixel 2's sensors */
#define HUD_ZONE_A       "soc-thermal"
#define HUD_ZONE_B       "gpu-thermal"
#define HUD_BATTERY_FILE "/sys/class/power_supply/battery/capacity"
#include "hud_host.h"

void diatom_port_low_battery(bool on, bool blink)
{
	pthread_mutex_lock(&g_mx);
	g_lowbat = on;
	g_lowbat_blink = blink;
	pthread_mutex_unlock(&g_mx);
}

/* No hardware mute on this device, but the launcher may still hold one:
 * while it does, the level never comes back on. */
void diatom_port_mute_set(bool on)
{
	if (on == g_muted) return;
	g_muted = on;
	if (g_mixer_fd >= 0 && g_level >= 0) gain_apply();
}

bool diatom_port_mute_get(void) { return g_muted; }

/* The ceilings, off this board's own tables: the CPU's steps (1296 MHz at
 * the top), the DDR's three trained rates (194, 328, 1056 MHz) and the GPU's
 * steps (200 to 520 MHz). FULL gives each back its top: the CPU its own
 * maximum, and 0 to a devfreq max_freq, which the kernel reads as no
 * ceiling. The DDR runs under the performance governor (plastron's
 * S01governor), so its ceiling is its rate. And the GPU's floor at its top
 * step, 520 MHz, while a core draws with it (see diatom_port.h). */
static const diatom_power_profile PX2_POWER = {
	.clock = {
		{ DIATOM_CPUFREQ "scaling_max_freq", { NULL, "1008000", "816000" },
		  DIATOM_CPUFREQ "cpuinfo_max_freq" },
		{ "/sys/class/devfreq/dmc/max_freq", { "0", "0", "328000000" }, NULL },
		{ "/sys/class/devfreq/ff400000.gpu/max_freq", { "0", "300000000", "200000000" }, NULL },
	},
	.n = 3,
	.gpu_floor = "/sys/class/devfreq/ff400000.gpu/min_freq",
	.gpu_floor_top = "520000000",
};

/* The same with the DDR at its full rate at every level, for a card with the
 * file steady-memory in .userdata/pixel2, or steady-memory.txt on PX2BOOT
 * (/run/zyperos-steady-memory, plastron's zyperos.init): the light level's
 * 328 MHz and the
 * lifts from it (the in-game menu, Fast Forward, a late frame) each switched
 * the DDR while the display read it, at any moment of its scan. ZyperOS's
 * src/device/pixel2.c (plat_ram_idle) says why it is there, and off. */
#define PX2_STEADY_MEMORY "/mnt/SDCARD/.userdata/" RK_PORT "/steady-memory"
#define PX2_STEADY_MEMORY_BOOT "/run/zyperos-steady-memory"
static const diatom_power_profile PX2_POWER_STEADY = {
	.clock = {
		{ DIATOM_CPUFREQ "scaling_max_freq", { NULL, "1008000", "816000" },
		  DIATOM_CPUFREQ "cpuinfo_max_freq" },
		{ "/sys/class/devfreq/dmc/max_freq", { "0", "0", "0" }, NULL },
		{ "/sys/class/devfreq/ff400000.gpu/max_freq", { "0", "300000000", "200000000" }, NULL },
	},
	.n = 3,
	.gpu_floor = "/sys/class/devfreq/ff400000.gpu/min_freq",
	.gpu_floor_top = "520000000",
};

void diatom_port_fast_clock(bool cpu, bool gpu)
{
	static bool said;
	static int gpu_held = -1;

	if (!diatom_cpufreq_hold_top(cpu) && !said) {
		said = true;
		diatom_port_log(DIATOM_LOG_WARN, "pixel2: could not set the CPU's lowest clock");
	}
	/* And the GPU's floor at its top step, for the same reason (see
	 * diatom_port.h); 0 is no floor, the governor's again. The first call
	 * always writes, as the CPU's does, so a crashed Diatom's hold is
	 * cleared. */
	if (gpu_held != (int)gpu) {
		gpu_held = gpu;
		if (!diatom_power_hold_gpu(&PX2_POWER, gpu))
			diatom_port_log(DIATOM_LOG_WARN, "pixel2: could not set the GPU's lowest clock");
	}
}

/* The ceilings for a power level (PX2_POWER above), written when the level
 * changes or after a handover (port_power.h, diatom_power_memo) */
static diatom_power_memo g_power_memo = { -1 };

void diatom_port_power(int level)
{
	if (!diatom_power_say(&g_power_memo,
	                      access(PX2_STEADY_MEMORY, F_OK) == 0 || access(PX2_STEADY_MEMORY_BOOT, F_OK) == 0
	                      ? &PX2_POWER_STEADY : &PX2_POWER, level))
		diatom_port_log(DIATOM_LOG_WARN, "pixel2: could not set every clock ceiling");
}

void diatom_port_power_invalidate(void) { diatom_power_forget(&g_power_memo); }

/* ---------- lifecycle ------------------------------------------------------- */

/* The connected connector, the CRTC driving it and its mode. The panel is the
 * only output; its preferred mode is its only mode (480x640, read with
 * modetest 2026-10-01). */
static bool kms_find(void)
{
	drmModeRes *res = drmModeGetResources(g_drm_fd);
	drmModeConnector *conn = NULL;
	drmModeEncoder *enc = NULL;
	int i;

	if (!res) return false;
	for (i = 0; i < res->count_connectors && !conn; i++) {
		drmModeConnector *c = drmModeGetConnector(g_drm_fd, res->connectors[i]);
		if (c && c->connection == DRM_MODE_CONNECTED && c->count_modes > 0) conn = c;
		else drmModeFreeConnector(c);
	}
	if (!conn) { drmModeFreeResources(res); return false; }

	g_connector_id = conn->connector_id;
	g_mode = conn->modes[0];
	for (i = 0; i < conn->count_modes; i++)
		if (conn->modes[i].type & DRM_MODE_TYPE_PREFERRED) { g_mode = conn->modes[i]; break; }

	enc = drmModeGetEncoder(g_drm_fd, conn->encoder_id ? conn->encoder_id : conn->encoders[0]);
	if (enc) {
		if (enc->crtc_id) {
			g_crtc_id = enc->crtc_id;
		} else {
			for (i = 0; i < res->count_crtcs; i++)
				if (enc->possible_crtcs & (1u << i)) { g_crtc_id = res->crtcs[i]; break; }
		}
	}
	for (i = 0; i < res->count_crtcs; i++)
		if (res->crtcs[i] == g_crtc_id) g_crtc_pipe = i;

	drmModeFreeEncoder(enc);
	drmModeFreeConnector(conn);
	drmModeFreeResources(res);
	return g_crtc_id != 0;
}

bool diatom_port_init(diatom_port_caps *out)
{
	g_input_debug   = getenv("DIATOM_INPUT_DEBUG") != NULL;
	g_present_debug = getenv("DIATOM_PRESENT_DEBUG") != NULL;

	g_drm_fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
	if (g_drm_fd < 0) { perror("open /dev/dri/card0"); return false; }
	/* Opening the device while nobody holds master makes us master, and a
	 * resident Diatom must not hold the display while the launcher draws.
	 * Taken back at the first present. */
	drmDropMaster(g_drm_fd);

	if (!kms_find()) {
		diatom_port_log(DIATOM_LOG_ERROR, "pixel2: no connected panel");
		return false;
	}
	g_panel_w = g_mode.hdisplay;
	g_panel_h = g_mode.vdisplay;
	/* Portrait panel, landscape surface. */
	g_turned    = g_panel_h > g_panel_w;
	g_surface_w = g_turned ? g_panel_h : g_panel_w;
	g_surface_h = g_turned ? g_panel_w : g_panel_h;

	g_gbm = gbm_create_device(g_drm_fd);
	if (!g_gbm) { diatom_port_log(DIATOM_LOG_ERROR, "pixel2: no GBM device"); return false; }

	/* g_idle on the monotonic clock, before the thread exists, for the one
	 * timed wait on it (gpu_target's): the wall clock here has jumped by
	 * decades on a dead RTC, which would turn 200 ms into forever. */
	{
		pthread_condattr_t ca;

		pthread_condattr_init(&ca);
		pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
		pthread_cond_destroy(&g_idle);
		pthread_cond_init(&g_idle, &ca);
		pthread_condattr_destroy(&ca);
	}
	if (pthread_create(&g_thread, NULL, present_thread, NULL) != 0) {
		diatom_port_log(DIATOM_LOG_ERROR, "pixel2: no presenting thread");
		return false;
	}
	pthread_mutex_lock(&g_mx);
	while (g_thread_ready == 0) pthread_cond_wait(&g_idle, &g_mx);
	pthread_mutex_unlock(&g_mx);
	if (g_thread_ready < 0) {
		pthread_join(g_thread, NULL);
		return false;
	}
	g_thread_running = true;

	g_mixer_fd = open("/dev/snd/controlC0", O_RDWR | O_CLOEXEC);
	jack_open();
	input_open();
	/* The motor is the joypad's: plastron's device tree hands PWM0 to the
	 * joypad driver, which makes the pad an FF_RUMBLE device. An image
	 * without that has no motor to run, and says so here. */
	out->rumble = evdev_rumble_open(g_pad_fd, g_pad_path);

	/* No audio here: it opens once the launcher has drawn, or at the first
	 * game; see diatom_port_audio_start. */

	port_logf(DIATOM_LOG_INFO,
	          "pixel2: panel %dx%d%s, surface %dx%d, crtc %u, audio %d Hz, pad %s, rumble %s",
	          g_panel_w, g_panel_h, g_turned ? " (turned)" : "",
	          g_surface_w, g_surface_h, g_crtc_id, AUDIO_RATE,
	          g_pad_fd >= 0 ? "yes" : "none", out->rumble ? "yes" : "none");

	out->surface_w           = g_surface_w;
	out->surface_h           = g_surface_h;
	out->audio_rate          = AUDIO_RATE;
	out->audio_buffer_frames = AUDIO_BUFFER_FRAMES;   /* open or not yet */
	out->present_blocks      = false;
	out->gpu                 = true;
	/* The mode's own rate: 25 MHz over 570 x 730 on the Pixel 2, 60.08 Hz */
	out->refresh_hz          = g_mode.htotal && g_mode.vtotal
	                         ? g_mode.clock * 1000.0 / ((double)g_mode.htotal * g_mode.vtotal) : 0;
	if (out->refresh_hz > 0 && g_vbl_nominal <= 0) {
		g_vbl_nominal = 1e6 / out->refresh_hz;
		g_vbl_period = g_vbl_nominal;
	}
	return true;
}

void diatom_port_shutdown(void)
{
	int i;

	if (g_thread_running) {
		drain();
		pthread_mutex_lock(&g_mx);
		g_thread_stop = true;
		pthread_cond_signal(&g_cv);
		pthread_mutex_unlock(&g_mx);
		pthread_join(g_thread, NULL);
		g_thread_running = false;
	}
	give_display();
	for (i = 0; i < MAILBOX_FRAMES; i++) free(g_frames[i].pixels);
	/* The level and the brightness are user settings and stay where they are,
	 * as on the Brick. */
	if (g_mixer_fd >= 0) { close(g_mixer_fd); g_mixer_fd = -1; }
	evdev_rumble_close();
	if (g_pad_fd >= 0)   close(g_pad_fd);
	if (g_keys_fd >= 0)  close(g_keys_fd);
	if (g_jack_fd >= 0)  close(g_jack_fd);
	pthread_mutex_lock(&g_audio_dev_mx);
	if (g_audio)         SDL_CloseAudioDevice(g_audio);
	g_audio = 0;
	pthread_mutex_unlock(&g_audio_dev_mx);
	SDL_Quit();
	/* The GBM surface and device go with the process: the buffer on glass
	 * stays allocated until then. */
	if (g_drm_fd >= 0)   close(g_drm_fd);
}

bool diatom_port_should_quit(void) { return false; }

bool diatom_port_capture(const char *path)
{
	bool ok;

	if (!g_thread_running || !path) return false;
	drain();
	pthread_mutex_lock(&g_mx);
	g_capture_path = path;
	pthread_cond_signal(&g_cv);
	while (g_capture_path) pthread_cond_wait(&g_idle, &g_mx);
	ok = g_capture_ok;
	pthread_mutex_unlock(&g_mx);
	return ok;
}

uint64_t diatom_port_now_us(void)
{
	return diatom_ticks_to_us(SDL_GetPerformanceCounter(),
	                          SDL_GetPerformanceFrequency());
}

void diatom_port_log(diatom_log_level lvl, const char *msg)
{
	static const char *tag[] = { "debug", "info", "warn", "error" };
	fprintf(stderr, "[%s] %s\n", tag[lvl], msg);
}
