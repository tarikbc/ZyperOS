/* TrimUI Brick (TG3040) port.
 *
 * Presentation is raw fbdev with a flip thread; SDL2, plastron's own build,
 * provides audio, the monotonic clock and the BMP writer for a capture. The
 * buttons and the headphone jack are read from their evdev nodes (input_scan).
 *
 * fbdev is not a fallback, it is the device's native display path. Measured
 * 2026-08-24 on firmware 1.1.1: scanout is the Allwinner disp2 engine
 * (/dev/fb0 -> the "disp" platform driver), while /dev/dri/card0 is only the
 * PowerVR render node (pvrsrvkm) with no display capability. The firmware's
 * SDL2 has exactly one real video driver, "mali", whose EGL swap blocks
 * ~30ms - two vblank intervals - regardless of swap interval, which quantized
 * the frame loop to 30fps.
 *
 * FBIOPAN_DISPLAY is itself a blocking vsync'd flip. Measured: a pan issued
 * right after vblank returns in one interval (a tight pan loop sustains
 * 60fps), but one issued mid-interval misses the latch deadline and waits for
 * the vsync after next (~25ms). A self-paced loop always lands mid-interval,
 * so calling pan inline halved the frame rate exactly as the EGL swap did.
 *
 * Hence the flip thread. present() only blits and publishes the page in a
 * latest-wins mailbox, never blocking, which is the seam's contract; the
 * thread pans at the panel's own rate and eats the blocking wait. Flips latch
 * at vblank, so no tearing; three pages mean the page being drawn is never
 * the one on glass or in flight. The frame loop keeps its own absolute clock,
 * exactly as on desktop - no console runs at the panel's rate.
 *
 * This file does not include libretro.h and must never need to.
 */
#include <SDL.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "diatom_port.h"
#include "ladder.h"
#include "hud.h"
#include "osdbar.h"
#include "panelgamma.h"
#include "panelgamma_sunxi.h"
#include "port_clock.h"
#include "port_cpufreq.h"
#include "themefile.h"

#define AUDIO_RATE 48000
/* Capacity in FRAMES (one frame = two int16 samples). 4096 at 48kHz is ~85ms,
 * with rate control aiming to hold it near half that. Same figure as desktop:
 * nothing about the device argues for a different one yet. */
#define AUDIO_BUFFER_FRAMES 4096
#define AUDIO_FRAME_BYTES   (2 * (int)sizeof(int16_t))

#define FB_PAGES 3

/* The GPU path's state, shared with the Pixel 2's port (port/gles_state.h):
 * the EGL objects, the frames' mailbox the presenting thread draws, the GL
 * objects, and a GPU core's frames. Its g_presented ("anything presented
 * since the last stop") and g_present_debug are this file's CPU path's too. */
#define PORT_NAME "brick"
#include "gles_state.h"

/* The GPU path's start and stop, from init and shutdown (the GPU path, below) */
static void gl_init(void);
static void gl_quit(void);
/* The GPU path can start (gl_init), and failed once (gl_ensure): then never
 * again in this process, so a driver that refuses costs one try */
static bool g_gl_possible, g_gl_failed;

static int                       g_fb_fd = -1;
static uint8_t                  *g_fb;
static size_t                    g_fb_size;
static struct fb_var_screeninfo  g_vinfo;
static struct fb_fix_screeninfo  g_finfo;
static int                       g_pages;      /* usable pages, up to FB_PAGES */
static bool                      g_pan_broken; /* pan failed; draw to front */

/* Flip mailbox. All page-role fields are guarded by g_flip_mx:
 *   g_page_front    on glass (last pan that completed)
 *   g_page_inflight being panned right now, -1 if none
 *   g_page_pending  published by present(), waiting for the thread, -1 if none
 * present() draws into any page holding none of those roles; when all three
 * are taken it steals g_page_pending back, which is safe precisely because the
 * thread only takes pending under the same lock. Latest wins, nothing waits. */
static pthread_t        g_flip_thread;
static pthread_mutex_t  g_flip_mx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t   g_flip_cv = PTHREAD_COND_INITIALIZER;
static pthread_cond_t   g_flip_idle = PTHREAD_COND_INITIALIZER;
static int              g_page_front;
static int              g_page_inflight = -1;
static int              g_page_pending  = -1;
static bool             g_flip_stop;
static bool             g_flip_running;

static SDL_AudioDeviceID g_audio;
static int               g_pad_fd = -1;  /* "Brick Gamepad": every button */
static bool              g_quit;
static uint32_t          g_buttons;
/* The pad's sticks, where it has them (the Smart Pro's, ABS_X and ABS_Y,
 * ABS_RX and ABS_RY from tools/sppad.c): the Brick's has none. g_axis_max is
 * ABS_X's end, 0 for no sticks. */
static int16_t           g_axes[4];
static int               g_axis_max;
static uint32_t          g_pressed;     /* went down during the last poll */
static bool              g_input_debug;

/* Opaque value for the framebuffer's alpha channel, zero if it has none.
 * The disp2 engine composites the fb layer in PER-PIXEL alpha mode: pixels
 * with a zero alpha byte are invisible, composited over black. Measured the
 * hard way 2026-08-24 - a pipeline that paced and captured perfectly while
 * the panel showed nothing, because capture reads memory and the panel reads
 * alpha. */
static uint32_t g_opaque;

/* Resampling maps, one entry per destination pixel on each axis, rebuilt only
 * when the source size, the destination rect or the filter changes.
 *
 * `w` is the weight of source pixel idx+1, in 1/256 units. Zero means the
 * destination pixel sits wholly inside one source pixel, which is the case for
 * every pixel at an integer factor and for most of them otherwise - so it is
 * the fast path, not an optimization for a rare case. */
typedef struct { int idx; int w; } diatom_tap;
typedef struct { int r, g, b; }    diatom_rgb;

static diatom_tap  *g_colmap, *g_rowmap;
static int          g_map_src_w = -1, g_map_src_h = -1;
static diatom_rect  g_map_dst;
static diatom_filter g_map_filter;
static bool          g_map_valid;

/* Output gain, by ioctl on /dev/snd/controlC0.
 *
 * Written for TrimUI's system, which had no alsa-lib or tinyalsa headers, and
 * forking `tinymix` per keypress means a process spawn in the input path plus a
 * dependency on a binary. So: a raw ioctl on a device node, which is what this
 * port already does for /dev/fb0. The struct layout is vendored from the kernel
 * UAPI rather than depended on, and its SIZE is baked into the request number
 * by _IOWR - validated against the running kernel by a probe (mixprobe) before
 * it was written here (sizeof 1224, read agreed with tinymix to the digit).
 *
 * `digital volume` is 0-63 and **INVERTED**: 0 is loudest, 63 is silence. The
 * driver advertises `dBscale-min=-74.24dB, step=+1.16dB`, i.e. that higher is
 * louder. That metadata is wrong. Proven by diffing the mixer across a
 * volume-up press in the device UI: 37 -> 15 when turned UP.
 *
 * `Headphone Volume` is 0-7 at 6 dB a step and is **INVERTED** too: 0 is
 * loudest, 7 is near silence, and its TLV lies about the direction exactly the
 * way `digital volume`'s does. Nothing on this codec's metadata can be trusted.
 *
 * This comment used to say the opposite - that the control was not a speaker
 * level, and that raising it rerouted output to the headphone JACK and muted
 * the speakers. That was wrong, and it was wrong in the most expensive way
 * available: `HpSpeaker Switch` drives the speaker off the headphone stage, so
 * this control gates everything the device plays. Whoever wrote the note
 * raised it, heard the speaker fall away, and reached for routing to explain
 * an attenuation. Settled by ear on 2026-08-31 with no plug in the jack - at 7
 * the speaker is barely audible, at 0 it is loud. It had sat at 3 since the
 * project began, so every sound Diatom has ever made was 18 dB down.
 */
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

#define GAIN_CTL     "digital volume"
#define GAIN_RAW_MAX 63         /* control range; 0 is loudest, 63 silent */
/* The part of that range you can actually hear.
 *
 * The register goes to 63 and stops being useful long before it: measured on
 * the speaker against a room baseline of 33 rms, raw 26 is 173 rms - five times
 * the room, quiet but unmistakably there - and by raw 34 it is 55, which is 1.6
 * times the room and indistinguishable from nothing. Spreading twenty levels
 * across the whole 63 therefore spends more than half the slider below the
 * floor: level 10 of 20 landed on raw 31, about two and a half times the room,
 * which is what "50% and very quiet" is.
 *
 * ZyperOS's launcher already had this number - the sweep was done there and the
 * constant is GAIN_RAW_USABLE in its src/platform.c. The port carries its own
 * copy of the mapping because it owns the level while a game runs, and only one
 * of the two was corrected. Same measurement, same ceiling, so the bar means
 * the same thing on the shelf and in a game.
 *
 * Those rms figures were taken with HP_CTL already at 0 - the sweep script sets
 * it before measuring - so they describe the chain as it behaves now, and the
 * 18 dB mixer_defaults() restored was never inside them. A note here briefly
 * claimed otherwise; it was inferred instead of read off the script that made
 * the table. The gap was between the SWEEP and gameplay, not inside the sweep,
 * which is why the table looked sane while the device sounded quiet.
 *
 * The weakness is the instrument. Those readings came from a microphone across
 * the room, where raw 26 is 5.2x the room and raw 34 is indistinguishable from
 * it - but a handheld sits at arm's length, and what reads as silence over
 * there is plainly audible in your hands. The floor is therefore set by ear,
 * not by this table.
 *
 * That session had ONE WORKING SPEAKER, unknown at the time. The quiet channel
 * turned out on 2026-09-02 to be a loose connection on the PCB; resoldered, and
 * both now play evenly. So 39 was originally judged against roughly half this
 * device's output - and it stood anyway, re-heard on the repaired hardware the
 * same day across the quiet end, the balance and the general sound. Two
 * independent confirmations now, not one lucky derivation.
 *
 * 39, chosen on the device on 2026-08-31 with a game playing, stepping the
 * register down until Eric called it: raw 37 is barely audible and is where he
 * wanted position 1. 39 is the constant that lands position 1 on 37 in both
 * this ladder and the launcher's, which round differently; 26 put it on 25.
 * Position 20 still lands on raw 0, so maximum is unchanged.
 *
 * The cost is resolution: about 2.3 dB a press rather than 1.5, in exchange for
 * 45 dB of range rather than 30. Worth it - 30 dB down is not quiet in a quiet
 * room, which is the thing a microphone across the room could not tell us. */
#define GAIN_RAW_USABLE 39

/* The jack wants a different window, not the same one moved.
 *
 * Set by ear on 2026-09-02 with a game playing and a plug in, the same method
 * that produced 39. The ceiling first: above raw 8 it is uncomfortable, so 8 is
 * where level 20 belongs. Then the floor, stepping down - 37, 45, 49, 53 and 57
 * were each still too loud to be a minimum, and 61 was called right.
 *
 * So the jack spends 53 register steps where the speaker spends 39, which an
 * offset cannot express: offsetting from the ceiling would put level 1 on 47,
 * and 45 was rejected on the way past. Headphones are far more efficient than
 * this speaker, so the same twenty steps have to cover more ground - 61 dB
 * against 45, about 3.1 dB a step rather than 2.3.
 *
 * The launcher carries the same four numbers in its src/platform.c, for the
 * same reason it carries GAIN_RAW_USABLE: this port owns the level while a game
 * runs and the shelf owns it otherwise, so a level that crosses the socket has
 * to mean the same thing on both sides. Change one, change the other. */
#define SPK_RAW_TOP     0
#define SPK_RAW_BOTTOM  GAIN_RAW_USABLE
#define HP_RAW_TOP      8
#define HP_RAW_BOTTOM   61

#define GAIN_LEVELS  20         /* what the USER moves in: 20 steps of 5% */
#define SPEAKER_CTL  "HpSpeaker Switch"   /* the speaker's only true mute */

/* Set by the launcher over the state plane, never read from hardware here:
 * what the switch is and what it means are the launcher's. */
static bool g_muted;
#define HP_CTL       "Headphone Volume"   /* 0-7, 6 dB a step, INVERTED */
#define HP_CTL_QUIET 7                    /* its quiet end, for the cut */
#define SWAP_CTL     "DAC Swap"           /* 1 crosses left and right */

/* Backlight. This device has no /sys/class/backlight; the panel is driven by
 * the Allwinner disp2 engine, and the firmware's own settings library goes
 * through /dev/disp. These are plain command numbers with an unsigned long[4]
 * argument block rather than _IOWR-encoded requests, so there is no struct size
 * to get wrong - validated against the running kernel by a probe (dispprobe)
 * before it was written here.
 *
 * Not inverted, unlike the mixer: 0 is dark, 255 is full. */
#define DISP_LCD_SET_BRIGHTNESS 0x102
#define DISP_LCD_GET_BRIGHTNESS 0x103

/* Brightness is perceived proportionally, not linearly. Measured 2026-08-26:
 * eight successive halvings of duty were all still visible, so equal *ratios*
 * are what read as equal steps. The 20-step linear ramp this replaces put nine
 * of its twenty steps above raw 128, where consecutive ones cannot be told
 * apart, and left raw 1-12 unreachable at any setting.
 *
 * The rungs are the launcher's own, read off ZyperOS's brightness keys, with two
 * added below. Matching them is not deference: every value here except the
 * bottom two is one the launcher also has a level for, so a brightness set in a
 * game means the same thing on the other side of the exit instead of snapping
 * to whatever is nearest. The step COUNT is launcher policy and belongs in the
 * protocol eventually; this is the standalone default.
 *
 * The first rung is the panel's measured floor - 0 and 1 are black, and the
 * driver clamps neither. ZyperOS's own bottom level is raw 1 and is deliberately
 * not copied. This table is also the only clamp there is: no arithmetic here
 * can produce a value off its ends. */
static const unsigned char bright_ladder[] = {
	2, 4, 8, 16, 32, 48, 72, 96, 128, 160, 192, 255
};
#define BRIGHT_LEVELS ((int)(sizeof bright_ladder / sizeof bright_ladder[0]) - 1)

static int g_disp_fd = -1;

/* The display's gamma table (port/panelgamma_sunxi.h): the Screen Colors and
 * Night Mode the launcher keeps in PANELGAMMA_COLOR_FILE. The kernel keeps a
 * table for every program after, so the launcher's is on the display when a
 * game starts; this port sets it again when it takes the display (a game
 * resumed at the boot comes before the launcher has set any), and at a
 * night's start and end over a game, which the launcher does not see while
 * the game has the screen. The frame loop's thread only. */
static panelgamma_color g_gamma_color;
static panelgamma_night g_gamma_night;
static time_t           g_night_looked;
/* The brightness boost's lift (diatom_port_level_lift), never in the file:
 * put on what is read at each take */
static int              g_gamma_lift;

static void display_gamma(bool take)
{
	time_t now = time(NULL);
	char msg[96];

	if (take) {
		panelgamma_screen_read(PANELGAMMA_COLOR_FILE, &g_gamma_color, &g_gamma_night);
		g_gamma_color.night = panelgamma_night_at(&g_gamma_night, (long long)now);
		g_gamma_color.lift = g_gamma_lift;
	} else {
		int w;

		if (now == g_night_looked) return;
		w = panelgamma_night_at(&g_gamma_night, (long long)now);
		g_night_looked = now;
		if (w == g_gamma_color.night) return;
		g_gamma_color.night = w;
	}
	g_night_looked = now;
	if (g_disp_fd < 0) return;
	if (panelgamma_sunxi_apply(g_disp_fd, &g_gamma_color) == 0)
		snprintf(msg, sizeof msg, "brick: the display's gamma table %s%s",
		         panelgamma_color_standard(&g_gamma_color) ? "off" : "set",
		         g_gamma_color.night ? ", Night Mode on" : "");
	else
		snprintf(msg, sizeof msg, "brick: could not set the display's gamma table");
	diatom_port_log(DIATOM_LOG_INFO, msg);
}

static int g_mixer_fd = -1;
static int g_level = -1;        /* the position last read or written; -1 none */

/* The overlay from diatom_port_overlay: a borrowed pointer, not a copy. See
 * diatom_port.h for the lifetime the host guarantees. */
static const uint8_t *g_ov;
static int            g_ov_w, g_ov_h;
static uint64_t       g_ov_until;

/* The level bar: when it came up and when it last changed, which is all its
 * slide and fade need (osdbar_envelope), what it shows, and the theme it
 * draws in, which the launcher says in THEME_FILE and osd_show reads on each
 * press. Set by the host's level steps and read by present, both on the
 * frame loop's thread, so no lock. */
static uint64_t g_osd_shown, g_osd_changed;
static bool     g_osd_on;
static int      g_osd_level;    /* what the bar shows - volume OR brightness */
static int      g_osd_max = GAIN_LEVELS;   /* out of what: the two differ now */
static int      g_osd_kind;     /* 0 volume, 1 brightness, 2 POWER's hold */
/* POWER held, with the launcher's Power Button on Hold (SETPOWERHOLD): the
 * bar's power kind filling on this clock from g_hold_t0 over g_hold_ms;
 * hold_tick turns it into the bar's level each frame. The frame loop's
 * thread, as the rest of the bar. */
static bool     g_hold_on;
static uint64_t g_hold_t0;
static unsigned g_hold_ms;
static int      g_osd_theme;    /* DS_LIGHT or DS_DARK */
/* The bar's colors in that theme, as the player's look has them (the theme
 * file's second line, themefile_read_bar); the gen moves when they do */
static ds_theme g_osd_colors;
static unsigned g_osd_colors_gen;
/* The level bar's look as the host's boost has it (diatom_port_level_look):
 * drawn for the kind it is of, osd_look_now says. Written under g_mx, which
 * the GPU path's thread reads it under; the CPU path reads it on the thread
 * that writes it. */
static osdbar_look g_osd_look;

/* Is there a plug in the headphone jack?
 *
 * SW_HEADPHONE_INSERT on the codec's own input node. The state is nowhere else
 * on this device - no ALSA jack control among its seventeen, nothing in sysfs -
 * so EVIOCGSW is the only way to ask, which is why this opens an input device
 * rather than reading a file.
 *
 * Found by capability, not by number. It is /dev/input/event2 today, but that
 * is an enumeration order rather than a promise, and being wrong would mean a
 * ladder calibrated for the wrong output with no sign that anything is off. */
#define BITS_PER_LONG   (8 * (int)sizeof(long))
#define NLONGS(max)     (((max) + BITS_PER_LONG) / BITS_PER_LONG)
#define SW_NLONGS       NLONGS(SW_MAX)
#define BIT_IS_SET(a,b) (((a)[(b) / BITS_PER_LONG] >> ((b) % BITS_PER_LONG)) & 1UL)

static int g_jack_fd = -1;

/* The gamepad and the headphone jack, in one pass, as the Pixel 2's port finds
 * its own, keeping the nodes it wants and closing only the rest: closing an
 * evdev descriptor waits out an RCU grace period in the kernel. */
static void input_scan(void)
{
	unsigned long keys[NLONGS(KEY_MAX)], sw[SW_NLONGS];
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
		if (g_pad_fd < 0 && BIT_IS_SET(keys, BTN_SOUTH)) {
			struct input_absinfo ax;

			g_pad_fd = fd;
			if (ioctl(fd, EVIOCGABS(ABS_X), &ax) == 0 && ax.maximum > 0)
				g_axis_max = ax.maximum;
		}
		else if (g_jack_fd < 0 && BIT_IS_SET(sw, SW_HEADPHONE_INSERT))  g_jack_fd = fd;
		else kept = false;
		if (!kept) close(fd);
	}
}

static void input_open(void);

static void jack_open(void)
{
	if (g_jack_fd < 0) input_scan();
	if (g_jack_fd < 0)
		fprintf(stderr, "diatom: no headphone jack input node; "
		                "volume will use the speaker ladder\n");
}

static int jack_present(void)
{
	unsigned long bits[SW_NLONGS];

	if (g_jack_fd < 0) return 0;
	memset(bits, 0, sizeof bits);
	if (ioctl(g_jack_fd, EVIOCGSW(sizeof bits), bits) < 0) return 0;
	return BIT_IS_SET(bits, SW_HEADPHONE_INSERT) ? 1 : 0;
}

/* Which window the level maps into. See HP_RAW_TOP above: headphones and the
 * speaker want different ceilings AND different floors, so both ends move. */
static int gain_top(void)  { return jack_present() ? HP_RAW_TOP    : SPK_RAW_TOP; }
static int gain_bot(void)  { return jack_present() ? HP_RAW_BOTTOM : SPK_RAW_BOTTOM; }

/* Positions to the register and back; the inverted register never leaves this
 * file. The register is the window as it stands, 0 loudest (port/ladder.h). A
 * register left past the window by another program reads as an end of the
 * scale: with a plug in, anything louder than raw 8 was set by something that
 * was not us. */
static int level_to_raw(int lv)
{
	return ladder_window_raw(lv, GAIN_LEVELS, gain_top(), gain_bot());
}
static int raw_to_level(int raw)
{
	return ladder_window_pos(raw, GAIN_LEVELS, gain_top(), gain_bot());
}

static int ctl_io(const char *name, long *val, int write)
{
	struct dm_ctl_elem_value v;

	if (g_mixer_fd < 0) return -1;
	memset(&v, 0, sizeof v);
	v.id.iface = 2;                                 /* SNDRV_CTL_ELEM_IFACE_MIXER */
	snprintf((char *)v.id.name, sizeof v.id.name, "%s", name);
	if (write) {
		v.value.integer.value[0] = *val;
		return ioctl(g_mixer_fd, DM_CTL_ELEM_WRITE, &v);
	}
	if (ioctl(g_mixer_fd, DM_CTL_ELEM_READ, &v) < 0) return -1;
	*val = v.value.integer.value[0];
	return 0;
}

static int gain_io(long *val, int write) { return ctl_io(GAIN_CTL, val, write); }

/* Write a control and complain if it does not land. The launcher had this same
 * write spelled "Headphone", a control this codec does not have; the ioctl
 * matches names exactly and the return was discarded, so it silently did
 * nothing for the life of the project while the source read as though it had
 * worked. Nothing here writes a control without checking again. */
static void ctl_set(const char *name, long val)
{
	if (ctl_io(name, &val, 1) < 0)
		fprintf(stderr, "brick: mixer rejected '%s' = %ld\n", name, val);
}

/* Codec-wide state that the volume level does not own, set once at init.
 *
 * Diatom sets this itself rather than inheriting it from the launcher because
 * it runs standalone as well as under one, and a frontend that is quiet only
 * when started the wrong way is worse than one that is simply quiet.
 * Idempotent, so both doing it costs nothing.
 *
 * HP_CTL: see the header comment above - 0 is the loud end.
 * SWAP_CTL at 1 crosses the channels. The stock hook clears it
 * (runtrimui-original.sh: `tinymix set 1 0`) and so does NextUI; we never did,
 * so left and right have been backwards the whole time. It is enumerated
 * rather than integer, but the value union overlaps and we only ever write
 * item 0, so the integer path reaches it. */
static void mixer_defaults(void)
{
	if (g_mixer_fd < 0) return;
	ctl_set(HP_CTL, 0);
	ctl_set(SWAP_CTL, 0);
}

/* Re-apply if the plug went in or came out since the last write.
 *
 * Without this the level only moves to the right window at the next volume
 * press, so plugging in mid-game leaves the old register in place - which is
 * the exact moment the difference is 9 dB and being worn on your head. One
 * ioctl on an already-open fd, and it writes nothing unless the state moved.
 *
 * The launcher does the same on its side, from plat_input_poll. Whichever
 * process is pumping input owns the level, so the two never write over each
 * other: the launcher stops polling while a game runs, and this stops when the
 * game ends. */
static int g_jack_was = -1;                /* last state written; -1 = never */
static void gain_apply(void);
/* A plug in or out seen here, not the first look after a handover
 * (g_jack_was -1), for diatom_port_level_plugged. Atomic: this poll runs in
 * the input poll, which a core with a thread of its own may call from there,
 * and the host asks from the frame loop. */
static bool g_jack_moved;

static void gain_jack_poll(void)
{
	if (jack_present() == g_jack_was || g_level < 0) return;
	if (g_jack_was >= 0) __atomic_store_n(&g_jack_moved, true, __ATOMIC_RELAXED);
	gain_apply();
}

static void gain_apply(void)
{
	bool cut = g_level <= 0 || g_muted;
	long v = cut ? GAIN_RAW_MAX : level_to_raw(g_level);
	long quiet = cut ? HP_CTL_QUIET : 0;

	g_jack_was = jack_present();
	if (gain_io(&v, 1) < 0) return;
	ctl_io(HP_CTL, &quiet, 1);

	/* Zero has to cut the path, not just attenuate it. The control advertises
	 * `mute=0`, meaning its minimum is maximum attenuation - about -74 dB -
	 * and not silence. Measured: with an ear against the speaker, level 0 is
	 * still audible, and a mic across the room cannot tell it from the room.
	 * So the speaker switch carries the last step.
	 *
	 * AND NEVER BACK ON WHILE MUTED. This line is where the launcher's mute
	 * used to die: with the switch down and a game muted, one volume press
	 * re-enabled the stage and the sound returned. The cut for level 0 is
	 * still ours; turning it on again is not, while somebody else is
	 * holding it off. */
	v = !cut;
	ctl_io(SPEAKER_CTL, &v, 1);
	/* The switch cuts the speaker only, so with headphones in neither the
	 * mute nor level 0 silenced them until 2026-09-30. They are cut by level
	 * instead: GAIN_CTL and HP_CTL both at their quiet ends, silent by ear
	 * that day. Not "Headphone Switch", tried the same day: with it and
	 * SPEAKER_CTL both off the codec stops taking samples, and the audio
	 * thread waits on it - a game froze switching to a headset until the
	 * mute came off. */
}

/* The hold's segments by now: the bar kept up while they fill, then left to
 * hold and fade as after a level step once all are lit */
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

static void osd_show(int kind, int level, int max)
{
	/* Feedback lives here too: the launcher owns UI, but it is not drawing
	 * while a game runs, so nothing else can show this. Held 1.5 s from the
	 * last press (OSDBAR_HOLD_MS), so holding a key keeps the bar up. The
	 * theme is the launcher's, in a file, read again on every press, so a
	 * change while the bar is up shows on the next one. */
	ds_theme colors;
	int theme = themefile_read_bar(THEME_FILE, &colors);
	uint64_t now = diatom_port_now_us();
	float a, rise;

	/* Coming in only when it was not already up: a press while it holds or
	 * fades keeps it where it is, as on the shelf. */
	/* Under g_mx, which the GPU path's thread reads them under */
	pthread_mutex_lock(&g_mx);
	if (!g_osd_on || !osdbar_envelope((uint32_t)((now - g_osd_shown) / 1000),
	                                  (uint32_t)((now - g_osd_changed) / 1000), &a, &rise))
		g_osd_shown = now;
	g_osd_on      = true;
	g_osd_kind    = kind;
	g_osd_level   = level;
	g_osd_max     = max;
	if (theme != g_osd_theme || memcmp(&colors.ground, &g_osd_colors.ground,
	                                   sizeof colors.ground * 18)) {
		g_osd_colors = colors;
		g_osd_colors_gen++;
	}
	g_osd_theme   = theme;
	g_osd_changed = now;
	pthread_mutex_unlock(&g_mx);
}

/* What the bar shows now: the host's look while it is the look of the bar's
 * kind, else the plain bar (POWER's hold, a kind it has not given a look for
 * yet). Under g_mx on the GPU path's thread (port/gles_present.h). */
static void osd_look_now(osdbar_look *l)
{
	const int kind = g_osd_kind == 2 ? OSDBAR_POWER : g_osd_kind ? OSDBAR_BRIGHT : OSDBAR_VOLUME;

	if (g_osd_kind != 2 && g_osd_look.kind == kind) *l = g_osd_look;
	else osdbar_look_plain(l, kind, g_osd_level, g_osd_max);
}

/* The panel at a rung, and back as the nearest rung, so a level the launcher
 * left between two reads as the closer one - exact for every level the launcher
 * can set except its black one. */
static void bright_apply(int pos)
{
	unsigned long a[4] = { 0, 0, 0, 0 };

	if (pos < 0)             pos = 0;
	if (pos > BRIGHT_LEVELS) pos = BRIGHT_LEVELS;
	a[1] = bright_ladder[pos];
	ioctl(g_disp_fd, DISP_LCD_SET_BRIGHTNESS, a);
}

static int bright_read(void)
{
	unsigned long a[4] = { 0, 0, 0, 0 };
	int raw;

	if (g_disp_fd < 0) return -1;
	raw = ioctl(g_disp_fd, DISP_LCD_GET_BRIGHTNESS, a);
	if (raw < 0) return -1;
	return ladder_nearest(bright_ladder, BRIGHT_LEVELS + 1, raw);
}

/* The low-battery disc: the one the launcher drew on the shelf until its
 * status corner replaced it (ZyperOS 87db5aa, which took out
 * draw_low_battery_dot), drawn that way: horizontal spans of a disc of radius
 * 9, centered 34 px from the top and right edges. Like the bar it goes into
 * the page after the blit, and when it goes, what it covered outside the
 * picture is put back to black - inside, the blit has already redrawn it. */
#define DOT_R  9
#define DOT_IN 34
static bool g_lowbat, g_lowbat_blink;
static bool g_dot_painted[FB_PAGES];

/* Shown this frame: always when low, the even seconds when blinking. */
static bool dot_visible(void)
{
	return g_lowbat && (!g_lowbat_blink || (diatom_port_now_us() / 1000000u) % 2 == 0);
}

/* The Border (diatom_port_border) in the pages' pixels, xres x yres, or NULL
 * for black: what lies outside the picture. The bars are painted once, when
 * the pages are (clear_pages), and whatever was drawn over a bar and comes
 * down (the low battery's dot, a notice, the level bar) puts the border's
 * pixel back where black was. Never read from a page: from here. */
static uint32_t *g_bd_px;
/* A new border to paint, at the next frame this path draws: not from the
 * host's call, which may come while the launcher shows the pages (paused) */
static bool      g_bd_repaint;

static inline uint32_t bd_px(int x, int y)
{
	return g_bd_px ? g_bd_px[(size_t)y * g_vinfo.xres + (size_t)x] : g_opaque;
}

static void dot_page(uint8_t *base, diatom_rect dst, bool draw)
{
	const unsigned ro = g_vinfo.red.offset, go = g_vinfo.green.offset;
	const unsigned bo = g_vinfo.blue.offset;
	const uint32_t red = (224u << ro) | (72u << go) | (72u << bo) | g_opaque;
	const int cx = (int)g_vinfo.xres - DOT_IN, cy = DOT_IN;
	int dy;

	for (dy = -DOT_R; dy <= DOT_R; dy++) {
		const int v = DOT_R * DOT_R - dy * dy, y = cy + dy;
		uint32_t *row = (uint32_t *)(base + (size_t)y * g_finfo.line_length);
		bool covered = y >= dst.y && y < dst.y + dst.h;
		int dx = 0, x;

		/* The launcher's span: sqrt(v) rounded, so the two discs match to the
		 * pixel. The largest dx with (2dx - 1)^2 <= 4v. */
		while ((2 * (dx + 1) - 1) * (2 * (dx + 1) - 1) <= 4 * v) dx++;
		for (x = cx - dx; x <= cx + dx; x++) {
			if (draw)
				row[x] = red;
			else if (!covered || x < dst.x || x >= dst.x + dst.w)
				row[x] = bd_px(x, y);
		}
	}
}

void diatom_port_low_battery(bool on, bool blink)
{
	g_lowbat = on;
	g_lowbat_blink = blink;
}

/* On-Screen Stats: diatom_port_hud and _hud_style are port/hud_host.h's
 * (the GPU path's section, below), drawn by either path */

/* The picture under the notice, in ordinary memory.
 *
 * Blending needs what is underneath, and reading it back out of the page was
 * the whole cost of a notice: framebuffer memory is slow to READ, and a notice
 * is almost all part-transparent pixels - 25,543 of 28,884 in an achievements
 * notice. Measured 2026-09-30 in Aladdin: 5.85 ms a frame to draw the notice
 * over a 4.14 ms picture, and in Sonic, whose picture costs 6.6 ms, enough to
 * miss frames. So while a notice is up, blit puts the rows it covers here as
 * it draws them, and the blend reads this instead of the page. */
static uint32_t *g_ov_under;
static size_t    g_ov_under_cap;     /* in pixels */
static bool      g_ov_live;          /* drawn this frame: decided once, in present */

static int ov_x0(void) { return ((int)g_vinfo.xres - g_ov_w) / 2; }
static int ov_y0(void) { return (int)g_vinfo.yres - g_ov_h - (int)g_vinfo.yres / 24; }

static void cpu_overlay(const uint8_t *bgra, int w, int h, unsigned ms)
{
	if (!bgra || ms == 0 || w <= 0 || h <= 0) {
		g_ov = NULL;
		g_ov_until = 0;
		return;
	}
	/* Refused, not clipped. Half a notice off the edge of the panel looks
	 * deliberate and is not. */
	if (w > (int)g_vinfo.xres || h > (int)g_vinfo.yres) {
		diatom_port_log(DIATOM_LOG_WARN, "overlay larger than the panel; ignored");
		return;
	}
	if ((size_t)w * (size_t)h > g_ov_under_cap) {
		uint32_t *u = realloc(g_ov_under, (size_t)w * (size_t)h * sizeof *u);

		/* Without it the notice still draws, by reading the page back -
		 * slowly, which is the cost this buffer exists to avoid. */
		if (u) { g_ov_under = u; g_ov_under_cap = (size_t)w * (size_t)h; }
	}
	g_ov = bgra;
	g_ov_w = w;
	g_ov_h = h;
	g_ov_until = diatom_port_now_us() + (uint64_t)ms * 1000ull;
}

/* Straight into the page after the blit and before publish, riding the same
 * flip as the picture - the level bar's arrangement, for the level bar's
 * reason: a second present would be a second presenter. */
static void draw_overlay(uint8_t *base)
{
	const unsigned ro = g_vinfo.red.offset, go = g_vinfo.green.offset;
	const unsigned bo = g_vinfo.blue.offset;
	const int x0 = ov_x0();
	const int y0 = ov_y0();
	int y, x;

	if (!g_ov || y0 < 0 || x0 < 0) return;

	for (y = 0; y < g_ov_h; y++) {
		uint32_t *row = (uint32_t *)(base + (size_t)(y0 + y) * g_finfo.line_length);
		const uint8_t *src = g_ov + (size_t)y * (size_t)g_ov_w * 4;
		const uint32_t *under = g_ov_under ? g_ov_under + (size_t)y * (size_t)g_ov_w
		                                   : NULL;

		for (x = 0; x < g_ov_w; x++) {
			const unsigned a = src[x * 4 + 3];
			uint32_t p;
			unsigned r, g, b;

			if (a == 0) continue;
			p = under ? under[x] : row[x0 + x];
			if (a == 255) {
				r = src[x * 4 + 2];
				g = src[x * 4 + 1];
				b = src[x * 4 + 0];
			} else {
				/* Straight alpha, rounded. The source is not premultiplied
				 * because the launcher renders it with SDL, which is not. */
				r = (src[x * 4 + 2] * a + ((p >> ro) & 0xff) * (255 - a) + 127) / 255;
				g = (src[x * 4 + 1] * a + ((p >> go) & 0xff) * (255 - a) + 127) / 255;
				b = (src[x * 4 + 0] * a + ((p >> bo) & 0xff) * (255 - a) + 127) / 255;
			}
			row[x0 + x] = (r << ro) | (g << go) | (b << bo) | g_opaque;
		}
	}
}

/* Where a notice was last drawn on each page, so it can be taken off again.
 *
 * The picture is redrawn every frame; the letterbox bars are not - they are
 * painted once, when the mode changes. So the part of a notice that falls on
 * a bar stays there after it comes down, which Eric saw in integer mode on
 * 2026-09-30: the old level bar's bug in the margins, for the notice. Per
 * page, because each of the three carries its own copy, and per rect, because
 * the next notice may be a different size. */
static diatom_rect g_ov_painted[FB_PAGES];

/* Black wherever `r` lies outside the picture. Inside it, the next blit has
 * already redrawn it. Writes only - the page is never read. */
static void clear_overlay(uint8_t *base, diatom_rect r, diatom_rect dst)
{
	int y, x;

	for (y = r.y; y < r.y + r.h; y++) {
		uint32_t *row = (uint32_t *)(base + (size_t)y * g_finfo.line_length);
		bool covered = y >= dst.y && y < dst.y + dst.h;

		for (x = r.x; x < r.x + r.w; x++) {
			if (covered && x >= dst.x && x < dst.x + dst.w) continue;
			row[x] = bd_px(x, y);
		}
	}
}

/* The notice's part of one page's present: draw it, or take off what an
 * earlier one left. */
static void overlay_page(uint8_t *base, int page, diatom_rect dst)
{
	diatom_rect now = { ov_x0(), ov_y0(), g_ov_w, g_ov_h };
	diatom_rect *was = &g_ov_painted[page];

	if (was->w > 0 && (!g_ov_live || memcmp(was, &now, sizeof now) != 0)) {
		clear_overlay(base, *was, dst);
		was->w = 0;
	}
	if (g_ov_live && now.x >= 0 && now.y >= 0) {
		draw_overlay(base);
		*was = now;
	}
}

/* The level bar: the launcher's own pill, from the same code (osdbar.c, which
 * is ZyperOS's src/osdbar.c), so a volume press looks the same in a game as on
 * the shelf, over a port and in a game on the Pixel 2. At the launcher's
 * scale: the panel's height over its 768 units, whatever the panel's width
 * (ZyperOS's src/canvas.h). The Brick's 1024x768 is one pixel a unit, so it
 * is drawn at scale 1: 476 x 92 pixels with its shadow, the pill 452 x 64 at
 * 22 under the top edge, centered; the Smart Pro's 1280x720 at 0.9375. Drawn into ordinary memory, premultiplied, and again only when
 * the level, the fade or the theme moves; the slide is where it goes on the
 * page.
 *
 * Into the page after the blit and before publish, riding the same flip as
 * the picture, and blended over what blit kept of the picture under it
 * (g_bar_under), never over the page: framebuffer memory is slow to read,
 * which is the notice's lesson, and the pill is almost all part-transparent
 * pixels, its body being surface at 93%. Where it is this frame is decided
 * once, in present, before the blit. */
static uint32_t    g_bar_px[OSDBAR_W * OSDBAR_H];     /* the bar, g_bar_w x g_bar_h */
static uint32_t    g_bar_under[OSDBAR_W * OSDBAR_H];  /* the picture under g_bar_rect */
static int         g_bar_w, g_bar_h;
static int         g_bar_x, g_bar_y;   /* its top left on the panel, y < 0 sliding in */
static bool        g_bar_live;         /* drawn this frame */
static diatom_rect g_bar_rect;         /* the part of it on the panel */

/* Where the bar was last drawn on each page, so it can be taken off again:
 * the notice's reason (g_ov_painted), since blit never writes the margins,
 * where the old bar stayed as two stubs in a pillarbox. A rect, because the
 * slide moves it. */
static diatom_rect g_bar_painted[FB_PAGES];

/* This frame's bar, before the blit: whether it is up, where, and its pixels,
 * as port/pixel2.c decides them on its render thread. Drawn again on a change
 * of the opacity itself rather than of its byte: a frame 96 ms into the fade
 * in is at 0.9997, which rounds to the byte 1 does, and the bar would be held
 * as it was drawn there, a level off in some 19,000 pixels. g_bar_under
 * starts black, as the page is where the picture does not reach. */
static void bar_frame(void)
{
	static osdbar_look drawn;
	static bool drawn_any;
	static int drawn_theme;
	static float drawn_alpha;
	static unsigned colors_seen;
	const float s = (float)g_vinfo.yres / 768.0f;
	const uint64_t now = diatom_port_now_us();
	osdbar_look look;
	float alpha, rise;
	diatom_rect r;
	size_t i, n;

	g_bar_live = false;
	hold_tick(now);
	if (!g_osd_on) return;
	if (!osdbar_envelope((uint32_t)((now - g_osd_shown) / 1000),
	                     (uint32_t)((now - g_osd_changed) / 1000), &alpha, &rise)) {
		g_osd_on = false;
		return;
	}
	if (s <= 0 || s > 1) return;
	/* The look's colors, handed to the bar once each time they move */
	if (colors_seen != g_osd_colors_gen) {
		osdbar_colors(g_osd_theme, &g_osd_colors);
		colors_seen = g_osd_colors_gen;
		drawn_any = false;
	}
	/* The look (the level, its mark moving, the boost past the top) painted
	 * again when it moves; the fade alone is osdbar_render_look's
	 * conversion of what it painted last */
	osd_look_now(&look);
	if (!drawn_any || memcmp(&look, &drawn, sizeof look) ||
	    g_osd_theme != drawn_theme || alpha != drawn_alpha) {
		osdbar_size(s, &g_bar_w, &g_bar_h);
		osdbar_render_look(g_bar_px, g_bar_w, g_bar_h, g_bar_w, s, g_osd_theme, &look,
		                   alpha, true);
		drawn = look;
		drawn_any = true;
		drawn_theme = g_osd_theme;
		drawn_alpha = alpha;
	}
	/* In the panel's middle: OSDBAR_X is that on a 4:3 one */
	g_bar_x = ((int)g_vinfo.xres - g_bar_w) / 2;
	g_bar_y = (int)roundf((OSDBAR_Y - rise) * s);
	r.x = g_bar_x > 0 ? g_bar_x : 0;
	r.y = g_bar_y > 0 ? g_bar_y : 0;
	r.w = (g_bar_x + g_bar_w < (int)g_vinfo.xres ? g_bar_x + g_bar_w
	                                              : (int)g_vinfo.xres) - r.x;
	r.h = (g_bar_y + g_bar_h < (int)g_vinfo.yres ? g_bar_y + g_bar_h
	                                              : (int)g_vinfo.yres) - r.y;
	if (r.w <= 0 || r.h <= 0) return;
	n = (size_t)r.w * (size_t)r.h;
	for (i = 0; i < n; i++) g_bar_under[i] = bd_px(r.x + (int)(i % (size_t)r.w), r.y + (int)(i / (size_t)r.w));
	g_bar_rect = r;
	g_bar_live = true;
}

/* The bar over what blit kept under it. Premultiplied, so a pixel is the bar's
 * color plus what is under it times what the bar leaves of it. Inside the
 * picture a pixel the bar does not touch is left as the blit drew it; outside,
 * every pixel of the rect is written, black where the bar is not, so a fade
 * leaves nothing behind in the margin. Writes only - the page is never read. */
static void draw_bar(uint8_t *base, diatom_rect dst)
{
	const unsigned ro = g_vinfo.red.offset, go = g_vinfo.green.offset;
	const unsigned bo = g_vinfo.blue.offset;
	const diatom_rect r = g_bar_rect;
	int y, x;

	for (y = r.y; y < r.y + r.h; y++) {
		uint32_t *row = (uint32_t *)(base + (size_t)y * g_finfo.line_length) + r.x;
		const uint32_t *src = g_bar_px + (size_t)(y - g_bar_y) * (size_t)g_bar_w
		                    + (r.x - g_bar_x);
		const uint32_t *under = g_bar_under + (size_t)(y - r.y) * (size_t)r.w;
		const bool covered = y >= dst.y && y < dst.y + dst.h;
		/* The picture's columns in this row, counted from r.x. */
		const int in0 = covered ? dst.x - r.x : 0;
		const int in1 = covered ? dst.x + dst.w - r.x : 0;

		for (x = 0; x < r.w; x++) {
			const uint32_t p = src[x], u = under[x];
			const unsigned a = p >> 24, k = 255 - a;
			unsigned cr, cg, cb;

			if (a == 0 && x >= in0 && x < in1) continue;
			cr = ((p >> 16) & 0xff) + (((u >> ro) & 0xff) * k + 127) / 255;
			cg = ((p >>  8) & 0xff) + (((u >> go) & 0xff) * k + 127) / 255;
			cb = ( p        & 0xff) + (((u >> bo) & 0xff) * k + 127) / 255;
			/* A channel past its alpha by a float's rounding would carry
			 * into the next one. */
			if (cr > 255) cr = 255;
			if (cg > 255) cg = 255;
			if (cb > 255) cb = 255;
			row[x] = (cr << ro) | (cg << go) | (cb << bo) | g_opaque;
		}
	}
}

/* The bar's part of one page's present: draw it, or take off what an earlier
 * frame left, the way overlay_page does the notice's. */
static void bar_page(uint8_t *base, int page, diatom_rect dst)
{
	diatom_rect *was = &g_bar_painted[page];

	if (was->w > 0 &&
	    (!g_bar_live || memcmp(was, &g_bar_rect, sizeof g_bar_rect) != 0)) {
		clear_overlay(base, *was, dst);
		was->w = 0;
	}
	if (g_bar_live) {
		draw_bar(base, dst);
		*was = g_bar_rect;
	}
}

/* Wait until the mailbox is empty and no pan is in flight, and report the page
 * left on glass. The flip thread keeps running - callers want quiescence, not
 * teardown.
 *
 * Two callers, for the same underlying reason. A capture must read the page
 * that is actually displayed rather than whichever one the thread last got to,
 * or the same run at the same frame count yields different images. And a
 * handover must leave nothing in flight before another process starts panning
 * the same framebuffer, or the display switches between their page and ours
 * mid-refresh. */
static int flip_drain(void)
{
	int front;

	pthread_mutex_lock(&g_flip_mx);
	while (g_flip_running && (g_page_pending >= 0 || g_page_inflight >= 0))
		pthread_cond_wait(&g_flip_idle, &g_flip_mx);
	front = g_page_front;
	pthread_mutex_unlock(&g_flip_mx);
	return front;
}

static uint8_t *page_base(int page)
{
	return g_fb + (size_t)page * g_vinfo.yres * g_finfo.line_length;
}

static void clear_pages(void);

/* The last frame parked where a launcher's two pages are not, after a stop
 * that had presented: the CPU path's, and the GPU path's give_display */
static void park(diatom_park park_mode, int front)
{
	int pg;
	struct fb_var_screeninfo v;

	/* Quiescent is necessary and not sufficient.
	 *
	 * The launcher renders into the SAME framebuffer - measured from its side,
	 * the shelf lands in fb0 with every pixel opaque, so there is one buffer
	 * and not two composited layers. This port uses three pages; a launcher
	 * double-buffering through GL uses two, from yoffset 0 upward. Those
	 * overlap. So a handover that stops on page 0 or 1 leaves the panel
	 * scanning out a page the launcher is about to draw its shelf into, and
	 * the result is interleaved bands of game and shelf - which is what the
	 * 240fps capture showed, rather than the single sweeping boundary two
	 * panners would make.
	 *
	 * Park on the TOP page, which a two-page consumer does not reach. This is
	 * a mitigation with a stated assumption, not a guarantee: nothing here can
	 * stop another process rendering into whatever page it likes. What it does
	 * is make the common case - a fresh consumer taking the low pages - safe,
	 * for the cost of one pan.
	 *
	 * The content parked is up to two frames old in a three-page rotation. At
	 * a handover the last frames are a paused or ending game, so that is
	 * imperceptible, and a stale frame for 30ms is a better trade than a torn
	 * one. */
	if (g_pages < 2 || g_fb_fd < 0) return;
	pg = g_pages - 1;

	if (park_mode == DIATOM_PARK_BLANK) {
		/* Opaque black, never a memset to zero: a zero alpha byte makes the
		 * pixel invisible on this panel rather than black, which is the trap
		 * documented at g_opaque and cost a day in 2026-08-24. */
		uint32_t *q = (uint32_t *)page_base(pg);
		size_t n = (size_t)g_vinfo.yres * g_finfo.line_length / sizeof *q, i;

		for (i = 0; i < n; i++) q[i] = g_opaque;
	} else {
		if (front == pg) return;
		memcpy(page_base(pg), page_base(front),
		       (size_t)g_vinfo.yres * g_finfo.line_length);
	}

	v = g_vinfo;
	v.yoffset  = (uint32_t)pg * v.yres;
	v.activate = FB_ACTIVATE_VBL;
	if (ioctl(g_fb_fd, FBIOPAN_DISPLAY, &v) != 0) {
		diatom_port_log(DIATOM_LOG_WARN, "pan to park page failed");
		return;
	}
	pthread_mutex_lock(&g_flip_mx);
	g_page_front = pg;
	pthread_mutex_unlock(&g_flip_mx);
}

static void cpu_present_stop(diatom_park park_mode)
{
	int front;
	bool was_presenting;

	if (!g_fb) return;
	front = flip_drain();

	was_presenting = g_presented;
	g_presented = false;

	/* Nothing presented since the last stop means somebody else has the
	 * display, and parking would TAKE it - which is what happens when a
	 * player quits from the launcher's in-game menu. Diatom paused at MENU,
	 * the launcher has been drawing its menu ever since, and a park here
	 * seized the panel to show a black page for 150ms before the launcher
	 * got it back. Measured: a pan to the park page landing between two of
	 * the launcher's own, with nothing of Diatom's on screen either side.
	 *
	 * Draining still matters - it is cheap and it costs nothing to be sure
	 * the flip thread is idle - but the pan does not happen. */
	if (!was_presenting) return;
	park(park_mode, front);
}


/* The refreshes, for the frame loop's display lock (diatom_port_vblank,
 * Timing's Smooth): the panel's rate from fb0's timings (panel_refresh_hz),
 * and from the flip thread's pans, which return at the refresh that latched
 * them, the last one's time and the period, which starts at the timings' and
 * moves a thirty-second of each pan's difference, as the Pixel 2's moves with
 * its flips. Written by the flip thread, read by the loop: a word each. */
static volatile uint64_t g_vbl_at;
static volatile double   g_vbl_period, g_vbl_nominal;

static void vbl_mark(uint64_t at)
{
	static uint64_t prev;

	if (prev && g_vbl_nominal > 0 && at > prev) {
		double d = (double)(at - prev);
		long k = lround(d / g_vbl_nominal);

		/* A pan one to four refreshes after the last, its period within 2%
		 * of the timings': a wake-up late or a frame skipped moves nothing */
		if (k >= 1 && k <= 4) {
			double one = d / (double)k;

			if (fabs(one - g_vbl_nominal) < g_vbl_nominal * 0.02)
				g_vbl_period += (one - g_vbl_period) / 32;
		}
	}
	prev = at;
	g_vbl_at = at;
}

/* The panel's refresh rate from fb0's timings, Hz: the pixel clock (Allwinner's
 * dev_fb.c fills pixclock, in picoseconds, and the porches and syncs from the
 * LCD's timings) over the whole frame's pixels; 0 when they are missing or
 * make no sense */
static double panel_refresh_hz(void)
{
	const struct fb_var_screeninfo *v = &g_vinfo;
	double htotal = (double)v->xres + v->left_margin + v->right_margin + v->hsync_len;
	double vtotal = (double)v->yres + v->upper_margin + v->lower_margin + v->vsync_len;
	double hz;

	if (!v->pixclock || !v->left_margin || !v->upper_margin) return 0;
	hz = 1e12 / ((double)v->pixclock * htotal * vtotal);
	return hz >= 30 && hz <= 120 ? hz : 0;
}

/* The rate and where it came from, once at the start */
static void refresh_log(double hz)
{
	char msg[200];

	if (hz > 0)
		snprintf(msg, sizeof msg, "brick: the panel refreshes at %.3f Hz (fb0's timings: "
		         "pixclock %u ps, %ux%u of %ux%u)", hz, g_vinfo.pixclock, g_vinfo.xres, g_vinfo.yres,
		         g_vinfo.xres + g_vinfo.left_margin + g_vinfo.right_margin + g_vinfo.hsync_len,
		         g_vinfo.yres + g_vinfo.upper_margin + g_vinfo.lower_margin + g_vinfo.vsync_len);
	else
		snprintf(msg, sizeof msg, "brick: the panel's rate is not in fb0's timings "
		         "(pixclock %u ps); no display lock", g_vinfo.pixclock);
	diatom_port_log(DIATOM_LOG_INFO, msg);
}

bool diatom_port_vblank(uint64_t *at_us, double *period_us)
{
	uint64_t at = g_vbl_at;

	if (!at || g_vbl_period <= 0) return false;
	*at_us = at;
	*period_us = g_vbl_period;
	return true;
}

static void *flip_worker(void *arg)
{
	struct fb_var_screeninfo v = g_vinfo;

	pthread_mutex_lock(&g_flip_mx);
	while (!g_flip_stop) {
		int page;

		if (g_page_pending < 0) {
			pthread_cond_wait(&g_flip_cv, &g_flip_mx);
			continue;
		}
		page = g_page_pending;
		g_page_pending  = -1;
		g_page_inflight = page;
		pthread_mutex_unlock(&g_flip_mx);

		/* Blocks until the address latches at a vsync - the whole reason
		 * this thread exists. */
		v.yoffset  = (uint32_t)page * v.yres;
		v.activate = FB_ACTIVATE_VBL;
		if (ioctl(g_fb_fd, FBIOPAN_DISPLAY, &v) != 0)
			diatom_port_log(DIATOM_LOG_WARN, "pan failed in flip thread");
		else
			vbl_mark(diatom_port_now_us());

		pthread_mutex_lock(&g_flip_mx);
		g_page_front    = page;
		g_page_inflight = -1;
		pthread_cond_broadcast(&g_flip_idle);
	}
	pthread_mutex_unlock(&g_flip_mx);
	return arg;
}

/* ---------- the audio sink ------------------------------------------------ */

/* What is open now. Empty means the default device; it is also what the host
 * is told, so "" reads as "wherever this device sends sound by default" rather
 * than as a name nobody recognizes. */
static char g_audio_dev[128];

/* Closing an SDL audio device JOINS its audio thread, and that thread can be
 * blocked writing to a sink that has stopped draining. So this call can fail to
 * return, and on 2026-09-05 a stalled A2DP sink did exactly that.
 *
 * The obvious fix - close on a detached thread - was tried and REVERTED the
 * same day. SDL is not safe against opening a device while a close of another
 * is in flight: test/port_test.c fails three ways with it, the first being that
 * the fallback open after a refused device stops working. A frontend that
 * cannot reopen its audio is worse than one that can hang on a dead headset.
 *
 * The hang is also not really here. The frame loop calls SDL_QueueAudio and
 * SDL_GetQueuedAudioSize every frame, and those take the same device lock the
 * audio thread holds while it writes - so a stalled sink stops the loop whether
 * or not anything closes. Fixing that means not letting the write stall, which
 * is the sink question, not the teardown question.
 */
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
	/* allowed_changes 0: SDL hands back exactly this spec and converts behind
	 * it, so a sink running at another rate never reaches the resampler and
	 * caps.audio_rate stays true across a reopen. */
	g_audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
	if (!g_audio) return false;
	SDL_PauseAudioDevice(g_audio, 0);
	/* What SDL actually negotiated, not what was asked for. A sink can open
	 * cleanly and then not carry sound, and when that happened on 2026-09-05
	 * there was no way to tell from outside whether SDL had agreed to
	 * something the device could not service. */
	{
		char msg[192];

		snprintf(msg, sizeof msg,
		         "audio: opened %s at %d Hz, %d ch, %d frames/period, %u byte buffer",
		         name && *name ? name : "the default device",
		         have.freq, have.channels, have.samples, have.size);
		diatom_port_log(DIATOM_LOG_INFO, msg);
	}
	return true;
}

bool diatom_port_audio_set(const char *name, char *actual, size_t cap)
{
	const char *want = name ? name : "";
	bool ok;

	/* Already there. A write on the state plane is idempotent, so re-stating
	 * the current device must not close and reopen a working sink: that drops
	 * whatever is queued and costs a fresh A2DP stream setup. Repeated writes
	 * are not hypothetical - a launcher that recomputes its routing on a timer
	 * sends one whenever it thinks the answer might have moved. */
	if (g_audio && !strcmp(want, g_audio_dev)) {
		diatom_port_audio_get(actual, cap);
		return true;
	}

	ok = audio_open(name);

	if (!ok) {
		char msg[192];

		snprintf(msg, sizeof msg, "audio: %s unavailable (%s); using the default",
		         name && *name ? name : "the default device", SDL_GetError());
		diatom_port_log(DIATOM_LOG_WARN, msg);
		/* The default, and if even that will not open, silence - which the
		 * write path already handles. Never a reason to stop. */
		if (audio_open(NULL)) g_audio_dev[0] = '\0';
	} else {
		snprintf(g_audio_dev, sizeof g_audio_dev, "%s", name ? name : "");
	}
	diatom_port_audio_get(actual, cap);
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
	if (g_audio) return true;
	if (audio_open(NULL)) {
		g_audio_dev[0] = '\0';
		return true;
	}
	fprintf(stderr, "audio unavailable (%s); continuing without sound\n", SDL_GetError());
	return false;
}

void diatom_port_audio_get(char *out, size_t cap)
{
	if (out && cap) snprintf(out, cap, "%s", g_audio_dev);
}

bool diatom_port_init(diatom_port_caps *out)
{

	g_mixer_fd = open("/dev/snd/controlC0", O_RDWR);
	g_disp_fd  = open("/dev/disp", O_RDWR);
	mixer_defaults();
	jack_open();
	g_input_debug   = getenv("DIATOM_INPUT_DEBUG") != NULL;
	g_present_debug = getenv("DIATOM_PRESENT_DEBUG") != NULL;

	/* No SDL_INIT_VIDEO: presentation does not go through SDL at all, and the
	 * mali video driver would otherwise claim the display. */
	/* Audio is initialised SEPARATELY and is allowed to fail. A subsystem that
	 * will not come up must not take the rest of the port with it - see the
	 * open below for why silence beats refusing to start. Rolled into one
	 * SDL_Init, a missing or busy audio device is indistinguishable from a
	 * missing display and kills both. */
	if (SDL_Init(SDL_INIT_EVENTS) != 0) {
		fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
		return false;
	}

	g_fb_fd = open("/dev/fb0", O_RDWR);
	if (g_fb_fd < 0) { perror("open /dev/fb0"); return false; }
	if (ioctl(g_fb_fd, FBIOGET_FSCREENINFO, &g_finfo) != 0 ||
	    ioctl(g_fb_fd, FBIOGET_VSCREENINFO, &g_vinfo) != 0) {
		perror("FBIOGET_SCREENINFO");
		return false;
	}
	if (g_vinfo.bits_per_pixel != 32) {
		/* The panel runs 32bpp (measured); anything else means the display
		 * setup changed and this port needs to learn the new format, not
		 * guess at it. */
		fprintf(stderr, "fb0 is %ubpp, port expects 32\n", g_vinfo.bits_per_pixel);
		return false;
	}

	g_pages = (int)(g_finfo.smem_len / ((size_t)g_vinfo.yres * g_finfo.line_length));
	if (g_pages > FB_PAGES) g_pages = FB_PAGES;
	if (g_pages < 1) { fprintf(stderr, "fb0 too small for one page\n"); return false; }

	g_fb_size = (size_t)g_pages * g_vinfo.yres * g_finfo.line_length;
	g_fb = mmap(NULL, g_fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, g_fb_fd, 0);
	if (g_fb == MAP_FAILED) { perror("mmap fb0"); g_fb = NULL; return false; }

	if (g_vinfo.transp.length > 0)
		g_opaque = ((1u << g_vinfo.transp.length) - 1) << g_vinfo.transp.offset;

	/* Opaque black, not memset zero: zero alpha is invisible, see g_opaque. */
	clear_pages();

	/* Start from a known page. The mailbox needs three pages - front, in
	 * flight, drawing; fewer, or a refused pan, means single-buffered
	 * drawing straight to glass: can tear, still works. */
	g_vinfo.yoffset = 0;
	if (ioctl(g_fb_fd, FBIOPAN_DISPLAY, &g_vinfo) != 0 || g_pages < 3) {
		g_pan_broken = true;
		diatom_port_log(DIATOM_LOG_WARN, "fb0 pan or pages unavailable; single-buffered");
	} else if (pthread_create(&g_flip_thread, NULL, flip_worker, NULL) != 0) {
		g_pan_broken = true;
		diatom_port_log(DIATOM_LOG_WARN, "no flip thread; single-buffered");
	} else {
		g_flip_running = true;
	}
	g_page_front = 0;

	/* No audio here: it opens once the launcher has drawn, or at the first
	 * game; see diatom_port_audio_start. */

	input_open();
	/* The GPU path last: it needs the pages and the flip thread */
	gl_init();

	{
		char msg[160];
		snprintf(msg, sizeof msg,
		         "brick: fb %ux%u stride %u, %d page(s), rgba at %u/%u/%u/%u+%u, audio %d Hz, gamepad %s",
		         g_vinfo.xres, g_vinfo.yres, g_finfo.line_length, g_pages,
		         g_vinfo.red.offset, g_vinfo.green.offset, g_vinfo.blue.offset,
		         g_vinfo.transp.offset, g_vinfo.transp.length,
		         AUDIO_RATE, g_pad_fd >= 0 ? "found" : "none");
		diatom_port_log(DIATOM_LOG_INFO, msg);
	}

	out->surface_w           = (int)g_vinfo.xres;
	out->surface_h           = (int)g_vinfo.yres;
	/* What is asked of SDL, which converts behind it, so the rate holds
	 * whatever device opens later; and the buffer is the port's own, open
	 * or not yet (see diatom_port_audio_start). */
	out->audio_rate          = AUDIO_RATE;
	out->audio_buffer_frames = AUDIO_BUFFER_FRAMES;
	out->present_blocks      = false;
	out->gpu                 = g_gl_possible;
	out->refresh_hz          = panel_refresh_hz();
	if (out->refresh_hz > 0) g_vbl_nominal = g_vbl_period = 1e6 / out->refresh_hz;
	refresh_log(out->refresh_hz);
	out->rumble              = false;
	return true;
}

void diatom_port_shutdown(void)
{
	int i;

	/* The GPU path first: its thread hands pages to the flip thread */
	gl_quit();
	for (i = 0; i < MAILBOX_FRAMES; i++) free(g_frames[i].pixels);
	if (g_flip_running) {
		pthread_mutex_lock(&g_flip_mx);
		g_flip_stop = true;
		pthread_cond_signal(&g_flip_cv);
		pthread_mutex_unlock(&g_flip_mx);
		pthread_join(g_flip_thread, NULL);
	}
	free(g_colmap);
	free(g_rowmap);
	free(g_ov_under);
	g_ov_under = NULL;
	g_ov_under_cap = 0;
	/* Hand the speaker back on, unless somebody is holding it off.
	 *
	 * Muting at level 0 switches HpSpeaker off, and that is device state which
	 * outlives this process. Leaving it off used to strand the device, because
	 * nothing else drove this switch - the launcher drove `digital volume` and
	 * turning the volume up there could not undo it, so the machine simply
	 * appeared to have lost its speaker.
	 *
	 * THAT REASONING EXPIRED on 2026-09-16. ZyperOS reads a hardware mute
	 * switch and drives this control itself, so the device is no longer
	 * stranded by an off speaker - and handing it back unconditionally
	 * would un-mute a device whose switch is still down, at the exact
	 * moment a game ends and the launcher's shelf comes back.
	 *
	 * The volume LEVEL is still deliberately not restored - that is a user
	 * setting and belongs wherever they left it. */
	if (g_mixer_fd >= 0) {
		long on = !g_muted;
		ctl_io(SPEAKER_CTL, &on, 1);
		close(g_mixer_fd);
		g_mixer_fd = -1;
	}
	/* Brightness is NOT restored: it is a user setting and the launcher
	 * re-applies its own on resume anyway. Unlike the speaker switch, leaving
	 * it strands nothing - the launcher's own control can always move it. */
	if (g_disp_fd >= 0) { close(g_disp_fd); g_disp_fd = -1; }
	if (g_fb)         munmap(g_fb, g_fb_size);
	if (g_fb_fd >= 0) close(g_fb_fd);
	if (g_pad_fd >= 0) close(g_pad_fd);
	if (g_audio)      SDL_CloseAudioDevice(g_audio);
	SDL_Quit();
}

/* One axis of the resampling map.
 *
 * Sharp-bilinear: take the bilinear weight, then steepen the ramp by the scale
 * factor so the blend spans one DESTINATION pixel rather than one source
 * pixel. Interior pixels come out exact and only the pixel straddling a source
 * boundary is mixed, which is why it looks like integer scaling with the
 * unevenness taken out rather than like a blur.
 *
 * At an integer factor the steepened weight lands on 0 or 1 for every pixel,
 * so the output is identical to nearest by construction - the `integer-sharp`
 * preset exists to demonstrate exactly that.
 *
 * Doubles here are free: this runs once per geometry or filter change, and the
 * per-pixel work uses only the integers it produces. */
static diatom_tap *build_map(int src, int dst, diatom_filter filter)
{
	diatom_tap *m;
	double scale;
	int i;

	if (src <= 0 || dst <= 0) return NULL;
	m = malloc((size_t)dst * sizeof *m);
	if (!m) return NULL;
	scale = (double)dst / (double)src;

	for (i = 0; i < dst; i++) {
		double c  = ((double)i + 0.5) / scale - 0.5;   /* source coordinate */
		int    p  = (int)floor(c);
		double fr = c - (double)p;

		/* Sharp-Shimmerless, as pixel2.c's SHIMMERLESS_SHADER: the source
		 * pixels under this pixel's two edges, and where they differ, the
		 * right one's share of the pixel's area. The old screens (the CRT,
		 * scanlines, the LCD grid) are shaders this CPU path does not draw,
		 * and come out as this, the closest it has. */
		if (filter >= DIATOM_FILTER_SHIMMERLESS) {
			int l = (int)floor((double)i / scale);
			int r = (int)floor((double)(i + 1) / scale);

			p  = r > l ? r - 1 : l;
			fr = r > l ? 1.0 - (scale * (double)r - (double)i) : 0.0;
			if (fr < 0.0) fr = 0.0;
			if (fr > 1.0) fr = 1.0;
			if (p >= src - 1) { p = src - 1; fr = 0.0; }
			if (p < 0) p = 0;
			m[i].idx = p;
			m[i].w   = (int)(fr * 256.0 + 0.5);
			continue;
		}

		fr = (fr - 0.5) * scale + 0.5;
		if (fr < 0.0) fr = 0.0;
		if (fr > 1.0) fr = 1.0;

		/* Clamp at the edges: no pixel outside the source to blend towards. */
		if (p < 0)        { p = 0;       fr = 0.0; }
		if (p >= src - 1) { p = src - 1; fr = 0.0; }

		if (filter == DIATOM_FILTER_NEAREST) {
			if (fr >= 0.5 && p + 1 < src) p++;
			fr = 0.0;
		}

		m[i].idx = p;
		m[i].w   = (int)(fr * 256.0 + 0.5);
		if (m[i].w > 256) m[i].w = 256;
	}

	/* Collapse a map whose every weight is 0 or 256 onto the fast path. At a
	 * whole factor sharp-bilinear is a no-op by construction, and this makes it
	 * a no-op in cost too: measured, sharp on an integer rect was paying 2.4 ms
	 * a frame to compute the same pixels. */
	for (i = 0; i < dst; i++)
		if (m[i].w != 0 && m[i].w != 256) return m;
	for (i = 0; i < dst; i++)
		if (m[i].w == 256) { m[i].idx++; m[i].w = 0; }
	return m;
}

static bool ensure_maps(int src_w, int src_h, diatom_rect dst,
                        diatom_filter filter)
{
	if (g_map_valid && src_w == g_map_src_w && src_h == g_map_src_h &&
	    filter == g_map_filter && !memcmp(&dst, &g_map_dst, sizeof dst))
		return true;

	free(g_colmap);
	free(g_rowmap);
	g_colmap = build_map(src_w, dst.w, filter);
	g_rowmap = build_map(src_h, dst.h, filter);
	g_map_valid = g_colmap && g_rowmap;
	if (!g_map_valid) return false;

	g_map_src_w = src_w;
	g_map_src_h = src_h;
	g_map_dst   = dst;
	g_map_filter = filter;
	return true;
}

/* Converted source rows, so a source pixel is converted ONCE rather than once
 * per destination pixel that samples it.
 *
 * Measured 2026-08-25: at 256x224 into 1024x768 the naive loop costs 7.52 ms
 * and this costs 3.37 ms, a 2.23x saving, because 4x horizontal and 3.4x
 * vertical scaling meant roughly thirteen redundant conversions of every source
 * pixel. The same measurement showed framebuffer memory is exactly as fast as
 * ordinary heap - 418 MB/s either way - so the cost was never the write, and
 * the disp2 hardware scaler would have been solving the wrong problem.
 *
 * Two slots because a blended row needs the source row above and below. Rows
 * are looked up by index, so consecutive destination rows sampling the same
 * source row convert nothing at all. */
#define ROWCACHE_MAX 2048     /* widest source in the matrix is 512 (SNES hires) */
static uint32_t g_rc[2][ROWCACHE_MAX];
static int      g_rc_row[2] = { -1, -1 };

static const uint32_t *cache_row(const void *src, size_t pitch, diatom_pixfmt fmt,
                                 int row, int src_w)
{
	const unsigned ro = g_vinfo.red.offset;
	const unsigned go = g_vinfo.green.offset;
	const unsigned bo = g_vinfo.blue.offset;
	const uint32_t opaque = g_opaque;
	uint32_t *dst;
	int slot, x;

	if (g_rc_row[0] == row) return g_rc[0];
	if (g_rc_row[1] == row) return g_rc[1];

	/* Evict the older row. Destination rows advance monotonically, so the
	 * lower index is the one that will not be wanted again. */
	slot = (g_rc_row[0] <= g_rc_row[1]) ? 0 : 1;
	dst  = g_rc[slot];

	if (fmt == DIATOM_PIX_RGB565) {
		const uint16_t *in = (const uint16_t *)((const uint8_t *)src + (size_t)row * pitch);
		for (x = 0; x < src_w; x++) {
			const uint16_t c = in[x];
			const uint32_t r = (c >> 11) & 0x1f, g = (c >> 5) & 0x3f, b = c & 0x1f;
			dst[x] = opaque
			       | (((r << 3) | (r >> 2)) << ro)
			       | (((g << 2) | (g >> 4)) << go)
			       | (((b << 3) | (b >> 2)) << bo);
		}
	} else {
		const uint32_t *in = (const uint32_t *)((const uint8_t *)src + (size_t)row * pitch);
		for (x = 0; x < src_w; x++) {
			const uint32_t c = in[x];
			dst[x] = opaque
			       | (((c >> 16) & 0xff) << ro)
			       | (((c >>  8) & 0xff) << go)
			       | (( c        & 0xff) << bo);
		}
	}
	g_rc_row[slot] = row;
	return dst;
}

static inline diatom_rgb unpack(uint32_t p)
{
	diatom_rgb v;
	v.r = (int)((p >> g_vinfo.red.offset)   & 0xff);
	v.g = (int)((p >> g_vinfo.green.offset) & 0xff);
	v.b = (int)((p >> g_vinfo.blue.offset)  & 0xff);
	return v;
}

static inline diatom_rgb mix(diatom_rgb a, diatom_rgb b, int w)
{
	diatom_rgb v;
	v.r = a.r + (((b.r - a.r) * w) >> 8);
	v.g = a.g + (((b.g - a.g) * w) >> 8);
	v.b = a.b + (((b.b - a.b) * w) >> 8);
	return v;
}

/* One panel row in ordinary memory, for the rows under a notice. */
#define UNDER_ROW_MAX 4096
static uint32_t g_under_row[UNDER_ROW_MAX];

/* Scale-blit src into the destination rect of one page, converting to the
 * framebuffer's own channel order (offsets read from the driver, not assumed).
 *
 * CLIPS rather than refuses: a fill or overscale mode hands over a rect larger
 * than the panel on purpose, and dropping the frame would be the wrong answer
 * to a deliberate crop. */
static void blit(uint8_t *page, const void *src, int w, int h, size_t pitch,
                 diatom_pixfmt fmt, diatom_rect dst)
{
	const unsigned ro = g_vinfo.red.offset;
	const unsigned go = g_vinfo.green.offset;
	const unsigned bo = g_vinfo.blue.offset;
	const uint32_t opaque = g_opaque;
	int x0, x1, y0, y1, y, uy0 = 0, ux0 = 0;
	bool under, bar_under;

#define PACK(c) (opaque | ((uint32_t)(c).r << ro) \
                        | ((uint32_t)(c).g << go) \
                        | ((uint32_t)(c).b << bo))

/* Two loops, not one with a branch: the vertical weight is constant across a
 * row, so testing it per pixel would be 1024 redundant branches per row.
 *
 * Both source from the converted row cache, so the nearest path - the default,
 * and every pixel at a whole factor - is a pure indexed copy with no arithmetic
 * at all. */
#define BLIT_ROW_NEAREST() do {                                              \
	int x;                                                                   \
	for (x = x0; x < x1; x++) {                                              \
		diatom_tap tx = g_colmap[x - dst.x];                                 \
		if (!tx.w) out[x] = c0[tx.idx];                                      \
		else       out[x] = PACK(mix(unpack(c0[tx.idx]),                     \
		                             unpack(c0[tx.idx + 1]), tx.w));         \
	}                                                                        \
} while (0)

#define BLIT_ROW_BLEND() do {                                                \
	int x;                                                                   \
	for (x = x0; x < x1; x++) {                                              \
		diatom_tap tx = g_colmap[x - dst.x];                                 \
		diatom_rgb a = unpack(c0[tx.idx]);                                   \
		diatom_rgb b = unpack(c1[tx.idx]);                                   \
		if (tx.w) {                                                          \
			a = mix(a, unpack(c0[tx.idx + 1]), tx.w);                        \
			b = mix(b, unpack(c1[tx.idx + 1]), tx.w);                        \
		}                                                                    \
		out[x] = PACK(mix(a, b, ty.w));                                      \
	}                                                                        \
} while (0)

	if (!g_map_valid || w > ROWCACHE_MAX) {
		static bool said;

		/* Once. A source wider than the row cache draws NOTHING, and doing
		 * that sixty times a second in silence is how a blank or stale panel
		 * gets blamed on the core. */
		if (!said) {
			char m[96];

			said = true;
			snprintf(m, sizeof m, "blit refused: source %d wide, cache holds %d",
			         w, ROWCACHE_MAX);
			diatom_port_log(DIATOM_LOG_WARN, m);
		}
		return;
	}

	y0 = dst.y > 0 ? dst.y : 0;
	x0 = dst.x > 0 ? dst.x : 0;
	y1 = dst.y + dst.h < (int)g_vinfo.yres ? dst.y + dst.h : (int)g_vinfo.yres;
	x1 = dst.x + dst.w < (int)g_vinfo.xres ? dst.x + dst.w : (int)g_vinfo.xres;
	if (x1 <= x0 || y1 <= y0) return;

	/* The cache is per-frame: the source buffer is rewritten every time. */
	g_rc_row[0] = g_rc_row[1] = -1;

	/* Under a notice, keep what is drawn: see g_ov_under. Black where the
	 * picture does not reach, as the page is there. */
	under = g_ov_live && g_ov_under && g_ov && ov_x0() >= 0 && ov_y0() >= 0 &&
	        (int)g_vinfo.xres <= UNDER_ROW_MAX;
	if (under) {
		size_t i, n = (size_t)g_ov_w * (size_t)g_ov_h;

		uy0 = ov_y0();
		ux0 = ov_x0();
		for (i = 0; i < n; i++)
			g_ov_under[i] = g_bd_px ? bd_px(ux0 + (int)(i % (size_t)g_ov_w), uy0 + (int)(i / (size_t)g_ov_w))
			                        : opaque;
	}
	/* And under the level bar, the same way: see g_bar_under, which
	 * bar_frame has made black. */
	bar_under = g_bar_live && (int)g_vinfo.xres <= UNDER_ROW_MAX;

	for (y = y0; y < y1; y++) {
		diatom_tap ty = g_rowmap[y - dst.y];
		uint32_t *row = (uint32_t *)(page + (size_t)y * g_finfo.line_length);
		bool band = under && y >= uy0 && y < uy0 + g_ov_h;
		bool bar_band = bar_under && y >= g_bar_rect.y &&
		                y < g_bar_rect.y + g_bar_rect.h;
		uint32_t *out = band || bar_band ? g_under_row : row;
		const uint32_t *c0 = cache_row(src, pitch, fmt, ty.idx, w);
		const uint32_t *c1 = ty.w ? cache_row(src, pitch, fmt, ty.idx + 1, w) : NULL;

		if (ty.w) BLIT_ROW_BLEND();
		else      BLIT_ROW_NEAREST();

		/* Drawn in ordinary memory, then copied to the page and to the
		 * notice's or the bar's slice of it - two writes, and never a read
		 * of the page. */
		if (band || bar_band)
			memcpy(row + x0, g_under_row + x0, (size_t)(x1 - x0) * sizeof *row);
		if (band) {
			int a = x0 > ux0 ? x0 : ux0;
			int b = x1 < ux0 + g_ov_w ? x1 : ux0 + g_ov_w;

			if (b > a)
				memcpy(g_ov_under + (size_t)(y - uy0) * (size_t)g_ov_w + (a - ux0),
				       g_under_row + a, (size_t)(b - a) * sizeof *row);
		}
		if (bar_band) {
			const diatom_rect r = g_bar_rect;
			int a = x0 > r.x ? x0 : r.x;
			int b = x1 < r.x + r.w ? x1 : r.x + r.w;

			if (b > a)
				memcpy(g_bar_under + (size_t)(y - r.y) * (size_t)r.w + (a - r.x),
				       g_under_row + a, (size_t)(b - a) * sizeof *row);
		}
	}

	/* Letterbox bars are not repainted per frame: pages start opaque black and
	 * the rect only shrinks when the user changes mode, which clears them. */
#undef BLIT_ROW_NEAREST
#undef BLIT_ROW_BLEND
#undef PACK
}

/* Wipe every page to opaque black. Needed when the rect shrinks, or the
 * previous mode's picture stays framing the new one. */
static void clear_pages(void)
{
	uint32_t *p = (uint32_t *)g_fb;
	size_t n = g_fb_size / sizeof *p, i;
	int pg, y;

	for (i = 0; i < n; i++) p[i] = g_opaque;
	/* And the Border over every page's panel, from the copy kept for it */
	if (!g_bd_px) return;
	for (pg = 0; pg < g_pages; pg++)
		for (y = 0; y < (int)g_vinfo.yres; y++)
			memcpy(page_base(pg) + (size_t)y * g_finfo.line_length,
			       g_bd_px + (size_t)y * g_vinfo.xres, (size_t)g_vinfo.xres * sizeof *p);
}

/* The host's picture in the pages' pixels at the panel's size: stretched,
 * smoothly (bilinear: a small picture is soft by being stretched, one of the
 * panel's size is itself), opaque. Built once a call. */
static void cpu_border(const uint8_t *bgra, int w, int h)
{
	const unsigned ro = g_vinfo.red.offset, go = g_vinfo.green.offset, bo = g_vinfo.blue.offset;
	const int W = (int)g_vinfo.xres, H = (int)g_vinfo.yres;
	uint32_t *px;
	int x, y;

	g_bd_repaint = true;
	if (!bgra || w <= 0 || h <= 0 || W <= 0 || H <= 0) {
		free(g_bd_px);
		g_bd_px = NULL;
		return;
	}
	px = g_bd_px ? g_bd_px : malloc((size_t)W * (size_t)H * sizeof *px);
	if (!px) return;
	for (y = 0; y < H; y++) {
		/* The panel pixel's centre in the picture's pixels */
		const float fy = ((float)y + 0.5f) * (float)h / (float)H - 0.5f;
		const int y0 = fy < 0 ? 0 : (int)fy, y1 = y0 + 1 < h ? y0 + 1 : h - 1;
		const float ty = fy < 0 ? 0 : fy - (float)y0;

		for (x = 0; x < W; x++) {
			const float fx = ((float)x + 0.5f) * (float)w / (float)W - 0.5f;
			const int x0 = fx < 0 ? 0 : (int)fx, x1 = x0 + 1 < w ? x0 + 1 : w - 1;
			const float tx = fx < 0 ? 0 : fx - (float)x0;
			const uint8_t *a = bgra + ((size_t)y0 * w + x0) * 4, *b = bgra + ((size_t)y0 * w + x1) * 4;
			const uint8_t *c = bgra + ((size_t)y1 * w + x0) * 4, *d = bgra + ((size_t)y1 * w + x1) * 4;
			unsigned ch[3];
			int k;

			for (k = 0; k < 3; k++) {
				const float top = a[k] + (b[k] - a[k]) * tx, bot = c[k] + (d[k] - c[k]) * tx;

				ch[k] = (unsigned)(top + (bot - top) * ty + 0.5f);
			}
			px[(size_t)y * W + x] = (ch[2] << ro) | (ch[1] << go) | (ch[0] << bo) | g_opaque;
		}
	}
	g_bd_px = px;
}

/* ---------- the GPU path ------------------------------------------------------
 *
 * The GPU draws a frame here when the frame needs it: an old screen's shader
 * or its strength (the CRT, the scanlines, the LCD grid, which the launcher
 * offers where plat_gpu_shaders says), a GPU core's frame (SET_HW_RENDER), or
 * every frame when told to (below). Nearest and Sharp-Shimmerless stay the
 * CPU's blit, as kit 2 proved them. Both paths put the frame in one of fb0's
 * pages and hand the page to the same flip thread, so the panel never knows
 * which one drew it.
 *
 * No window surface. The PowerVR's null window system has one swap chain on
 * this display, and the launcher's SDL window holds it while a game runs (the
 * launcher keeps its window; src/device/brick.c hands nothing over). The fb
 * driver gives its memory as a dma-buf instead (FBIOGET_DMABUF, Allwinner's
 * dev_fb.c), EGL makes an image of each page (EGL_EXT_image_dma_buf_import),
 * and the presenting thread draws into the page's framebuffer object on a
 * context with no surface (EGL_KHR_surfaceless_context), waits for the GPU on
 * a fence and publishes the page, as the CPU path does after its blit.
 *
 * The drawing is the Pixel 2's (port/gles_state.h, gles_draw.h, gles_objects.h,
 * gles_present.h, gles_gpu.h); what differs is here: the EGL objects
 * (gl_start), where a frame is drawn (PRESENT_TARGET: a page), which way up
 * (PRESENT_TOP_DOWN: a page's first row is the top), what drawn means
 * (PRESENT_SWAP: the alpha made opaque, then a fence waited), putting it on
 * the panel (show_frame: the flip thread) and the display handed back
 * (give_display: the park page). make check-gpu-upright draws every kind of
 * picture through it into a page's stand-in on the host.
 *
 * DIATOM_PRESENT=cpu never starts it. DIATOM_PRESENT=gpu, or the card's
 * diatom-present in this handheld's settings folder (.userdata/brick, or
 * .userdata/smartpro on a Smart Pro: present_file.h) saying "gpu", draws
 * every frame with it, so a kit can time the two paths on the same game; the
 * card's file, whatever it says, also puts both paths' present times in the
 * log. */
#include "present_file.h"

/* Allwinner's fb driver's own command (drivers/video/fbdev/sunxi/disp2/disp/
 * dev_fb.c): the whole of fb0's memory as one dma-buf */
struct brick_fb_dmabuf { int fd; uint32_t flags; };
#define BRICK_FBIOGET_DMABUF _IOR('F', 0x21, struct brick_fb_dmabuf)

/* DRM's four-character codes, written out: no libdrm here */
#define BRICK_FOURCC(a, b, c, d) \
	((uint32_t)(a) | (uint32_t)(b) << 8 | (uint32_t)(c) << 16 | (uint32_t)(d) << 24)

static bool        g_gl_all;            /* every frame by the GPU */
static bool        g_gl_last;           /* the last frame presented was the GPU's */
static int         g_dmabuf_fd = -1;
static EGLImageKHR g_page_img[FB_PAGES];
static GLuint      g_page_tex[FB_PAGES], g_page_fb[FB_PAGES];
static int         g_gl_page = -1;      /* the page the thread draws into; its own */
static PFNEGLCREATEIMAGEKHRPROC            p_create_image;
static PFNEGLDESTROYIMAGEKHRPROC           p_destroy_image;
static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC p_image_target;

/* What the shared code reads that the CPU path has no use for: the
 * overlay's new image and the level bar's colors handed over. And On-Screen
 * Stats' line, style and colors, which both paths draw (the GPU path's
 * draw_hud, the CPU path's hud_page), written under g_mx by the host side */
static bool      g_ov_dirty;
static unsigned  g_osd_colors_seen;
static bool      g_hud_on, g_hud_dirty;
static char      g_hud_text[160];
static ds_theme  g_hud_colors;
static hud_style g_hud_style;

/* On-Screen Stats' host side (port/hud_host.h), on the Brick's sensors: the
 * A133's CPU and GPU zones, as its device tree names them, and the AXP2202's
 * gauge (plastron's kernel patch 0016) */
#define HUD_ZONE_A       "cpu_thermal_zone"
#define HUD_ZONE_B       "gpu_thermal_zone"
#define HUD_BATTERY_FILE "/sys/class/power_supply/axp2202-battery/capacity"
#include "hud_host.h"

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

/* port/gles_present.h's, which the drawing needs first */
static float ov_scale(void);

/* A page's first row in memory is the panel's top: the display reads fb0
 * from the page's first byte, the dma-buf import makes that byte the image's
 * first texel, and GL draws a texture's first row at y 0, the bottom of a
 * window surface. So the drawing's y goes the other way here (to_clip), and a
 * capture's rows come back in order (capture_front). Without it every frame
 * the GPU drew, a shader's or a GPU core's, the Border and every notice on
 * it, reached the panel upside down (issue #419, 1.0.7-kit.1). */
#define PRESENT_TOP_DOWN

#include "gles_draw.h"

/* Whether this EGL names an extension in its string */
static bool egl_has(const char *name)
{
	const char *ext = eglQueryString(g_egl_display, EGL_EXTENSIONS);
	size_t n = strlen(name);

	while (ext && (ext = strstr(ext, name))) {
		if (ext[n] == ' ' || ext[n] == '\0') return true;
		ext += n;
	}
	return false;
}

/* fb0's pixel layout as a DRM code: the A133's is B, G, R, A in memory */
static uint32_t page_fourcc(void)
{
	bool alpha = g_vinfo.transp.length > 0;

	if (g_vinfo.red.offset == 16 && g_vinfo.blue.offset == 0)
		return alpha ? BRICK_FOURCC('A', 'R', '2', '4') : BRICK_FOURCC('X', 'R', '2', '4');
	if (g_vinfo.red.offset == 0 && g_vinfo.blue.offset == 16)
		return alpha ? BRICK_FOURCC('A', 'B', '2', '4') : BRICK_FOURCC('X', 'B', '2', '4');
	return 0;
}

/* The pages as images, textures and framebuffer objects; false, logged, at
 * the first that fails */
static bool pages_start(void)
{
	struct brick_fb_dmabuf exp = { -1, 0 };
	uint32_t fourcc = page_fourcc();
	int i;

	if (!fourcc) {
		port_logf(DIATOM_LOG_WARN, "brick: GPU path off: fb0's layout (red at %u, blue at %u) "
		          "has no DRM code here", g_vinfo.red.offset, g_vinfo.blue.offset);
		return false;
	}
	if (ioctl(g_fb_fd, BRICK_FBIOGET_DMABUF, &exp) != 0 || exp.fd < 0) {
		port_logf(DIATOM_LOG_WARN, "brick: GPU path off: fb0 gave no dma-buf (%s)", strerror(errno));
		return false;
	}
	g_dmabuf_fd = exp.fd;
	glGenTextures(FB_PAGES, g_page_tex);
	glGenFramebuffers(FB_PAGES, g_page_fb);
	for (i = 0; i < g_pages; i++) {
		const EGLint attrs[] = {
			EGL_WIDTH, (EGLint)g_vinfo.xres,
			EGL_HEIGHT, (EGLint)g_vinfo.yres,
			EGL_LINUX_DRM_FOURCC_EXT, (EGLint)fourcc,
			EGL_DMA_BUF_PLANE0_FD_EXT, g_dmabuf_fd,
			EGL_DMA_BUF_PLANE0_OFFSET_EXT, (EGLint)((size_t)i * g_vinfo.yres * g_finfo.line_length),
			EGL_DMA_BUF_PLANE0_PITCH_EXT, (EGLint)g_finfo.line_length,
			EGL_NONE
		};

		g_page_img[i] = p_create_image(g_egl_display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT,
		                               (EGLClientBuffer)NULL, attrs);
		if (g_page_img[i] == EGL_NO_IMAGE_KHR) {
			port_logf(DIATOM_LOG_WARN, "brick: GPU path off: page %d as an image (0x%x)",
			          i, eglGetError());
			return false;
		}
		glBindTexture(GL_TEXTURE_2D, g_page_tex[i]);
		p_image_target(GL_TEXTURE_2D, (GLeglImageOES)g_page_img[i]);
		glBindFramebuffer(GL_FRAMEBUFFER, g_page_fb[i]);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_page_tex[i], 0);
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
			port_logf(DIATOM_LOG_WARN, "brick: GPU path off: page %d cannot be drawn into", i);
			return false;
		}
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
	return true;
}

/* On the presenting thread: EGL with no surface, the pages, and the GL
 * objects the drawing needs (port/gles_objects.h) */
static bool gl_start(void)
{
	static const EGLint config_attribs[] = {
		EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
		EGL_NONE
	};
	static const EGLint context_attribs[] = { EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE };
	EGLint n = 0;

	g_egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (g_egl_display == EGL_NO_DISPLAY || !eglInitialize(g_egl_display, NULL, NULL)) {
		diatom_port_log(DIATOM_LOG_WARN, "brick: GPU path off: no EGL display");
		g_egl_display = EGL_NO_DISPLAY;
		return false;
	}
	port_logf(DIATOM_LOG_INFO, "brick: EGL %s, %s", eglQueryString(g_egl_display, EGL_VENDOR),
	          eglQueryString(g_egl_display, EGL_VERSION));
	if (!egl_has("EGL_EXT_image_dma_buf_import") || !egl_has("EGL_KHR_surfaceless_context") ||
	    !egl_has("EGL_KHR_image_base")) {
		diatom_port_log(DIATOM_LOG_WARN, "brick: GPU path off: EGL lacks dma-buf images or "
		                "surfaceless contexts");
		return false;
	}
	eglBindAPI(EGL_OPENGL_ES_API);
	if (!eglChooseConfig(g_egl_display, config_attribs, &g_egl_config, 1, &n) || n < 1) {
		diatom_port_log(DIATOM_LOG_WARN, "brick: GPU path off: no OpenGL ES 3 config");
		return false;
	}
	g_egl_context = eglCreateContext(g_egl_display, g_egl_config, EGL_NO_CONTEXT, context_attribs);
	g_egl_surface = EGL_NO_SURFACE;
	if (g_egl_context == EGL_NO_CONTEXT ||
	    !eglMakeCurrent(g_egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, g_egl_context)) {
		port_logf(DIATOM_LOG_WARN, "brick: GPU path off: no OpenGL ES 3 context (0x%x)", eglGetError());
		return false;
	}
	port_logf(DIATOM_LOG_INFO, "brick: GL %s, %s", (const char *)glGetString(GL_RENDERER),
	          (const char *)glGetString(GL_VERSION));
	p_create_image  = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
	p_destroy_image = (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
	p_image_target  = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
	if (!p_create_image || !p_destroy_image || !p_image_target || !sync_procs()) {
		diatom_port_log(DIATOM_LOG_WARN, "brick: GPU path off: no image or fence calls");
		return false;
	}
	g_panel_w = g_surface_w = (int)g_vinfo.xres;
	g_panel_h = g_surface_h = (int)g_vinfo.yres;
	g_turned = false;
	if (!pages_start()) return false;

#include "gles_objects.h"
	diatom_port_log(DIATOM_LOG_INFO, "brick: GPU path ready, drawing into fb0's pages");
	return true;
}

static void gl_stop(void)
{
	int i;

	if (g_egl_display == EGL_NO_DISPLAY) return;
	if (g_egl_context != EGL_NO_CONTEXT) {
		glDeleteFramebuffers(FB_PAGES, g_page_fb);
		glDeleteTextures(FB_PAGES, g_page_tex);
	}
	for (i = 0; i < FB_PAGES; i++)
		if (g_page_img[i] != EGL_NO_IMAGE_KHR && p_destroy_image) {
			p_destroy_image(g_egl_display, g_page_img[i]);
			g_page_img[i] = EGL_NO_IMAGE_KHR;
		}
	if (g_dmabuf_fd >= 0) { close(g_dmabuf_fd); g_dmabuf_fd = -1; }
	eglMakeCurrent(g_egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	if (g_egl_context != EGL_NO_CONTEXT) eglDestroyContext(g_egl_display, g_egl_context);
	g_egl_context = EGL_NO_CONTEXT;
	eglTerminate(g_egl_display);
	g_egl_display = EGL_NO_DISPLAY;
}

/* A page holding no role in the flip mailbox; when all are taken, the pending
 * one, which the flip thread has not started: a frame nobody saw. The CPU
 * path's choice too (present). */
static int page_take(void)
{
	int page;

	pthread_mutex_lock(&g_flip_mx);
	for (page = 0; page < g_pages; page++)
		if (page != g_page_front && page != g_page_inflight && page != g_page_pending)
			break;
	if (page == g_pages) {
		page = g_page_pending;
		g_page_pending = -1;
	}
	pthread_mutex_unlock(&g_flip_mx);
	return page;
}

/* PRESENT_TARGET: the next frame into a free page's framebuffer object */
static void gl_target(void)
{
	g_gl_page = page_take();
	glBindFramebuffer(GL_FRAMEBUFFER, g_page_fb[g_gl_page]);
	glViewport(0, 0, g_panel_w, g_panel_h);
}

/* PRESENT_SWAP: every pixel's alpha opaque (the display engine shows a pixel
 * by its alpha byte, see g_opaque), then the GPU done before the page goes
 * to the flip thread, as the display reads memory, not the GPU */
static void gl_drawn(void)
{
	EGLSyncKHR s;

	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	s = p_create_sync(g_egl_display, EGL_SYNC_FENCE_KHR, NULL);
	if (s != EGL_NO_SYNC_KHR) {
		glFlush();
		wait_sync(s);
	} else {
		glFinish();
	}
}

/* The drawn page to the flip thread, latest wins */
static void show_frame(void)
{
	if (g_gl_page < 0) return;
	pthread_mutex_lock(&g_flip_mx);
	g_page_pending = g_gl_page;
	pthread_cond_signal(&g_flip_cv);
	pthread_mutex_unlock(&g_flip_mx);
	g_gl_page = -1;
}

static void park(diatom_park park_mode, int front);

/* At a stop, after the shared code drew black for BLANK: the last page parked
 * where the launcher's two pages are not (park) */
static void give_display(void)
{
	park(DIATOM_PARK_KEEP, flip_drain());
}

#define PRESENT_TARGET() gl_target()
#define PRESENT_SWAP()   gl_drawn()
#define PORT_FN(n)       gl_##n

/* port/gles_gpu.h's diatom_port_gpu_begin as gl_gpu_begin, so this file's
 * own can start the GPU path first, and its diatom_port_gpu_proc as
 * gl_gpu_proc, so this file's own can look further (both below) */
#define diatom_port_gpu_begin gl_gpu_begin
#define diatom_port_gpu_proc  gl_gpu_proc
bool gl_gpu_begin(const diatom_gpu_spec *spec);
void *gl_gpu_proc(const char *name);
#include "gles_present.h"
#undef diatom_port_gpu_begin
#undef diatom_port_gpu_proc

/* The presenting thread started at the first frame or core that needs it,
 * not at Diatom's start: its EGL and its programs cost nothing to a boot or
 * to the games the CPU draws */
static bool gl_ensure(void)
{
	if (g_thread_running) return true;
	if (!g_gl_possible || g_gl_failed) return false;
	{
		pthread_condattr_t ca;

		pthread_condattr_init(&ca);
		pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
		pthread_cond_destroy(&g_idle);
		pthread_cond_init(&g_idle, &ca);
		pthread_condattr_destroy(&ca);
	}
	g_thread_ready = 0;
	g_thread_stop = false;
	if (pthread_create(&g_thread, NULL, present_thread, NULL) != 0) {
		diatom_port_log(DIATOM_LOG_WARN, "brick: GPU path off: no presenting thread");
		g_gl_failed = true;
		return false;
	}
	pthread_mutex_lock(&g_mx);
	while (g_thread_ready == 0) pthread_cond_wait(&g_idle, &g_mx);
	pthread_mutex_unlock(&g_mx);
	if (g_thread_ready < 0) {
		pthread_join(g_thread, NULL);
		g_gl_failed = true;
		return false;
	}
	g_thread_running = true;
	return true;
}

/* The frame wanted from the GPU: an old screen drawn by a shader, or every
 * frame when told to */
static bool gl_wanted(diatom_filter filter)
{
	return (g_gl_all || (filter != DIATOM_FILTER_NEAREST && filter != DIATOM_FILTER_SHARP)) &&
	       gl_ensure();
}

/* A core that draws with the GPU: the GPU path first */
bool diatom_port_gpu_begin(const diatom_gpu_spec *spec)
{
	if (!gl_ensure()) {
		diatom_port_log(DIATOM_LOG_ERROR, "brick: no GPU for the core (the GPU path is off)");
		return false;
	}
	return gl_gpu_begin(spec);
}

/* A core's GL or EGL call by name: EGL's answer first, as on the Pixel 2,
 * then the libraries' own symbols. EGL 1.4, the PowerVR's, owes
 * eglGetProcAddress only for extensions, and a core's GL loader (libretro's
 * glsm, the cores' own) asks for core calls by name too; Diatom links both
 * libraries, so opening them again only finds them. */
void *diatom_port_gpu_proc(const char *name)
{
	static void *gles, *egl;
	void *f = gl_gpu_proc(name);

	if (f || !name) return f;
	if (!gles) gles = dlopen("libGLESv2.so.2", RTLD_NOW | RTLD_GLOBAL);
	if (gles && (f = dlsym(gles, name))) return f;
	if (!egl) egl = dlopen("libEGL.so.1", RTLD_NOW | RTLD_GLOBAL);
	return egl ? dlsym(egl, name) : NULL;
}

/* Whether the last frame on the panel was the GPU's: a shader's frame, or a
 * GPU core's (present_gpu, counted in g_gpu_seq), since the CPU's last */
static unsigned g_gpu_seq_seen;
static bool gl_drew_last(void)
{
	return g_gl_last || g_gpu_seq != g_gpu_seq_seen;
}

/* Whether the GPU path may start, with why not in the log */
static void gl_init(void)
{
	const char *want = getenv("DIATOM_PRESENT");
	char line[16] = "", path[512];
	FILE *f;

	present_file_paths(NULL, 0, path, sizeof path);
	if (!want && (f = fopen(path, "r"))) {
		if (fgets(line, sizeof line, f)) line[strcspn(line, " \r\n")] = '\0';
		fclose(f);
		want = line;
		/* A kit's timing: both paths' present times in the log, once a
		 * second (DIATOM_PRESENT_DEBUG's lines) */
		g_present_debug = true;
	}
	if (want && !strcmp(want, "cpu")) {
		diatom_port_log(DIATOM_LOG_INFO, "brick: GPU path off (DIATOM_PRESENT=cpu)");
		return;
	}
	if (g_pan_broken || !g_flip_running || g_pages < 3) {
		diatom_port_log(DIATOM_LOG_INFO, "brick: GPU path off: no flip thread or no three pages");
		return;
	}
	g_gl_possible = true;
	g_gl_all = want && !strcmp(want, "gpu");
	if (g_gl_all) diatom_port_log(DIATOM_LOG_INFO, "brick: every frame by the GPU (DIATOM_PRESENT=gpu)");
}

static void gl_quit(void)
{
	if (!g_thread_running) return;
	drain();
	pthread_mutex_lock(&g_mx);
	g_thread_stop = true;
	pthread_cond_signal(&g_cv);
	pthread_mutex_unlock(&g_mx);
	pthread_join(g_thread, NULL);
	g_thread_running = false;
}

/* On-Screen Stats on the CPU path: the line made into pixels when it, its
 * style or its colors change (hud_render, as the GPU path's draw_hud does),
 * placed by the style (hud_place), then on every page it goes on its
 * rectangle cleared outside the picture (the letterbox, where a page keeps
 * what was drawn three frames ago: blending over that again would darken a
 * half-clear pill each time) and the line blended in, straight alpha. Taken
 * off a page it was on once it is off or has moved. On the frame loop's
 * thread, in present. */
static uint32_t    g_hudc_px[1024 * 128];
static diatom_rect g_hudc_rect;                 /* where it goes now; w 0, nowhere */
static diatom_rect g_hudc_painted[FB_PAGES];
static char        g_hudc_text[sizeof g_hud_text];
static hud_style   g_hudc_style;
static ds_theme    g_hudc_colors;

static void hud_cpu_update(void)
{
	char text[sizeof g_hud_text];
	hud_style st;
	ds_theme colors;
	bool on;
	int w = 0, h = 0, x = 0, y = 0;

	pthread_mutex_lock(&g_mx);
	on = g_hud_on;
	if (on) {
		memcpy(text, g_hud_text, sizeof text);
		st = g_hud_style;
		colors = g_hud_colors;
	}
	pthread_mutex_unlock(&g_mx);
	if (!on) {
		g_hudc_rect.w = 0;
		g_hudc_text[0] = '\0';
		return;
	}
	if (g_hudc_rect.w > 0 && !strcmp(text, g_hudc_text) &&
	    !memcmp(&st, &g_hudc_style, sizeof st) && !memcmp(&colors, &g_hudc_colors, sizeof colors))
		return;
	memcpy(g_hudc_text, text, sizeof text);
	g_hudc_style = st;
	g_hudc_colors = colors;
	g_hudc_rect.w = 0;
	/* At the launcher's scale, the panel's height over its 768 units: 1 on
	 * the Brick, 0.9375 on the Smart Pro's 1280x720 (ZyperOS's
	 * src/canvas.h) */
	const float k = g_vinfo.yres < 768 ? (float)g_vinfo.yres / 768.0f : 1.0f;
	hud_size(text, k, &st, &w, &h);
	if (!w || w > 1024 || h > 128 || w > (int)g_vinfo.xres || h > (int)g_vinfo.yres) return;
	hud_render(g_hudc_px, w, h, w, k, text, &st, &colors, false);
	hud_place(&st, k, w, h, (int)g_vinfo.xres, (int)g_vinfo.yres, &x, &y);
	if (x < 0 || y < 0 || x + w > (int)g_vinfo.xres || y + h > (int)g_vinfo.yres) return;
	g_hudc_rect = (diatom_rect){ x, y, w, h };
}

static void hud_page(uint8_t *base, int page, diatom_rect dst)
{
	const unsigned ro = g_vinfo.red.offset, go = g_vinfo.green.offset;
	const unsigned bo = g_vinfo.blue.offset;
	diatom_rect *was = &g_hudc_painted[page], r = g_hudc_rect;
	int yy, xx;

	if (was->w > 0 && (r.w == 0 || memcmp(was, &r, sizeof r) != 0)) {
		clear_overlay(base, *was, dst);
		was->w = 0;
	}
	if (r.w == 0) return;
	clear_overlay(base, r, dst);
	for (yy = 0; yy < r.h; yy++) {
		uint32_t *row = (uint32_t *)(base + (size_t)(r.y + yy) * g_finfo.line_length) + r.x;
		const uint32_t *src = g_hudc_px + (size_t)yy * (size_t)r.w;

		for (xx = 0; xx < r.w; xx++) {
			const uint32_t q = src[xx];
			const unsigned a = q >> 24;
			unsigned cr = (q >> 16) & 0xff, cg = (q >> 8) & 0xff, cb = q & 0xff;

			if (a == 0) continue;
			if (a < 255) {
				const uint32_t p = row[xx];

				cr = (cr * a + ((p >> ro) & 0xff) * (255 - a) + 127) / 255;
				cg = (cg * a + ((p >> go) & 0xff) * (255 - a) + 127) / 255;
				cb = (cb * a + ((p >> bo) & 0xff) * (255 - a) + 127) / 255;
			}
			row[xx] = (cr << ro) | (cg << go) | (cb << bo) | g_opaque;
		}
	}
	*was = r;
}

/* The planes and the strength are held for both paths: the CPU path reads
 * g_planes in present, the GPU path copies both into each frame */
void diatom_port_planes(const diatom_plane *planes, int n) { gl_planes(planes, n); }
void diatom_port_strength(int percent) { gl_strength(percent); }

/* A stop by the path that drew the last frame */
void diatom_port_present_stop(diatom_park park_mode)
{
	if (g_thread_running && gl_drew_last()) gl_present_stop(park_mode);
	else                                    cpu_present_stop(park_mode);
}

/* The notice to both: the CPU path keeps the picture under it, the GPU path
 * uploads it */
static void cpu_overlay(const uint8_t *bgra, int w, int h, unsigned ms);
void diatom_port_overlay(const uint8_t *bgra, int w, int h, unsigned ms)
{
	cpu_overlay(bgra, w, h, ms);
	if (g_thread_running) gl_overlay(bgra, w, h, ms);
}

/* The Border to both: the CPU path keeps it at the panel's size and paints
 * the pages with it at its next frame, the GPU path uploads it */
void diatom_port_border(const uint8_t *bgra, int w, int h)
{
	cpu_border(bgra, w, h);
	/* Kept by the GPU path's state even with its thread not started: a
	 * thread that starts later uploads it (gles_objects.h) */
	gl_border(bgra, w, h);
}

/* A capture of the last frame by the path that drew it: the GPU's draws it
 * again on its thread (port/gles_present.h's capture_front) */
static bool cpu_capture(const char *path);
bool diatom_port_capture(const char *path)
{
	bool ok;

	if (!g_thread_running || !gl_drew_last() || !path) return cpu_capture(path);
	drain();
	pthread_mutex_lock(&g_mx);
	g_capture_path = path;
	pthread_cond_signal(&g_cv);
	while (g_capture_path) pthread_cond_wait(&g_idle, &g_mx);
	ok = g_capture_ok;
	pthread_mutex_unlock(&g_mx);
	return ok;
}

static void cpu_present(const void *src, int w, int h, size_t pitch,
                        diatom_pixfmt fmt, diatom_rect dst,
                        diatom_filter filter)
{
	uint64_t t0 = 0, t1 = 0;
	bool rect_changed;
	int page;

	/* Planes (a DS frame's two screens): this port's blit is one source rect
	 * into one destination through prebuilt maps, so it draws the largest
	 * plane and nothing else - one screen, never two garbled into one. The
	 * Brick has no DS on ZyperOS; this only keeps the seam honest. */
	if (src && g_nplanes > 0) {
		const diatom_plane *p = &g_planes[0];
		size_t bpp = fmt == DIATOM_PIX_RGB565 ? 2 : 4;
		int i;

		for (i = 1; i < g_nplanes; i++)
			if ((long)g_planes[i].dst.w * g_planes[i].dst.h > (long)p->dst.w * p->dst.h)
				p = &g_planes[i];
		src = (const unsigned char *)src + (size_t)p->src.y * pitch + (size_t)p->src.x * bpp;
		w = p->src.w;
		h = p->src.h;
		dst = p->dst;
	}

	/* Dupe frame: the front page already shows it. Nothing to draw, nothing
	 * to flip. */
	if (!src || w <= 0 || h <= 0) return;

	rect_changed = !g_map_valid || memcmp(&dst, &g_map_dst, sizeof dst) != 0;

	/* What the PORT was handed, which is not the same claim as what the host
	 * computed and logged. A game came up filling only the left half of the
	 * panel while the host's own line said 1024x768 at 0,0, and there was no
	 * way to tell which of the two was wrong. Only on a change, so it is one
	 * line per mode rather than one per frame.
	 *
	 * The SOURCE side counts as a change too, and used to not.
	 *
	 * ensure_maps rebuilds on src w/h and filter as well as dst, so a core that
	 * alters its geometry mid-run - which they do, and the host counts them -
	 * silently threw the maps away and built new ones with nothing written
	 * down. That is precisely the case this line exists for: the half-panel
	 * picture, and a report of a GBA game drawn twice side by side while the
	 * display mode was being cycled. Whatever that turns out to be, the log now
	 * says what the port was handed at the moment it changed, including the
	 * pitch - a stride that disagrees with the width is what draws an image
	 * twice across a row. */
	if (rect_changed || w != g_map_src_w || h != g_map_src_h ||
	    filter != g_map_filter) {
		char m[192];

		size_t bpp  = (fmt == DIATOM_PIX_RGB565) ? 2 : 4;
		size_t span = pitch / (bpp ? bpp : 1);

		/* The stride in PIXELS beside the width in pixels. A core is entitled
		 * to pad - mGBA hands over 240 visible pixels in a 256-wide buffer, so
		 * pitch 512 against width 240 is correct and normal, and calling that
		 * a mismatch cries wolf on every GBA frame. What is not survivable is
		 * a stride SHORTER than the width: that reads the next row's pixels
		 * into this one, which is what draws an image twice across a row. */
		snprintf(m, sizeof m,
		         "present: src %dx%d pitch %zu (%zu px) fmt %d -> dst %dx%d "
		         "at %d,%d (panel %ux%u)%s",
		         w, h, pitch, span, (int)fmt, dst.w, dst.h, dst.x, dst.y,
		         g_vinfo.xres, g_vinfo.yres,
		         span >= (size_t)w ? "" : "  <-- STRIDE SHORTER THAN WIDTH");
		diatom_port_log(DIATOM_LOG_INFO, m);
	}

	if (!ensure_maps(w, h, dst, filter)) {
		/* Silent until now: a failed map means present draws nothing at all
		 * and the panel holds whatever was there, which looks like the game
		 * having frozen rather than like an allocation having failed. */
		diatom_port_log(DIATOM_LOG_WARN, "present: no scale map; frame dropped");
		return;
	}
	/* A smaller rect leaves the old picture around the new one. Only on a mode
	 * change, so the cost of wiping every page does not matter. */
	if (rect_changed || g_bd_repaint) {
		g_bd_repaint = false;
		clear_pages();
		memset(g_bar_painted, 0, sizeof g_bar_painted);
		memset(g_dot_painted, 0, sizeof g_dot_painted);
		memset(g_ov_painted, 0, sizeof g_ov_painted);
	}

	g_ov_live = g_ov && diatom_port_now_us() < g_ov_until;
	bar_frame();

	if (g_pan_broken) {
		blit(page_base(g_page_front), src, w, h, pitch, fmt, dst);
		bar_page(page_base(g_page_front), g_page_front, dst);
		{
			bool dot = dot_visible();

			if (dot || g_dot_painted[g_page_front]) {
				dot_page(page_base(g_page_front), dst, dot);
				g_dot_painted[g_page_front] = dot;
			}
		}
		overlay_page(page_base(g_page_front), g_page_front, dst);
		return;
	}

	/* Pick a page holding no role. If every page is spoken for - the panel is
	 * consuming slower than the core produces - steal the pending one: the
	 * thread has not started panning it, so overwriting it just replaces a
	 * frame nobody saw with a newer one. Latest wins. */
	page = page_take();

	if (g_present_debug) t0 = diatom_port_now_us();

	blit(page_base(page), src, w, h, pitch, fmt, dst);
	/* On-Screen Stats first, so the bar, the disc and a notice go over it */
	hud_cpu_update();
	hud_page(page_base(page), page, dst);
	/* Painted, or unpainted. A page keeps whatever was last written outside
	 * dst, so the bar has to be taken off the same page it was put on - and
	 * only here, where this page is the one being prepared and no one is
	 * scanning it out. */
	bar_page(page_base(page), page, dst);
	/* The disc the same way: painted every frame it is shown, taken off the
	 * page it was on once it isn't - which, blinking, is every other second. */
	{
		bool dot = dot_visible();

		if (dot || g_dot_painted[page]) {
			dot_page(page_base(page), dst, dot);
			g_dot_painted[page] = dot;
		}
	}
	overlay_page(page_base(page), page, dst);

	pthread_mutex_lock(&g_flip_mx);
	g_presented = true;
	g_page_pending = page;
	pthread_cond_signal(&g_flip_cv);
	pthread_mutex_unlock(&g_flip_mx);

	if (g_present_debug) {
		static int n;
		t1 = diatom_port_now_us();
		if (++n >= 60) {
			char msg[96];
			n = 0;
			snprintf(msg, sizeof msg, "present: blit+publish %llu us",
			         (unsigned long long)(t1 - t0));
			diatom_port_log(DIATOM_LOG_DEBUG, msg);
		}
	}
}

/* A frame to the panel by the path it needs (the GPU path, above). Between the
 * two, the one stopping first finishes what it has in hand: the GPU's frame in
 * flight, before the CPU writes a page; and the pages cleared, as the CPU path
 * draws only inside the picture and keeps track of what it painted around it,
 * which a page the GPU drew wholly would leave wrong. */
void diatom_port_present(const void *src, int w, int h, size_t pitch,
                         diatom_pixfmt fmt, diatom_rect dst,
                         diatom_filter filter)
{
	bool gpu = gl_wanted(filter);

	if (!src || w <= 0 || h <= 0) {
		if (!gpu) cpu_present(src, w, h, pitch, fmt, dst, filter);
		return;
	}
	/* The display's gamma table at the take (nothing presented since the
	 * last stop), and a night's start or end after it, whichever path draws */
	display_gamma(!g_presented);
	if (gpu != gl_drew_last()) {
		port_logf(DIATOM_LOG_INFO, "brick: frames now by the %s", gpu ? "GPU" : "CPU");
		if (gl_drew_last()) {
			drain();
			flip_drain();
			clear_pages();
			g_map_valid = false;
			memset(g_bar_painted, 0, sizeof g_bar_painted);
			memset(g_ov_painted, 0, sizeof g_ov_painted);
			memset(g_dot_painted, 0, sizeof g_dot_painted);
			memset(g_hudc_painted, 0, sizeof g_hudc_painted);
		}
		g_gl_last = gpu;
		g_gpu_seq_seen = g_gpu_seq;
	}
	if (gpu) gl_present(src, w, h, pitch, fmt, dst, filter);
	else     cpu_present(src, w, h, pitch, fmt, dst, filter);
}

size_t diatom_port_audio_write(const int16_t *frames, size_t n)
{
	size_t queued, room;

	/* Never blocks; drops on overflow. A blocking write would pace the whole
	 * program off the audio clock, which rules out dynamic rate control.
	 *
	 * Take only what fits. The previous test was `queued >= capacity`, which
	 * refuses only an ALREADY FULL queue and then admits a whole batch: at 4095
	 * queued a 832-frame batch landed entire, and the queue reached a measured
	 * **4927 against a stated 4096**. That mattered beyond the extra latency,
	 * because rate control clamps its error term at +1.0 - reached at exactly
	 * capacity - so everything above capacity was invisible to the controller
	 * that was supposed to be preventing it.
	 *
	 * Clamping rather than refusing the batch outright loses less: the frames
	 * that do not fit are dropped either way, and there is no reason to discard
	 * the ones that would have. */
	/* A sink can die under the port - a headset switched off, or carried out
	 * of range. SDL reports the device STOPPED; falling back happens here
	 * rather than in a poll of its own because this already runs every frame,
	 * and the host finds out through diatom_port_audio_get, which is the only
	 * way it can (the port cannot announce anything).
	 *
	 * Only when a name was asked for. If the DEFAULT device dies there is
	 * nowhere better to go, and retrying it every frame would be a reopen
	 * storm in place of silence. */
	if (g_audio && g_audio_dev[0] &&
	    SDL_GetAudioDeviceStatus(g_audio) == SDL_AUDIO_STOPPED) {
		diatom_port_log(DIATOM_LOG_WARN,
		                "audio: the sink went away; falling back to the default");
		g_audio_dev[0] = '\0';
		audio_open(NULL);
	}
	if (!g_audio || !frames || !n) return 0;
	queued = diatom_port_audio_queued();
	room   = queued >= (size_t)AUDIO_BUFFER_FRAMES
	       ? 0 : (size_t)AUDIO_BUFFER_FRAMES - queued;
	if (n > room) n = room;
	if (!n) return 0;

	SDL_QueueAudio(g_audio, frames, (Uint32)(n * AUDIO_FRAME_BYTES));
	return n;
}

size_t diatom_port_audio_queued(void)
{
	if (!g_audio) return 0;
	return SDL_GetQueuedAudioSize(g_audio) / AUDIO_FRAME_BYTES;
}

/* Kernel code -> Diatom button. brick-pad sends the GKD Pixel 2's codes, so
 * this is the Pixel's table: the caps marked A, B, X, Y send SOUTH, EAST,
 * NORTH, WEST, named for their labels, which puts the right-hand button on the
 * game's A as on a SNES pad; L2 and R2 are keys, the d-pad four keys. MENU is
 * the Brick's own (BTN_MODE). */
static const struct { int code; int btn; } keymap[] = {
	{ BTN_DPAD_UP,    DIATOM_BTN_UP },    { BTN_DPAD_DOWN,  DIATOM_BTN_DOWN },
	{ BTN_DPAD_LEFT,  DIATOM_BTN_LEFT },  { BTN_DPAD_RIGHT, DIATOM_BTN_RIGHT },
	{ BTN_SOUTH,      DIATOM_BTN_A },     { BTN_EAST,       DIATOM_BTN_B },
	{ BTN_NORTH,      DIATOM_BTN_X },     { BTN_WEST,       DIATOM_BTN_Y },
	{ BTN_TL,         DIATOM_BTN_L1 },    { BTN_TR,         DIATOM_BTN_R1 },
	{ BTN_TL2,        DIATOM_BTN_L2 },    { BTN_TR2,        DIATOM_BTN_R2 },
	{ BTN_SELECT,     DIATOM_BTN_SELECT },{ BTN_START,      DIATOM_BTN_START },
	{ BTN_MODE,       DIATOM_BTN_MENU },
};

/* The four level buttons, as level keys: volume on its own two, brightness on
 * the two front keys (BTN_THUMBL and BTN_THUMBR), all on the gamepad. Never
 * reported as buttons, so no brightness press reaches a core. A press is
 * latched until the host has read it, so a tap between two frames counts; a
 * key the events say is held is checked against the kernel, so a release this
 * port never saw cannot leave the host repeating. The stepping and the repeat
 * are the host's. */
static const struct { int code; uint32_t key; } level_buttons[] = {
	{ KEY_VOLUMEUP,   DIATOM_LEVEL_KEY_VOLUME_UP },
	{ KEY_VOLUMEDOWN, DIATOM_LEVEL_KEY_VOLUME_DOWN },
	{ BTN_THUMBR,     DIATOM_LEVEL_KEY_BRIGHTNESS_UP },
	{ BTN_THUMBL,     DIATOM_LEVEL_KEY_BRIGHTNESS_DOWN },
};
#define LEVEL_BUTTONS (sizeof level_buttons / sizeof level_buttons[0])
static uint32_t g_level_down;      /* held, by the events */
static uint32_t g_level_pressed;   /* went down since the host last asked */

/* Whether `code` is a level button, recorded here if so. */
static bool level_button(int code, bool down)
{
	size_t k;

	for (k = 0; k < LEVEL_BUTTONS; k++) {
		if (level_buttons[k].code != code) continue;
		if (down) {
			g_level_down    |= level_buttons[k].key;
			g_level_pressed |= level_buttons[k].key;
		} else {
			g_level_down &= ~level_buttons[k].key;
		}
		return true;
	}
	return false;
}

/* The pad's keys as they are right now, for a start or after the kernel
 * dropped events (SYN_DROPPED): the held-at-entry rule needs to know about a
 * key that was already down. */
static void pad_resync(void)
{
	unsigned long keys[NLONGS(KEY_MAX)];
	size_t i;

	if (g_pad_fd < 0) return;
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
	if (g_pad_fd < 0) input_scan();
	if (g_pad_fd < 0)
		diatom_port_log(DIATOM_LOG_WARN, "brick: no gamepad found; no game input");
	pad_resync();
}

void diatom_port_input_poll(void)
{
	struct input_event ev[32];
	SDL_Event sev;
	const uint32_t before = g_buttons;
	ssize_t n;
	size_t i, k;

	/* The jack, checked once a frame here for the same reason the launcher
	 * checks it in plat_input_poll: whoever is pumping input owns the level,
	 * and this stops being called the moment the game ends. */
	gain_jack_poll();

	while (SDL_PollEvent(&sev))
		if (sev.type == SDL_QUIT) g_quit = true;

	/* Every press since the last poll, a tap gone up again too
	 * (diatom_port_input_pressed) */
	g_pressed = 0;
	while (g_pad_fd >= 0 && (n = read(g_pad_fd, ev, sizeof ev)) > 0) {
		for (i = 0; i < (size_t)n / sizeof ev[0]; i++) {
			if (ev[i].type == EV_SYN && ev[i].code == SYN_DROPPED) {
				pad_resync();
				continue;
			}
			/* A stick: to libretro's range from the pad's own */
			if (ev[i].type == EV_ABS && g_axis_max > 0) {
				int k2 = ev[i].code == ABS_X ? 0 : ev[i].code == ABS_Y ? 1 :
				         ev[i].code == ABS_RX ? 2 : ev[i].code == ABS_RY ? 3 : -1;
				long v = (long)ev[i].value * 0x7fff / g_axis_max;

				if (k2 >= 0)
					g_axes[k2] = (int16_t)(v > 0x7fff ? 0x7fff : v < -0x7fff ? -0x7fff : v);
				continue;
			}
			if (ev[i].type != EV_KEY || ev[i].value == 2) continue;
			if (g_input_debug) {
				char msg[48];
				snprintf(msg, sizeof msg, "key %d -> %d", ev[i].code, ev[i].value);
				diatom_port_log(DIATOM_LOG_DEBUG, msg);
			}
			/* Volume and brightness are the port's, and stop here: during
			 * a game nothing else sees them, the device UI not running. */
			if (level_button(ev[i].code, ev[i].value != 0)) continue;
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
}

uint32_t diatom_port_input_state(void) { return g_buttons; }
uint32_t diatom_port_input_pressed(void) { return g_pressed; }

bool diatom_port_input_axes(int16_t axes[4])
{
	if (g_axis_max <= 0) return false;
	memcpy(axes, g_axes, sizeof g_axes);
	return true;
}

/* No motor yet: GPIO 227 and the bldo3 rail wait for the tester's buzz
 * (the Brick parity plan's item 14), and `scale` will set the voltage */
bool diatom_port_rumble(uint16_t strong, uint16_t weak, float scale)
{
	(void)strong;
	(void)weak;
	(void)scale;
	return false;
}

void diatom_port_input_reset(void)
{
	struct input_event ev[32];

	SDL_PumpEvents();
	SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
	while (g_pad_fd >= 0 && read(g_pad_fd, ev, sizeof ev) > 0) { }
	g_level_down = g_level_pressed = 0;
	g_pressed = 0;
	pad_resync();
}

/* ---------- levels: the hardware under them --------------------------------- */

void diatom_port_level_invalidate(void)
{
	g_level = -1;
	/* And the jack, for the reason in the header: a transition that happened
	 * while the launcher was driving is invisible here, so the remembered
	 * state is a memory of a world this process was not watching. */
	g_jack_was = -1;
}

/* Positions, not a maximum index, so one MORE than the internal level ceiling.
 * That off-by-one is the whole reason the state plane pins the word. */
int diatom_port_level_positions(diatom_level_kind kind)
{
	switch (kind) {
	case DIATOM_LEVEL_VOLUME:     return g_mixer_fd >= 0 ? GAIN_LEVELS + 1 : 0;
	case DIATOM_LEVEL_BRIGHTNESS: return g_disp_fd >= 0 ? BRIGHT_LEVELS + 1 : 0;
	default:                      return 0;
	}
}

int diatom_port_level_read(diatom_level_kind kind)
{
	long v;

	switch (kind) {
	case DIATOM_LEVEL_VOLUME:
		/* Kept as well as returned: the mute and the jack re-apply act on
		 * the level the hardware is at, read here the first frame after a
		 * handover, before any press writes one. */
		if (gain_io(&v, 0) < 0) return -1;
		g_level = raw_to_level((int)v);
		return g_level;
	case DIATOM_LEVEL_BRIGHTNESS:
		return bright_read();
	default:
		return -1;
	}
}

void diatom_port_level_write(diatom_level_kind kind, int pos)
{
	switch (kind) {
	case DIATOM_LEVEL_VOLUME:
		if (g_mixer_fd < 0) return;
		g_level = pos;
		gain_apply();
		break;
	case DIATOM_LEVEL_BRIGHTNESS:
		if (g_disp_fd < 0) return;
		bright_apply(pos);
		break;
	default:
		break;
	}
}

uint32_t diatom_port_level_keys(void)
{
	unsigned long keys[NLONGS(KEY_MAX)];
	uint32_t out;
	size_t k;

	if (g_level_down) {
		memset(keys, 0, sizeof keys);
		if (g_pad_fd < 0 || ioctl(g_pad_fd, EVIOCGKEY(sizeof keys), keys) < 0)
			memset(keys, 0, sizeof keys);
		for (k = 0; k < LEVEL_BUTTONS; k++)
			if ((g_level_down & level_buttons[k].key) &&
			    !BIT_IS_SET(keys, level_buttons[k].code))
				g_level_down &= ~level_buttons[k].key;
	}
	out = g_level_down | g_level_pressed;
	g_level_pressed = 0;
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

/* The table again at once, on the frame loop's thread as display_gamma is,
 * while a game has the display; else at the take (display_gamma) */
void diatom_port_level_lift(int lift)
{
	if (lift < 0) lift = 0;
	if (lift > PANELGAMMA_LIFT_MAX) lift = PANELGAMMA_LIFT_MAX;
	if (lift == g_gamma_lift) return;
	g_gamma_lift = lift;
	g_gamma_color.lift = lift;
	if (g_disp_fd < 0 || !g_presented) return;
	if (panelgamma_sunxi_apply(g_disp_fd, &g_gamma_color) == 0)
		port_logf(DIATOM_LOG_INFO, "brick: the display's gamma table lifted %d", lift);
	else
		diatom_port_log(DIATOM_LOG_WARN, "brick: could not set the display's gamma table");
}

bool diatom_port_level_plugged(void)
{
	return __atomic_exchange_n(&g_jack_moved, false, __ATOMIC_RELAXED);
}

void diatom_port_power_hold(bool on, unsigned ms, unsigned at, bool done)
{
	ds_theme colors;
	int theme = themefile_read_bar(THEME_FILE, &colors);
	uint64_t now = diatom_port_now_us();
	float a, rise;

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
}
bool     diatom_port_should_quit(void) { return g_quit; }

static bool cpu_capture(const char *path)
{
	SDL_Surface *s, *rgb;
	bool ok;

	int front;

	if (!g_fb || !path) return false;

	/* Wait for the mailbox to drain before reading the front page.
	 *
	 * Without this, a capture reads whichever page the flip thread last got to,
	 * which depends on how long the blit took - so the same run captured at the
	 * same frame count could yield the frame before. Found 2026-08-25 while
	 * checking a blit optimization for correctness: old and new binaries
	 * produced captures differing in one 24-row band, and only at some frame
	 * counts, which is a blinking sprite one frame apart rather than a blit
	 * defect. An instrument that is not deterministic cannot verify anything. */
	front = flip_drain();

	/* Read back the page on glass. Masks come from the driver's reported
	 * channel offsets, same as the blit writes. */
	s = SDL_CreateRGBSurfaceFrom(page_base(front),
	                             (int)g_vinfo.xres, (int)g_vinfo.yres, 32,
	                             (int)g_finfo.line_length,
	                             0xffu << g_vinfo.red.offset,
	                             0xffu << g_vinfo.green.offset,
	                             0xffu << g_vinfo.blue.offset, 0);
	if (!s) return false;

	/* Plain 24-bit RGB: a 32-bit BMP carries a V4/V5 header several readers
	 * refuse, and the alpha channel is meaningless here anyway. */
	rgb = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_RGB24, 0);
	SDL_FreeSurface(s);
	if (!rgb) return false;
	ok = SDL_SaveBMP(rgb, path) == 0;
	SDL_FreeSurface(rgb);
	return ok;
}


uint64_t diatom_port_now_us(void)
{
	return diatom_ticks_to_us(SDL_GetPerformanceCounter(),
	                          SDL_GetPerformanceFrequency());
}

void diatom_port_sleep_us(double us) { diatom_nanosleep_us(us); }

void diatom_port_log(diatom_log_level lvl, const char *msg)
{
	static const char *tag[] = { "debug", "info", "warn", "error" };
	fprintf(stderr, "[%s] %s\n", tag[lvl], msg);
}

void diatom_port_mute_set(bool on)
{
	if (on == g_muted) return;
	g_muted = on;
	/* Straight away rather than at the next level change: the whole point is
	 * that a player flipping a switch hears it now. gain_apply writes the
	 * gain too, which is now part of the cut.
	 *
	 * Not with no level, though. Out of a game the level is -1 and the codec
	 * is the launcher's; applying -1 switched the speaker off under it and,
	 * with headphones in, wrote a gain past the register's end that wrapped
	 * around to full volume. Found 2026-09-30. */
	if (g_mixer_fd >= 0 && g_level >= 0) gain_apply();
}

bool diatom_port_mute_get(void) { return g_muted; }

/* The Brick has no GPU clock to hold: the CPU alone */
/* Its display is SDL's, which says nothing of when a frame was shown */
unsigned diatom_port_refreshes_missed(void) { return 0; }

void diatom_port_fast_clock(bool cpu, bool gpu)
{
	static bool said;

	(void)gpu;
	if (!diatom_cpufreq_hold_top(cpu) && !said) {
		said = true;
		diatom_port_log(DIATOM_LOG_WARN, "brick: could not set the CPU's lowest clock");
	}
}

/* The Brick's clocks are not measured per system: every game gets them all.
 * Once they are, its steps are a diatom_power_profile (port_power.h), as
 * the Pixel 2's are, and this writes them with diatom_power_set. */
void diatom_port_power(int level) { (void)level; }
void diatom_port_power_invalidate(void) { }

/* GPU frames: port/gles_gpu.h, through the GPU path (above), which a core
 * asking for the GPU is refused without (see diatom_port.h) */
