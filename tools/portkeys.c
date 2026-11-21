/* SPDX-License-Identifier: GPL-3.0-only */
/* portkeys: the GKD Pixel 2's volume keys while a port runs, and the bar that
 * says what they did.
 *
 * ZyperOS and Diatom read the volume keys themselves, and launch.sh stops both
 * for a port (or a standalone emulator), so for as long as Stardew Valley or
 * OpenBOR ran, VOL+ and VOL- did nothing. launch.sh starts this beside the
 * port and stops it after.
 *
 *   VOL+ / VOL-          the volume, a position at a time
 *   MENU held + VOL      the backlight instead, a rung at a time
 *   MENU on its own      with --menu (OpenBOR, a port), the in-game menu (menu_show)
 *
 * And over any port, "Getting hot" when the chip passes the kernel's trip, as
 * over a game (heat_check), and POWER and a battery run flat (end_port); over
 * your PICO-8 the low-battery disc and On-Screen Stats too (over_tick), all
 * of it in one frame on the cursor window (scene_put), and the player's
 * hotkeys, SELECT and a button (hot_do), and Auto Off (idle_tick).
 *
 * WHAT A KEY MEANS is tools/pkkeys.c's, which tools/pkkeys-check.c holds:
 * the events of every device are handled in the order of their times, so
 * MENU on the joypad and VOL+ on gpio-keys a few microseconds after it are a
 * brightness step whichever device is read first; the kernel's autorepeat is
 * not a step, and a held key repeats on the launcher's clock instead (300 ms,
 * then every 90, src/keyrepeat.h), woken by poll's timeout; and every step is
 * a line on stderr, which launch.sh sends to the log:
 * "portkeys: volume 5 -> 6 (VOL+)", "portkeys: brightness 7 -> 6 (MENU+VOL-)".
 *
 * THE LAUNCHER'S LEVELS, NOT STEPS OF ITS OWN. The twenty-one volume positions
 * and twelve backlight rungs are src/device/pixel2_levels.h's, turned into
 * register values by the same arithmetic, read from the launcher's settings
 * database when this starts and written back on every step. So the bar shows
 * where you are on the scale the shelf shows, and the shelf comes back at the
 * level you left Stardew at, instead of putting its own back. The gain is
 * written on the RK817's "Master Playback Volume" in the window of the output
 * in use (the jack is watched, as the launcher does) and on "USB Playback
 * Volume" for a USB-C DAC, by ioctl: no alsa-lib, no amixer on the system.
 * (alsa-lib only plays a screenshot's shutter, on the mix beside the port:
 * tools/pkshutter.h.)
 *
 * AND THE BOOST PAST THE TOP, AS THE LAUNCHER'S (src/boost.h, DESIGN.md 11.6).
 * At the top, VOL+ held a second more or pressed three more times opens cells
 * past it: the volume's are software gain, which this writes to the file the
 * port's ALSA plugin reads every period (src/eqdsp.h's eq_share, the launcher
 * being gone), and the backlight's a lift of the mid-tones in the display's
 * gamma table, loaded on the display borrowed for the bar. The cells are
 * kept beside the levels in the same database, written as this starts, and
 * drawn in the bar every frame while they charge, open, fill or fold;
 * tools/pkkeys.c holds the rules, and a plug in or out closes the volume's.
 *
 * THE BAR, ON THE PANEL'S CURSOR LAYER. The port owns the display: it is the
 * DRM master, and setting any plane is a master's ioctl, so no other process
 * can show anything while it runs - there is no compositor to ask. What this
 * does is borrow the port's own file: pidfd_getfd copies the port's open
 * /dev/dri/card0 into this process (root may), and a copy of a master's file
 * IS master. With it the bar goes up on the VOP's cursor window, above the
 * game, with the legacy cursor ioctl - which the kernel applies as an
 * asynchronous update, outside the commits the port's page flips go through,
 * so the game is neither waited on nor made to wait. Turning the window on
 * as a show starts and off as it ends are ordinary commits, and a flip the
 * port makes in the same refresh as either can be refused, a single repeated
 * frame each. The window is otherwise idle during a port: plastron's SDL
 * hides its pointer when the picture is turned. The VOP cannot turn a plane,
 * so the bar is drawn turned, the way the launcher turns its own picture onto
 * the 480x640 panel. It is drawn in the theme the launcher would be in: the
 * Appearance setting in the launcher's database, with Auto resolved at every
 * press by the launcher's own rule (src/theme.c) in the handheld's zone, so
 * the bar turns at the Night Hours' edges over a port as the shelf would (pkset.c,
 * which tools/portkeys-check.c holds). Only when the database cannot be read
 * is it what the launcher left in /tmp/zyperos-theme (src/themefile.h), which
 * nothing writes again while the port runs; so a hand edit of that file
 * changes the bar over a port only then.
 *
 * A borrowed master is held only while the bar shows. The copy keeps the
 * port's file open, and the master with it, for as long as it is held: a
 * port that closed the display and opened it again, or a second program
 * started by the port's script, or the launcher back from the port, could
 * not become master and would draw nothing. So the copy is let go of after
 * the bar's last frame, and at once if the port's process ends (its pidfd is
 * polled with the keys), and before every frame it is checked to still be
 * the file the port has open (kcmp, which compares the two open files
 * themselves) and still the master, in case the port closed the display or
 * gave the master up while the bar was up. The next press borrows it again.
 * launch.sh waits for this process to end before it starts the launcher.
 *
 * And never a file the program has only just opened. SDL's KMSDRM opens the
 * display as a program starts, takes the master, checks it and closes it
 * again, twice, in a few milliseconds, before it opens the display it keeps;
 * a copy taken then keeps that file and its master after SDL closed it, and
 * SDL's next open is refused the master: "kmsdrm not available", and the
 * program ends (your PICO-8, one start in two on 1.1.0-rc.9, while this
 * looked for its display once a second for On-Screen Stats). So a display
 * file is copied only once settled (tools/pkkeys.h's pk_looks): open under
 * the same number at a look PK_SETTLE_MS before, or the file of a process
 * whose master was held before. A look reads /proc's links and copies
 * nothing. A press in the meantime shows its bar once the file has settled;
 * MENU and a screenshot wait for it, PK_SETTLE_MS at most.
 *
 * No exclusive grab: the port and gptokeyb still see every key. */
#include <dirent.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/dma-buf.h>
#include <linux/input.h>
#include <linux/kcmp.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <sqlite3.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "battempty.h"
#include "device/pixel2_levels.h"
#include "heat.h"
#include "hud.h"
#include "hudmenu.h"
#include "idle.h"
#include "look.h"
#include "theme.h"
#include "osdbar.h"
#include "panelgamma.h"
#include "p8draw.h"
#include "p8fx.h"
#include "pkhot.h"
#include "pkkbd.h"
#include "pkkeys.h"
#include "pkover.h"
#include "pkset.h"
#include "pkfeel.h"
#include "pkshot.h"
#include "pkshutter.h"
#include "pkssh.h"
#include "pkvpad.h"
#include "powerhold.h"
#include "portmenu.h"
#include "ctl.h"
#include "zychord.h"
#include "sidelights.h"
#include "status.h"
#include "themefile.h"

/* TERM, from launch.sh as the port ends: Developer SSH is stopped first
 * (pkssh_stop), its sessions with it, then this ends, as it ended on TERM
 * before. A flag, which the main loop and menu_show's wait look at. */
static volatile sig_atomic_t g_term;
static void on_term(int sig) { (void)sig; g_term = 1; }

#ifndef SYS_pidfd_open
#define SYS_pidfd_open 434
#endif
#ifndef SYS_pidfd_getfd
#define SYS_pidfd_getfd 438
#endif
#ifndef SYS_kcmp
#define SYS_kcmp 272                 /* asm-generic's, which is aarch64's */
#endif
#ifndef DRM_IOCTL_MODE_GETFB2        /* Linux 5.7 */
#define DRM_IOCTL_MODE_GETFB2 DRM_IOWR(0xCE, struct drm_mode_fb_cmd2)
#endif

/* tools/pkkeys.h writes the kernel's numbers out, to build on the host */
_Static_assert(PK_KEY_VOLUMEUP == KEY_VOLUMEUP && PK_KEY_VOLUMEDOWN == KEY_VOLUMEDOWN &&
               PK_BTN_MENU == BTN_TRIGGER_HAPPY1, "pkkeys.h's key codes are <linux/input.h>'s");

/* An event's time, by the names the kernel's headers have used since 4.16 */
#ifndef input_event_sec
#define input_event_sec  time.tv_sec
#define input_event_usec time.tv_usec
#endif

#define BITS_PER_LONG   (sizeof(long) * 8)
#define NLONGS(n)       (((n) + BITS_PER_LONG - 1) / BITS_PER_LONG)
#define HAS(bits, b)    ((bits[(b) / BITS_PER_LONG] >> ((b) % BITS_PER_LONG)) & 1)

/* The kernel's control structs, vendored as Diatom does (port/pixel2.c) */
struct pk_ctl_elem_id {
	unsigned int numid; int iface; unsigned int device, subdevice;
	unsigned char name[44]; unsigned int index;
};
struct pk_aes_iec958 {
	unsigned char status[24], subcode[147], pad, dig_subframe[4];
};
/* Every member kept, not only the one used: the ioctl number carries the
 * struct's size, and a union one member short is a different ioctl */
struct pk_ctl_elem_value {
	struct pk_ctl_elem_id id;
	unsigned int indirect: 1;
	union {
		union { long value[128]; long *value_ptr; } integer;
		union { long long value[64]; long long *value_ptr; } integer64;
		union { unsigned int item[128]; unsigned int *item_ptr; } enumerated;
		union { unsigned char data[512]; unsigned char *data_ptr; } bytes;
		struct pk_aes_iec958 iec958;
	} value;
	unsigned char reserved[128];
};
#define PK_CTL_ELEM_WRITE  _IOWR('U', 0x13, struct pk_ctl_elem_value)

#define BACKLIGHT  "/sys/class/backlight/backlight/brightness"
#define CARD       "/dev/dri/card0"
#define PANEL_S    0.625f            /* the panel's pixels per launcher unit */

static int mixer = -1, jack = -1;
static pk_levels L = { -1, -1 };     /* positions; -1 not known yet */
/* The boost past the top of each (pkkeys.h): its cells, kept beside the
 * levels, the volume's as the gain in the file the port's plugin reads, the
 * backlight's as the lift in the display's table (port_gamma) */
static pk_boost BO;
static sqlite3 *db;
/* MENU > Appearance as the launcher saved it, "" for never; read once, since
 * nothing can change it while a port runs. appearance_read is whether the
 * database answered at all: if not, the bar goes by THEME_FILE. */
static char appearance_word[16];
static bool appearance_read;

/* The look and Auto's hours from the device's database, as the launcher reads
 * them (src/sysmenu_screen.c, look_load): look.* words and the Night Hours' minutes. The bar's colors
 * for both themes are then the look's (osdbar_colors), or the tokens' for the
 * look as it ships. */
static void look_load_db(sqlite3 *d)
{
	char ac[16] = "", li[16] = "", dk[16] = "", co[16] = "", zy[8] = "", m[16];
	int lm = THEME_NIGHT_UNTIL, dm = THEME_NIGHT_FROM, t;
	look l;

	pkset_text(d, "look.accent", ac, sizeof ac);
	pkset_text(d, "look.light", li, sizeof li);
	pkset_text(d, "look.dark", dk, sizeof dk);
	pkset_text(d, "look.corners", co, sizeof co);
	pkset_text(d, "look.zyper", zy, sizeof zy);
	l = look_parse(ac, li, dk, co, zy);
	look_set(&l);
	for (t = 0; t < DS_THEMES; t++)
		osdbar_colors(t, look_is_default(&l) ? NULL : theme_at(t));
	/* Auto's hours are the Night Hours (src/theme.h): light from
	 * night.until, dark from night.from, as the launcher's night_theme_hours
	 * hands them in. The launcher migrated 1.0.5's auto.light and auto.dark
	 * into them before any port could run. */
	if (pkset_text(d, "night.until", m, sizeof m)) lm = atoi(m);
	if (pkset_text(d, "night.from", m, sizeof m)) dm = atoi(m);
	theme_set_hours(lm, dm);
}

static long now_ms(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}

/* ---- the levels ------------------------------------------------------------ */

static void ctl_write(const char *name, long raw)
{
	struct pk_ctl_elem_value v;

	if (mixer < 0) return;
	memset(&v, 0, sizeof v);
	v.id.iface = 2;                     /* SNDRV_CTL_ELEM_IFACE_MIXER */
	snprintf((char *)v.id.name, sizeof v.id.name, "%s", name);
	/* Both channels: one written alone reads as a balance problem */
	v.value.integer.value[0] = raw;
	v.value.integer.value[1] = raw;
	ioctl(mixer, PK_CTL_ELEM_WRITE, &v);   /* no DAC, no USB control: fine */
}

static int jack_in(void)
{
	unsigned long sw[NLONGS(SW_MAX + 1)] = { 0 };

	if (jack < 0 || ioctl(jack, EVIOCGSW(sizeof sw), sw) < 0) return 0;
	return HAS(sw, SW_HEADPHONE_INSERT) ? 1 : 0;
}

static void apply_volume(void)
{
	if (L.vol < 0) return;
	ctl_write(GAIN_CTL, px2_gain_raw(L.vol, jack_in()));
	ctl_write(USB_CTL, px2_usb_raw(L.vol));
}

/* A plug in or out, or a resync that read the jack's switch again: the other
 * output's window, as the launcher does (plat_audio_jack_poll), and an output
 * that did change closes the volume boost, so the new one starts at the top
 * without it (src/levels.c's levels_output_changed). The first reading is
 * main's, as the port starts; a resync that finds the plug as it was is no
 * change. */
static int jack_was = -1;

static void plug_changed(void)
{
	int in = jack_in();

	if (jack_was >= 0 && in != jack_was && pk_boost_output_changed(db, &BO))
		fprintf(stderr, "portkeys: the output changed, so the volume boost is closed\n");
	jack_was = in;
	apply_volume();
}

static void apply_brightness(void)
{
	FILE *f = fopen(BACKLIGHT, "w");

	if (!f) return;
	fprintf(f, "%d\n", bright_ladder[L.bright]);
	fclose(f);
}

/* ---- the bar ----------------------------------------------------------------
 *
 * Borrowed for one show: the port's DRM file, its pid and pidfd, the number
 * the port has the file open under (what kcmp compares ours with), and two
 * dumb buffers on it, the bar drawn turned into whichever is not on screen. */

static struct {
	int fd, pidfd, pid, their_fd;
	uint32_t crtc;
	int crtc_index;              /* its place in the card's list, as possible_crtcs counts */
	int pw, ph;                  /* the panel's mode: 480 x 640 */
	int cw;                      /* a buffer's width: bh, padded to 16 pixels */
	uint32_t handle[2], pitch, size;
	uint32_t *map[2];
	uint32_t fb[2];              /* overlay fallback only */
	uint32_t plane;              /* the same */
	int cur;
	bool cursor;                 /* the cursor window took it */
} G = { .fd = -1, .pidfd = -1 };

static struct {
	int kind, level, max;
	long shown, changed;
	bool live;
	int theme;                   /* DS_LIGHT or DS_DARK, as bar_show resolved it at the last press */
	float drawn_a, drawn_rise;   /* what is on the panel, to skip repeats */
	osdbar_look drawn;           /* the same */
	int drawn_theme;
} B;

static int bw, bh;               /* the bar as the player sees it: 373 x 58 */

/* The low-battery disc and On-Screen Stats' line over your PICO-8, and what
 * is on the window: see "the disc and the stats line", below */
#define PK_HUD_KEY "hud"       /* src/game_menu.h's HUD_KEY, the switch */
#define PK_CPUFREQ "/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq"

static struct {
	bool allowed;                /* the --menu tag's program (port_menu_overlays) */
	int lowbat;                  /* status_batt_level's: 0, 1 low, 2 critical */
	bool hud_on, clock24;        /* On-Screen Stats, the player's, and MENU > Clock */
	hud_style st;
	char text[192];              /* the line now, "" for none */
	int theme;                   /* the theme it was drawn in */
	int hw, hh, hx, hy;          /* its pixels' size and place, landscape */
	unsigned gen;                /* a new one at each new drawing of it */
	long hud_next;               /* its next half second */
	long retry;                  /* the next borrow while wanted and not held */
	uint32_t crtc;               /* the panel's CRTC, its place in the list */
	int crtc_index;
	int vfd;                     /* a display file of this process's own, or -1 */
	bool vbl_pending;            /* a blank's event asked for on it */
	pk_fps fps;
	unsigned long long cpu_busy, cpu_all;
} O = { .vfd = -1 };
static uint32_t hud_px[640 * 160];

/* The cursor window refused a frame of everything (scene_put): the bar alone
 * on an overlay plane from then on, and no notice, disc or line */
static bool cursor_refused;

/* What is on the window, to draw again only for a change. `on`: a frame of
 * this process's is up on the window now. */
static struct {
	bool on;
	bool bar;
	float a, rise;
	osdbar_look look;
	int theme;
	const uint32_t *note;
	bool disc;
	unsigned hud;                /* O.gen drawn, 0 for none */
} S;


/* The very ioctl the fallback needs, asked to set plane 0, which there is
 * none of: the master is refused for that (ENOENT), anyone else for not
 * being master (EACCES), before the plane is even looked for. */
static bool is_master(int fd)
{
	struct drm_mode_set_plane p;

	memset(&p, 0, sizeof p);
	return ioctl(fd, DRM_IOCTL_MODE_SETPLANE, &p) == 0 || errno != EACCES;
}

static void release(bool port_gone)
{
	int i;

	if (G.fd < 0) return;
	for (i = 0; i < 2; i++) {
		if (G.map[i]) munmap(G.map[i], G.size);
		G.map[i] = NULL;
	}
	/* The cursor window off, whether the port is still running or not, and
	 * before the file is closed: it is a master's ioctl, so it is done only
	 * while this copy still is the master. Its frame is made inside the kernel
	 * and is no file's, so nothing takes it down with the file, and one left on
	 * the window stays until a master changes it. A port that ends while the
	 * bar is up, still the master, has the window turned off here. One that
	 * gives the master up while the bar shows (SDL's KMSDRM does when its last
	 * window goes, and keeps the file) leaves a frame: held() lets the copy go
	 * and this ioctl is refused. The launcher clears such a frame as it starts,
	 * hiding SDL's pointer (SDL_ShowCursor(SDL_DISABLE), src/device/pixel2.c). */
	if (G.cursor) {
		struct drm_mode_cursor c = { .flags = DRM_MODE_CURSOR_BO, .crtc_id = G.crtc };
		ioctl(G.fd, DRM_IOCTL_MODE_CURSOR, &c);
	}
	/* With the port still running, take the rest of ours down and give the
	 * memory back. With it gone, its file goes with this last copy, and the
	 * kernel frees the buffers and removes the framebuffers ADDFB made, the
	 * overlay's, from their plane. */
	if (!port_gone) {
		if (G.plane && !G.cursor) {
			struct drm_mode_set_plane p = { .plane_id = G.plane, .crtc_id = G.crtc };
			ioctl(G.fd, DRM_IOCTL_MODE_SETPLANE, &p);
		}
		for (i = 0; i < 2; i++) {
			if (G.fb[i]) ioctl(G.fd, DRM_IOCTL_MODE_RMFB, &G.fb[i]);
			if (G.handle[i]) {
				struct drm_mode_destroy_dumb d = { .handle = G.handle[i] };
				ioctl(G.fd, DRM_IOCTL_MODE_DESTROY_DUMB, &d);
			}
		}
	}
	close(G.fd);
	if (G.pidfd >= 0) close(G.pidfd);
	memset(&G, 0, sizeof G);
	G.fd = G.pidfd = -1;
	memset(&S, 0, sizeof S);         /* nothing of ours on the window */
	/* A bar cut short goes away as one that ran its time does: an empty
	 * boost closes (bar_tick). It had no gain and no lift, so nothing is
	 * loaded again for it. */
	if (B.live) pk_boost_gone(db, &BO);
	B.live = false;
	fprintf(stderr, "portkeys: display let go%s\n", port_gone ? " (port ended)" : "");
}

/* Still the file the port has open, and still the master? Else the copy goes.
 *
 * kcmp compares two open files themselves, the same struct file or not, so a
 * port that closed the display, or opened something else under the same
 * number, says no. (Not /proc's fdinfo: the drm-client-id line is printed by
 * a driver's show_fdinfo, and rockchip's file operations, DEFINE_DRM_GEM_FOPS,
 * have none.) And a port can keep the file and give the master up: SDL drops
 * it when its last window goes, and keeps the file. */
static bool held(void)
{
	if (G.fd < 0) return false;
	if (syscall(SYS_kcmp, (pid_t)getpid(), (pid_t)G.pid, KCMP_FILE,
	            (unsigned long)G.fd, (unsigned long)G.their_fd) != 0 || !is_master(G.fd)) {
		release(false);
		return false;
	}
	return true;
}

/* The CRTC lighting the panel, its place in the card's list and its mode. */
static bool find_crtc(int fd)
{
	struct drm_mode_card_res res;
	uint32_t ids[8];
	unsigned i;

	memset(&res, 0, sizeof res);
	if (ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) return false;
	if (res.count_crtcs > 8) res.count_crtcs = 8;
	memset(&res, 0, sizeof res);
	res.count_crtcs = 8;
	res.crtc_id_ptr = (uintptr_t)ids;
	if (ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) return false;
	for (i = 0; i < res.count_crtcs && i < 8; i++) {
		struct drm_mode_crtc c = { .crtc_id = ids[i] };
		if (ioctl(fd, DRM_IOCTL_MODE_GETCRTC, &c) == 0 && c.mode_valid) {
			G.crtc = ids[i];
			G.crtc_index = (int)i;
			G.pw = c.mode.hdisplay;
			G.ph = c.mode.vdisplay;
			return true;
		}
	}
	return false;
}

/* Two ARGB buffers the bar's size, turned: bh wide, bw tall - but the width
 * padded to a multiple of 16 pixels, 64 for the bar's 58.
 *
 * The cursor ioctl takes a width and a height and no pitch: the kernel wraps
 * the buffer as rows of exactly width * 4 bytes (drm_mode_cursor_universal),
 * where rockchip makes a dumb buffer's rows a multiple of 64 bytes, for the
 * Mali. At 58 pixels a row would be 232 bytes to the window and 256 in the
 * buffer, and the bar would lean 24 bytes more on every row. 16 pixels are
 * 64 bytes, so a row of 64 is one the driver has no cause to pad. The six
 * columns past the bar stay clear: they fall on the far side of it from the
 * panel's edge, below it as the player sees it, so it is where it was. */
static size_t notice_bytes(void);

static bool buffers(void)
{
	int i, rows;

	G.cw = (bh + 15) / 16 * 16;
	/* Rows enough for the heat notice as well, which goes up on the same
	 * buffers in rows of its own width (scene_put), and over your PICO-8 for
	 * the whole panel's picture, the disc and the line far apart with the
	 * bar (and its slide) between: the cursor window reads its rows as the
	 * frame's width says, whatever the buffer was made as */
	{
		size_t most = notice_bytes();

		if (O.allowed) {
			const bool turned = G.ph > G.pw;
			const int lw = turned ? G.ph : G.pw, lh = turned ? G.pw : G.ph;
			size_t all = turned ? (size_t)((lh + 16 + 15) / 16 * 16) * 4 * lw
			                    : (size_t)((lw + 15) / 16 * 16) * 4 * (lh + 16);

			if (all > most) most = all;
		}
		rows = (int)((most + (size_t)G.cw * 4 - 1) / ((size_t)G.cw * 4));
	}
	if (rows < bw) rows = bw;
	for (i = 0; i < 2; i++) {
		struct drm_mode_create_dumb c = { .width = (uint32_t)G.cw, .height = (uint32_t)rows, .bpp = 32 };
		struct drm_mode_map_dumb m;
		void *p;

		if (ioctl(G.fd, DRM_IOCTL_MODE_CREATE_DUMB, &c) < 0) return false;
		G.handle[i] = c.handle;
		G.pitch = c.pitch;
		G.size = (uint32_t)c.size;
		memset(&m, 0, sizeof m);
		m.handle = c.handle;
		if (ioctl(G.fd, DRM_IOCTL_MODE_MAP_DUMB, &m) < 0) return false;
		p = mmap(NULL, G.size, PROT_READ | PROT_WRITE, MAP_SHARED, G.fd, (off_t)m.offset);
		if (p == MAP_FAILED) return false;
		G.map[i] = p;
		memset(p, 0, G.size);
	}
	return true;
}

static bool overlay_setup(void);

/* A program the in-game menu is starting again (PORT_MENU_RESTART), on its
 * way out: never borrowed from, so no copy of this process's keeps its
 * display, and the master with it, from the loading screen and the program
 * that start next. 0 for none. */
static pid_t gone_pid;

/* What the last look for the port's display found (master_find): 1 a
 * master's copy, 0 no program with the display, -1 a display file not
 * settled yet (tools/pkkeys.h's pk_looks: a program starting, SDL looking at
 * the display, which a copy would take from it) */
static pk_looks LOOKS;
static int last_find;

/* Every process but this one with the display open, until one of the copies
 * is the master's: that copy, in G with the pid, its pidfd, the number the
 * port has it under and the CRTC lighting the panel. Nothing made on it yet.
 * Only a settled display file is copied (pk_looks_seen): one the process has
 * had open since a look PK_SETTLE_MS before, or one of a process whose master
 * was held before. Returns 1, 0, or -1 as last_find says. */
static int master_find(void)
{
	DIR *proc = opendir("/proc");
	struct dirent *pe;
	int self = getpid();
	long now = now_ms();

	pk_looks_begin(&LOOKS, now);

	while (proc && (pe = readdir(proc))) {
		int pid = atoi(pe->d_name), pidfd;
		char dir[64];
		DIR *fds;
		struct dirent *fe;

		if (pid <= 1 || pid == self || pid == gone_pid) continue;
		snprintf(dir, sizeof dir, "/proc/%d/fd", pid);
		if (!(fds = opendir(dir))) continue;
		pidfd = -1;
		while ((fe = readdir(fds))) {
			char link[352], target[64];
			ssize_t n;
			int theirs = atoi(fe->d_name), fd;

			if (fe->d_name[0] == '.') continue;
			snprintf(link, sizeof link, "%s/%s", dir, fe->d_name);
			if ((n = readlink(link, target, sizeof target - 1)) <= 0) continue;
			target[n] = '\0';
			if (strcmp(target, CARD)) continue;
			/* Looked at, not copied, until it has settled */
			if (!pk_looks_seen(&LOOKS, pid, theirs, now)) continue;
			if (pidfd < 0 && (pidfd = (int)syscall(SYS_pidfd_open, pid, 0)) < 0) break;
			fd = (int)syscall(SYS_pidfd_getfd, pidfd, theirs, 0);
			if (fd < 0) continue;
			if (!is_master(fd) || !find_crtc(fd)) {
				close(fd);
				continue;
			}
			G.fd = fd;
			G.pidfd = pidfd;
			G.pid = pid;
			G.their_fd = theirs;
			closedir(fds);
			closedir(proc);
			pk_looks_end(&LOOKS, false);
			pk_looks_trust(&LOOKS, pid);
			return last_find = 1;
		}
		closedir(fds);
		if (pidfd >= 0) close(pidfd);
	}
	if (proc) closedir(proc);
	pk_looks_end(&LOOKS, true);
	return last_find = LOOKS.waiting ? -1 : 0;
}

static bool master_copy(void)
{
	return master_find() == 1;
}

/* The same for what the player asked for at once (MENU, a screenshot): a
 * display file not settled yet is waited for, looking again until it is,
 * PK_SETTLE_MS and a little at most, rather than taken for none - which for
 * MENU would be the menu over a loading screen the port has already left */
static bool master_copy_settled(void)
{
	long t0 = now_ms();
	int r;

	while ((r = master_find()) < 0 && now_ms() - t0 < PK_SETTLE_MS + 200) {
		long w = pk_looks_wait(&LOOKS, now_ms());
		struct timespec nap = { 0, (w < 5 ? 5 : w > 50 ? 50 : w) * 1000000L };

		nanosleep(&nap, NULL);
	}
	return r == 1;
}

/* The port's display, for the bar: its master copy and two buffers on it */
static bool borrow(void)
{
	if (!master_copy()) return false;
	/* The CRTC the blanks are counted on, kept past the copy (vbl_arm) */
	O.crtc = G.crtc;
	O.crtc_index = G.crtc_index;
	if (!buffers()) {
		fprintf(stderr, "portkeys: no buffers for the bar\n");
		release(false);
		return false;
	}
	/* The cursor window, until it says otherwise (cursor_refused). A frame
	 * on it (scene_put) is written in rows of its own width and the window
	 * told that width, whatever rows the driver made the buffer with; only
	 * the bar alone on an overlay plane goes by the buffer's (ADDFB2 takes
	 * the pitch) */
	G.cursor = !cursor_refused;
	if (!G.cursor) {
		fprintf(stderr, "portkeys: no cursor window; the bar on an overlay plane\n");
		if (!overlay_setup()) {
			fprintf(stderr, "portkeys: no overlay plane either; no bar\n");
			release(false);
			return false;
		}
	}
	B.drawn_a = -1;     /* nothing of ours on these buffers yet */
	fprintf(stderr, "portkeys: showing the bar over pid %d (%dx%d)\n", G.pid, G.pw, G.ph);
	return true;
}

/* The fallback when the CRTC has no cursor window, or one that will not take
 * the buffer: the first free overlay plane that can be on the panel's CRTC
 * and takes ARGB, with framebuffers of our own. Each update is then an
 * ordinary commit, and can cost the port a frame. */
static bool overlay_setup(void)
{
	struct drm_mode_get_plane_res res = { 0 };
	uint32_t ids[16] = { 0 };
	unsigned i;
	int k;

	res.count_planes = 16;
	res.plane_id_ptr = (uintptr_t)ids;
	if (ioctl(G.fd, DRM_IOCTL_MODE_GETPLANERESOURCES, &res) < 0) return false;
	for (i = 0; i < res.count_planes && i < 16 && !G.plane; i++) {
		uint32_t fmts[32] = { 0 };
		struct drm_mode_get_plane p = { .plane_id = ids[i], .count_format_types = 32,
		                                .format_type_ptr = (uintptr_t)fmts };
		unsigned f;

		/* possible_crtcs counts CRTCs by their place in the card's list */
		if (ioctl(G.fd, DRM_IOCTL_MODE_GETPLANE, &p) < 0 || p.fb_id ||
		    !((p.possible_crtcs >> G.crtc_index) & 1)) continue;
		for (f = 0; f < p.count_format_types && f < 32; f++)
			if (fmts[f] == 0x34325241) G.plane = ids[i];   /* DRM_FORMAT_ARGB8888 */
	}
	if (!G.plane) return false;
	for (k = 0; k < 2; k++) {
		struct drm_mode_fb_cmd2 fb = { .width = (uint32_t)bh, .height = (uint32_t)bw,
		                               .pixel_format = 0x34325241 };
		fb.handles[0] = G.handle[k];
		fb.pitches[0] = G.pitch;
		if (ioctl(G.fd, DRM_IOCTL_MODE_ADDFB2, &fb) < 0) return false;
		G.fb[k] = fb.fb_id;
	}
	return true;
}

/* The bar in its landscape place (x, y), turned onto the portrait panel the
 * way the launcher turns its picture (plat_save_screen: -90 degrees, the
 * landscape top along the panel's left edge): a landscape pixel (x, y) is the
 * panel's (y, W - 1 - x), W the landscape width, the panel's height. False
 * when the frame did not go up, and then the copy may have been let go of. */
static bool put(const osdbar_look *look, float alpha, float rise)
{
	static uint32_t flat[OSDBAR_W * OSDBAR_H];
	int next = G.cur ^ 1, x, y, lx, ly, px, py;
	uint32_t *dst = G.map[next];

	/* In the theme bar_show resolved at the last press, the one the shelf
	 * would be in at that minute, so over a port the bar is the shelf's.
	 * The buffers are osdbar_size's, the boost's room included (buffers). */
	osdbar_render_look(flat, bw, bh, bw, PANEL_S, B.theme, look, alpha, true);
	for (y = 0; y < bh; y++)
		for (x = 0; x < bw; x++)
			dst[(size_t)(bw - 1 - x) * (G.pitch / 4) + y] = flat[(size_t)y * bw + x];

	lx = (int)(OSDBAR_X * PANEL_S + 0.5f);
	ly = (int)((OSDBAR_Y - rise) * PANEL_S + 0.5f);
	px = ly;
	py = G.ph - lx - bw;
	if (G.cursor) {
		/* The whole padded width: the window reads rows of width * 4 */
		struct drm_mode_cursor c = {
			.flags = DRM_MODE_CURSOR_BO | DRM_MODE_CURSOR_MOVE, .crtc_id = G.crtc,
			.x = px, .y = py, .width = (uint32_t)G.cw, .height = (uint32_t)bw,
			.handle = G.handle[next] };
		if (ioctl(G.fd, DRM_IOCTL_MODE_CURSOR, &c) < 0) {
			/* Not master now (EACCES): no plane of any kind would take
			 * it, so the copy goes. No cursor window (ENXIO), or one that
			 * will not take this buffer (EINVAL): an overlay plane, for
			 * the rest of this show. Anything else is this frame's, and
			 * the next one tries the window again. */
			if (errno == EACCES) {
				release(false);
				return false;
			}
			if (errno != ENXIO && errno != EINVAL) return false;
			fprintf(stderr, "portkeys: no cursor window (%s), using an overlay plane\n",
			        strerror(errno));
			G.cursor = false;
			if (!overlay_setup()) {
				fprintf(stderr, "portkeys: no overlay plane either; no bar\n");
				release(false);
				return false;
			}
		}
	}
	if (!G.cursor) {
		struct drm_mode_set_plane p = {
			.plane_id = G.plane, .crtc_id = G.crtc, .fb_id = G.fb[next],
			.crtc_x = px, .crtc_y = py, .crtc_w = (uint32_t)bh, .crtc_h = (uint32_t)bw,
			.src_w = (uint32_t)bh << 16, .src_h = (uint32_t)bw << 16 };
		if (ioctl(G.fd, DRM_IOCTL_MODE_SETPLANE, &p) < 0) {
			if (errno == EACCES) release(false);
			return false;
		}
	}
	G.cur = next;
	return true;
}

/* A press that found the port's display not settled yet (master_find's
 * -1): its bar, shown once it has (bar_tick), PK_SETTLE_MS later at most,
 * and given up a second after the press. Never a copy in the meantime. */
static struct {
	bool on;
	int  kind, level, max;
	long at, until;
} PEND;

static void bar_show(int kind, int level, int max)
{
	long now = now_ms();
	float a, rise;

	if (!held() && !borrow()) {
		if (last_find < 0) {
			const long w = pk_looks_wait(&LOOKS, now);

			if (!PEND.on) PEND.until = now + 1000;
			PEND.on = true;
			PEND.kind = kind;
			PEND.level = level;
			PEND.max = max;
			PEND.at = now + (w < 1 ? 1 : w);
		}
		return;
	}
	PEND.on = false;
	/* The theme is worked out here, on every press, and not once at start:
	 * a port can run for hours, and Auto turns at the Night Hours whether the
	 * launcher is there to say so or not. The setting, read at start, and
	 * the clock, by the launcher's rule (pkset_bar_theme, theme_saved_at); a
	 * word in capitals or none at all reads as the launcher reads it. With no
	 * database to go by, the file the launcher left, read again on every
	 * press: it can be missing on a cold boot straight into a port, and is
	 * then Beard, the default, and not an error. With the database, the file
	 * is not read, so a hand edit of /tmp/zyperos-theme no longer changes the
	 * bar over a port. A change redraws a bar still on the panel too
	 * (bar_tick compares the theme with what it drew), and the next one comes
	 * up in the new theme either way. */
	B.theme = pkset_bar_theme(appearance_read ? appearance_word : NULL, time(NULL),
	                          THEME_FILE);
	/* With no database, the look's colors are the file's too, as the
	 * launcher last wrote them (themefile_read_bar); with one, they were
	 * set at start (look_load_db) */
	if (!appearance_read) {
		ds_theme c;

		if (themefile_read_bar(THEME_FILE, &c) == B.theme) osdbar_colors(B.theme, &c);
	}
	if (!B.live || !osdbar_envelope((uint32_t)(now - B.shown), (uint32_t)(now - B.changed), &a, &rise))
		B.shown = now;
	B.kind = kind;
	B.level = level;
	B.max = max;
	B.changed = now;
	B.live = true;
}

/* Out at once rather than after its hold: POWER let go before its hold was
 * done (pw_bar) */
static void bar_fade(void)
{
	long now = now_ms();

	if (B.live && now - B.changed < OSDBAR_HOLD_MS) B.changed = now - OSDBAR_HOLD_MS;
}

/* ---- "Getting hot" over a port ----------------------------------------------
 *
 * DESIGN.md 11.23's notice with the thermometer, which Diatom shows over a
 * game when the chip passes the kernel's second trip. The launcher renders it
 * as it hands over to the port (src/portmenu.h), in both themes, and here it
 * is read once at the start, scaled to the panel as Diatom scales it (the
 * launcher's 1024 units to 640) and kept. When src/heat.h's rules say so -
 * two readings at 85 C or more, 5 s apart, then quiet until the chip has
 * cooled under 80 C and five minutes have passed - it goes up for
 * DS_MS_HOLD_HEAT where Diatom puts it, at the bottom, centered, on the
 * cursor window the bar uses, in the same frame as the bar when a volume key
 * is pressed under it (scene_put). The chip is read with the battery, every
 * 5 s, so it costs the port nothing it was not already paying. */

static int read_level(const char *path);   /* with the battery, below */
static bool batt_level(int *pct, bool *charging);   /* the same */

static heat_state H;
static struct {
	uint32_t *px[DS_THEMES];     /* premultiplied ARGB, landscape, panel pixels */
	int w[DS_THEMES], h[DS_THEMES];
	bool live;                   /* up */
	long until;
	int theme;                   /* the theme it went up in */
	/* Another notice's pixels in its place (notice_show), NULL for the heat
	 * notice's */
	const uint32_t *own;
	int own_w, own_h;
} N;

/* The chip's temperature in mC, as the launcher reads it (plat_chip_temp):
 * /tmp/zyperos_fake_temp (C or mC) standing in, else the hotter of the
 * soc-thermal and gpu-thermal zones */
static bool chip_temp(int *mc)
{
	static char zone[2][48];
	static int nzones = -1;
	int v = read_level("/tmp/zyperos_fake_temp"), best = -1, i;

	if (v >= 0) {
		*mc = v < 1000 ? v * 1000 : v;
		return true;
	}
	if (nzones < 0) {
		nzones = 0;
		for (i = 0; i < 8 && nzones < 2; i++) {
			char path[64], type[32] = "";
			ssize_t r;
			int fd;

			snprintf(path, sizeof path, "/sys/class/thermal/thermal_zone%d/type", i);
			if ((fd = open(path, O_RDONLY | O_CLOEXEC)) < 0) continue;
			r = read(fd, type, sizeof type - 1);
			close(fd);
			type[r > 0 ? r : 0] = '\0';
			if (strncmp(type, "soc-thermal", 11) && strncmp(type, "gpu-thermal", 11)) continue;
			snprintf(zone[nzones++], sizeof zone[0], "/sys/class/thermal/thermal_zone%d/temp", i);
		}
	}
	for (i = 0; i < nzones; i++)
		if ((v = read_level(zone[i])) > best) best = v;
	if (best < 0) return false;
	*mc = best;
	return true;
}

/* A notice from the launcher's file: scaled by PANEL_S with a bilinear
 * sample of the premultiplied pixels, which keeps the text's edges as
 * Diatom's GPU does; malloc'd, NULL when there is none. With `slot`, the
 * empty square a "SLOT" trailer names (notice.h's NOTICE_SLOT: x, y, side,
 * corner), scaled the same way, or all 0 for none. */
static uint32_t *dtov_read(const char *path, int *pw, int *ph, int slot[4])
{
	FILE *f = fopen(path, "rb");
	uint8_t hdr[8], tr[12];
	uint32_t *src = NULL, *dst = NULL;
	int sw, sh, w, h, x, y;

	if (slot) memset(slot, 0, 4 * sizeof *slot);
	if (!f) return NULL;
	if (fread(hdr, 1, 8, f) != 8 || memcmp(hdr, "DTOV", 4)) goto done;
	sw = hdr[4] | hdr[5] << 8;
	sh = hdr[6] | hdr[7] << 8;
	w = (int)(sw * PANEL_S + 0.5f);
	h = (int)(sh * PANEL_S + 0.5f);
	if (sw <= 0 || sh <= 0 || w < 2 || h < 2 || sw > 1024 || sh > 768) goto done;
	if (!(src = malloc((size_t)sw * sh * 4)) || fread(src, 4, (size_t)sw * sh, f) != (size_t)sw * sh) goto done;
	if (slot && fread(tr, 1, sizeof tr, f) == sizeof tr && !memcmp(tr, "SLOT", 4))
		for (x = 0; x < 4; x++) slot[x] = (int)((tr[4 + x * 2] | tr[5 + x * 2] << 8) * PANEL_S + 0.5f);
	for (x = 0; x < sw * sh; x++) {              /* BGRA bytes are ARGB words: premultiplied */
		uint32_t c = src[x], a = c >> 24;

		src[x] = a << 24 | (((c >> 16) & 255) * a / 255) << 16 | (((c >> 8) & 255) * a / 255) << 8 |
		         ((c & 255) * a / 255);
	}
	if (!(dst = malloc((size_t)w * h * 4))) goto done;
	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++) {
			float fx = (x + 0.5f) / PANEL_S - 0.5f, fy = (y + 0.5f) / PANEL_S - 0.5f;
			int x0 = fx < 0 ? 0 : (int)fx, y0 = fy < 0 ? 0 : (int)fy;
			int x1 = x0 + 1 < sw ? x0 + 1 : x0, y1 = y0 + 1 < sh ? y0 + 1 : y0;
			float tx = fx - x0, ty = fy - y0;
			uint32_t c00 = src[y0 * sw + x0], c10 = src[y0 * sw + x1];
			uint32_t c01 = src[y1 * sw + x0], c11 = src[y1 * sw + x1], out = 0;
			int k;

			if (tx < 0) tx = 0;
			if (ty < 0) ty = 0;
			for (k = 0; k < 32; k += 8) {
				float v = (((c00 >> k) & 255) * (1 - tx) + ((c10 >> k) & 255) * tx) * (1 - ty) +
				          (((c01 >> k) & 255) * (1 - tx) + ((c11 >> k) & 255) * tx) * ty;

				out |= (uint32_t)(v + 0.5f) << k;
			}
			dst[(size_t)y * w + x] = out;
		}
	*pw = w;
	*ph = h;
done:
	free(src);
	fclose(f);
	return dst;
}

/* One theme's heat notice */
static void notice_load(int theme, const char *path)
{
	int w = 0, h = 0;
	uint32_t *px = dtov_read(path, &w, &h, NULL);

	if (!px) return;
	N.px[theme] = px;
	N.w[theme] = w;
	N.h[theme] = h;
}

/* The most either theme's notice needs of a buffer, turned, in rows of its own
 * width padded to 16 pixels, as the bar's are (buffers) */
static size_t notice_bytes(void)
{
	size_t most = 0;
	int t;

	for (t = 0; t < DS_THEMES; t++) {
		size_t n = (size_t)((N.h[t] + 15) / 16 * 16) * 4 * (size_t)N.w[t];

		if (N.px[t] && n > most) most = n;
	}
	return most;
}

/* The chip read, with the battery's cadence (heat_poll keeps HEAT_POLL_MS),
 * and the notice up when it is due */
static void heat_check(long now)
{
	heat_poll(&H, (unsigned)now, chip_temp);
	if (!heat_take(&H, (unsigned)now)) return;
	N.theme = pkset_bar_theme(appearance_read ? appearance_word : NULL, time(NULL), THEME_FILE);
	if (!N.px[N.theme]) {
		fprintf(stderr, "portkeys: getting hot (%d mC), but no notice to show\n", H.mc);
		return;
	}
	fprintf(stderr, "portkeys: getting hot (%d mC); the notice over the port\n", H.mc);
	N.live = true;
	N.own = NULL;
	N.until = now + DS_MS_HOLD_HEAT;
}

/* Another notice at the heat notice's place for `ms`, the next one taking
 * its place: premultiplied ARGB, landscape, in the panel's pixels, which the
 * caller keeps for that long (the frame copies nothing). NULL takes it down.
 * For the program's own pop-ups over it (a screenshot's). */
static void notice_show(const uint32_t *px, int w, int h, long ms)
{
	N.own = px;
	N.own_w = w;
	N.own_h = h;
	N.live = px && w > 0 && h > 0;
	N.until = now_ms() + ms;
}

/* What the bar shows now: the level's as its boost has it (pkkeys.h), which
 * moves while it charges, opens, fills or folds, and POWER's hold as it was */
static void bar_look(osdbar_look *l, long now)
{
	if (B.kind == OSDBAR_VOLUME || B.kind == OSDBAR_BRIGHT)
		pk_boost_look(&BO, B.kind == OSDBAR_BRIGHT ? PK_BRIGHT : PK_VOLUME, (uint32_t)now, l);
	else
		osdbar_look_plain(l, B.kind, B.level, B.max);
}

static void port_gamma(void);        /* with the keys, below */

/* The bar gone, its time run: an empty boost closes with it and a filled one
 * stays, as in the launcher (levels_bar_gone). Closing an empty one changes
 * neither the gain nor the lift, but the table is asked for all the same, on
 * the display still borrowed; it is left alone when it holds what is wanted. */
static void bar_gone(void)
{
	if (pk_boost_gone(db, &BO) & (1 << PK_BRIGHT)) port_gamma();
}

/* ---- the disc and the stats line over your PICO-8 ---------------------------
 *
 * Diatom draws two things over a game that no port had: the low-battery
 * disc (red, top right, blinking when critical) and On-Screen Stats' line
 * (src/hud.h), the player's items in the player's corner and style, while
 * the in-game menu's On-Screen Stats is on. Over your PICO-8 (the --menu tag,
 * src/portmenu.h's port_menu_overlays) this draws both on the cursor window
 * the bar uses, the bar and the notice in the same frame (scene_put,
 * tools/pkover.h), for as long as they are wanted:
 *
 *   the disc, by the battery's level at each reading (every 5 s, with the
 *   empty one): status.h's rule, the launcher's and Diatom's;
 *   the line, every half second as Diatom makes it: the frames a second
 *   counted here, Speed left out (PICO-8 says nothing of it), the chip's
 *   temperature, the CPU's load and clock, the battery and the time, in the
 *   look's colors of the theme the shelf would be in.
 *
 * The frames a second, as px2-shot --fps counts them: at every vertical
 * blank, whether the panel shows a new framebuffer. The blank is an event on
 * a display file of this process's own (O.vfd), never on the copy of the
 * program's, whose page flips' events the program reads itself; asked for
 * one blank at a time, and only while the line shows FPS.
 *
 * WHY ONLY YOUR PICO-8. Both stay up, so the copy of the program's display is
 * held for as long as they show, and with it its master (the top of this
 * file). A port's script can start one program after another, each wanting
 * the master; PICO-8.sh execs pico8_64, which opens its display once and
 * keeps it. The copy is let go of all the same when the program ends (its
 * pidfd), gives the master up, or the in-game menu opens, and borrowed
 * again a second later at the most. */

/* On-Screen Stats as the in-game menu keeps it (src/hudmenu.h), and the
 * clock's 12 or 24 hours: read at the start and after every menu */
static void over_load(void)
{
	char v[64];
	int k;

	O.hud_on = pkset_text(db, PK_HUD_KEY, v, sizeof v) && !strcmp(v, "1");
	O.st = hud_default;
	if (pkset_text(db, HM_KEY_ITEMS, v, sizeof v)) O.st.items = hud_items_parse(v);
	if (pkset_text(db, HM_KEY_CORNER, v, sizeof v) && (k = hm_parse(HM_CORNER, v)) >= 0) O.st.corner = (uint8_t)k;
	if (pkset_text(db, HM_KEY_EDGE, v, sizeof v) && (k = hm_parse(HM_EDGE, v)) >= 0) O.st.edge = (uint8_t)k;
	if (pkset_text(db, HM_KEY_BACKGROUND, v, sizeof v) && (k = hm_parse(HM_BACKGROUND, v)) >= 0)
		O.st.background = (uint8_t)k;
	if (pkset_text(db, HM_KEY_OPACITY, v, sizeof v) && (k = hm_parse(HM_OPACITY, v)) >= 0) O.st.opacity = (uint8_t)k;
	if (pkset_text(db, HM_KEY_COLOR, v, sizeof v) && (k = hm_parse(HM_COLOR, v)) >= 0) O.st.color = (uint8_t)k;
	if (pkset_text(db, HM_KEY_SIZE, v, sizeof v) && (k = hm_parse(HM_SIZE, v)) >= 0) O.st.size = (uint8_t)k;
	hud_clamp(&O.st);
	O.clock24 = pkset_text(db, "clock", v, sizeof v) && !strcmp(v, "24h");
	O.hud_next = 0;
	O.text[0] = '\0';
	O.gen++;
	if (O.allowed)
		fprintf(stderr, "portkeys: On-Screen Stats %s over the program\n", O.hud_on ? "on" : "off");
}

/* A battery reading, every BATT_EVERY_MS: the disc's level */
static void lowbat_reading(int pct, bool charging)
{
	int level = status_batt_level(pct, charging);

	if (O.allowed && level != O.lowbat)
		fprintf(stderr, "portkeys: battery %d%%%s, %s\n", pct, charging ? ", charging" : "",
		        level == 2 ? "the low-battery disc over the program, blinking"
		        : level ? "the low-battery disc over the program" : "no low-battery disc");
	O.lowbat = level;
}

static bool over_wanted(void)
{
	return O.allowed && !cursor_refused && (O.lowbat > 0 || O.hud_on);
}

static bool hud_fps(void)
{
	return O.allowed && O.hud_on && (O.st.items & (1u << HUD_FPS));
}

/* Busy against idle since the last look, from /proc/stat's first line, as
 * Diatom's line counts it; -1 the first time */
static int cpu_pct(void)
{
	unsigned long long v[10] = { 0 }, all = 0, idle, busy;
	char line[256] = "";
	FILE *f = fopen("/proc/stat", "re");
	int i, pct = -1;

	if (!f) return -1;
	if (!fgets(line, sizeof line, f)) line[0] = '\0';
	fclose(f);
	if (sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu", &v[0], &v[1], &v[2],
	           &v[3], &v[4], &v[5], &v[6], &v[7], &v[8], &v[9]) < 4)
		return -1;
	for (i = 0; i < 8; i++) all += v[i];      /* guest time is in user's already */
	idle = v[3] + v[4];
	busy = all - idle;
	if (O.cpu_all && all > O.cpu_all) pct = (int)((busy - O.cpu_busy) * 100 / (all - O.cpu_all));
	O.cpu_busy = busy;
	O.cpu_all = all;
	return pct;
}

/* The line's numbers now, and its pixels when it says something new or the
 * theme turned: the items asked for and only those */
static void hud_update(int lw, int lh)
{
	hud_values v = { O.fps.fps, -1.0, -1, -1, -1, -1, -1, -1, O.clock24 };
	const uint8_t items = O.st.items;
	char text[sizeof O.text];
	int theme, mc, khz, w, h;

	if (items & (1u << HUD_TEMP) && chip_temp(&mc)) v.temp_c = (mc + 500) / 1000;
	if (items & (1u << HUD_CPU)) v.cpu_pct = cpu_pct();
	if (items & (1u << HUD_CLOCK) && (khz = read_level(PK_CPUFREQ)) > 0) v.cpu_mhz = (khz + 500) / 1000;
	if (items & (1u << HUD_BATTERY)) {
		bool charging;

		if (!batt_level(&v.battery_pct, &charging)) v.battery_pct = -1;
	}
	if (items & (1u << HUD_TIME)) {
		time_t t = time(NULL);
		struct tm tm;

		if (localtime_r(&t, &tm)) {
			v.hour = tm.tm_hour;
			v.min = tm.tm_min;
		}
	}
	hud_text(text, sizeof text, &v, items);
	theme = pkset_bar_theme(appearance_read ? appearance_word : NULL, time(NULL), THEME_FILE);
	if (!strcmp(text, O.text) && theme == O.theme && O.hw) return;
	snprintf(O.text, sizeof O.text, "%s", text);
	O.theme = theme;
	O.hw = O.hh = 0;
	O.gen++;
	if (!text[0]) return;
	hud_size(text, PANEL_S, &O.st, &w, &h);
	if (w <= 0 || h <= 0 || (size_t)w * h > sizeof hud_px / sizeof hud_px[0]) return;
	{
		ds_theme c;
		const ds_theme *colors = theme_at(theme);

		/* With no database, the colors the launcher left, as Diatom
		 * reads them (themefile_read_bar) */
		if (!appearance_read && themefile_read_bar(THEME_FILE, &c) == theme) colors = &c;
		hud_render(hud_px, w, h, w, PANEL_S, text, &O.st, colors, true);
	}
	hud_place(&O.st, PANEL_S, w, h, lw, lh, &O.hx, &O.hy);
	O.hw = w;
	O.hh = h;
}

/* The blanks' file: this process's own, never the master (let go of at
 * once, as menu_waiting's: opened while nobody is master, a file becomes it) */
static void vbl_close(void)
{
	if (O.vfd >= 0) close(O.vfd);
	O.vfd = -1;
	O.vbl_pending = false;
	pkover_fps_lost(&O.fps);
}

static void vbl_arm(void)
{
	union drm_wait_vblank w;

	if (O.vfd < 0) {
		if (!O.crtc || (O.vfd = open(CARD, O_RDWR | O_CLOEXEC | O_NONBLOCK)) < 0) return;
		ioctl(O.vfd, DRM_IOCTL_DROP_MASTER, 0);
		pkover_fps_init(&O.fps);
	}
	if (O.vbl_pending) return;
	memset(&w, 0, sizeof w);
	w.request.type = _DRM_VBLANK_RELATIVE | _DRM_VBLANK_EVENT;
	if (O.crtc_index == 1) w.request.type |= _DRM_VBLANK_SECONDARY;
	else if (O.crtc_index > 1)
		w.request.type |= ((unsigned)O.crtc_index << _DRM_VBLANK_HIGH_CRTC_SHIFT) & _DRM_VBLANK_HIGH_CRTC_MASK;
	w.request.sequence = 1;
	if (ioctl(O.vfd, DRM_IOCTL_WAIT_VBLANK, &w) == 0) {
		O.vbl_pending = true;
	} else {
		fprintf(stderr, "portkeys: no vertical blanks to count (%s); no FPS\n", strerror(errno));
		vbl_close();
		O.crtc = 0;                  /* found again at the next borrow */
	}
}

/* The file the main loop polls for the blanks, -1 for none */
static int over_poll_fd(void)
{
	return O.vbl_pending ? O.vfd : -1;
}

/* A blank came: the framebuffer the panel shows now, counted, and the next
 * blank asked for */
static void over_vblank(void)
{
	char buf[256];
	ssize_t r;
	bool blank = false;

	while ((r = read(O.vfd, buf, sizeof buf)) > 0) {
		ssize_t i = 0;

		while (i + (ssize_t)sizeof(struct drm_event) <= r) {
			const struct drm_event *e = (const void *)(buf + i);

			if (e->length < sizeof *e) break;
			if (e->type == DRM_EVENT_VBLANK) blank = true;
			i += e->length;
		}
	}
	if (!blank) return;
	O.vbl_pending = false;
	{
		struct drm_mode_crtc c = { .crtc_id = O.crtc };

		if (ioctl(O.vfd, DRM_IOCTL_MODE_GETCRTC, &c) == 0)
			pkover_fps_blank(&O.fps, c.fb_id, (uint64_t)now_ms());
	}
	if (hud_fps()) vbl_arm();
}

/* The disc's and the line's state at `now`, and how long until either
 * changes by itself: the next half second, the next blink */
static int over_tick(long now, int lw, int lh)
{
	int wait = -1;

	if (!O.allowed) return -1;
	if (O.hud_on && now >= O.hud_next) {
		O.hud_next = now + HUD_EVERY_MS;
		hud_update(lw, lh);
	}
	if (O.hud_on) wait = (int)(O.hud_next - now);
	if (O.lowbat == 2) {
		int b = (int)(1000 - now % 1000);

		if (wait < 0 || b < wait) wait = b;
	}
	if (hud_fps() && O.crtc) vbl_arm();
	else if (O.vfd >= 0) vbl_close();
	return wait;
}

/* Everything up into one frame on the cursor window (tools/pkover.h): the
 * line, the notice, the bar at `a` and `rise` when `look` is given, the disc.
 * False when it did not go up (and then the copy may have been let go of). */
static bool scene_put(const osdbar_look *look, float a, float rise, bool disc)
{
	static uint32_t flat[OSDBAR_W * OSDBAR_H];
	static uint32_t *scratch;
	const bool turned = G.ph > G.pw;
	const int lw = turned ? G.ph : G.pw, lh = turned ? G.pw : G.ph;
	int next = G.cur ^ 1;
	pk_scene sc;
	pk_rect u;
	pk_window w;
	struct drm_mode_cursor c;

	if (!scratch && !(scratch = malloc((size_t)(lw + 64) * (lh + 64) * 4))) return false;
	memset(&sc, 0, sizeof sc);
	sc.lw = lw;
	sc.lh = lh;
	if (O.allowed && O.hud_on && O.hw)
		sc.layer[PK_LAYER_HUD] = (pk_layer){ hud_px, O.hx, O.hy, O.hw, O.hh };
	if (N.live) {
		const uint32_t *px = N.own ? N.own : N.px[N.theme];
		int nw = N.own ? N.own_w : N.w[N.theme], nh = N.own ? N.own_h : N.h[N.theme];

		if (px) sc.layer[PK_LAYER_NOTICE] = (pk_layer){ px, (lw - nw) / 2, lh - nh - lh / 24, nw, nh };
	}
	if (look && a > 0) {
		/* In the theme bar_show resolved at the last press, the one the
		 * shelf would be in at that minute, so over a port the bar is the
		 * shelf's */
		osdbar_render_look(flat, bw, bh, bw, PANEL_S, B.theme, look, a, true);
		sc.layer[PK_LAYER_BAR] = (pk_layer){ flat, (int)(OSDBAR_X * PANEL_S + 0.5f),
		                                     (int)((OSDBAR_Y - rise) * PANEL_S + 0.5f), bw, bh };
	}
	sc.disc = disc;
	u = pkover_union(&sc);
	if (!u.w) return false;
	if (u.w > lw + 64 || u.h > lh + 64) return false;
	w = pkover_window(u, turned, G.ph);
	if ((size_t)w.bw * 4 * w.bh > G.size) {
		fprintf(stderr, "portkeys: a frame of %dx%d is more than the buffers hold\n", w.bw, w.bh);
		return false;
	}
	pkover_render(&sc, u, w, turned, G.map[next], scratch);
	memset(&c, 0, sizeof c);
	c.flags = DRM_MODE_CURSOR_BO | DRM_MODE_CURSOR_MOVE;
	c.crtc_id = G.crtc;
	c.x = w.x;
	c.y = w.y;
	c.width = (uint32_t)w.bw;
	c.height = (uint32_t)w.bh;
	c.handle = G.handle[next];
	if (ioctl(G.fd, DRM_IOCTL_MODE_CURSOR, &c) < 0) {
		/* Not master now (EACCES): the copy goes. No cursor window
		 * (ENXIO), or one that will not take this buffer (EINVAL): the
		 * bar alone on an overlay plane for the rest of this show (put).
		 * Anything else is this frame's, tried again at the next. */
		if (errno == EACCES) {
			release(false);
			return false;
		}
		if (errno != ENXIO && errno != EINVAL) return false;
		fprintf(stderr, "portkeys: no cursor window (%s), the bar alone on an overlay plane\n",
		        strerror(errno));
		G.cursor = false;
		cursor_refused = true;
		if (!overlay_setup()) {
			fprintf(stderr, "portkeys: no overlay plane either; no bar\n");
			release(false);
		}
		return false;
	}
	G.cur = next;
	return true;
}

/* One frame of everything's life, if it changed; how long until the next.
 * While the bar slides or fades that is a refresh, 16 ms, and a frame the
 * window did not take is tried again at the next. Every frame first asks
 * whether the copy is still the port's file and still the master. When the
 * last thing up has gone, the window off and the copy let go of: the next
 * press borrows it again, and the disc or the line a second later at most.
 * Turning the window off and on again is an ordinary commit at each end of a
 * show, where a window kept on with nothing in it would have cost none after
 * the first; but a copy kept between shows keeps the port's master with it
 * (see the top of this file), which only the disc and the line over your
 * PICO-8 do. */
static int bar_tick_now(void);

/* bar_tick_now, and a press's bar waiting for the port's display (PEND) */
static int bar_tick(void)
{
	long now = now_ms();
	int w;

	if (PEND.on && now >= PEND.at) {
		if (now >= PEND.until) {
			PEND.on = false;
		} else {
			const int kind = PEND.kind;

			bar_show(kind, PEND.level, PEND.max);
			if (kind == OSDBAR_BRIGHT && held()) port_gamma();
		}
	}
	w = bar_tick_now();
	if (PEND.on) {
		const long pw = PEND.at > now_ms() ? PEND.at - now_ms() : 1;

		if (w < 0 || pw < w) w = (int)pw;
	}
	return w;
}

static int bar_tick_now(void)
{
	long now = now_ms();
	float a = 0, rise = 0;
	bool alive = false, disc, changed;
	osdbar_look look;
	int wait = -1, ow, lw = 640, lh = 480;

	memset(&look, 0, sizeof look);
	heat_check(now);
	if (N.live && now >= N.until) N.live = false;
	if (G.fd >= 0) {
		lw = G.ph > G.pw ? G.ph : G.pw;
		lh = G.ph > G.pw ? G.pw : G.ph;
	}
	ow = over_tick(now, lw, lh);
	disc = O.allowed && pkover_disc_on(O.lowbat, (uint64_t)now);
	if (!B.live && !N.live && !over_wanted()) {
		if (G.fd >= 0) release(false);
		return -1;
	}
	if (G.fd < 0) {
		/* The bar borrows at its press (bar_show); the notice, the disc
		 * and the line here */
		if (!N.live && now < O.retry) return (int)(O.retry - now);
		if (!borrow()) {
			/* A display file not settled yet: looked at again as it
			 * settles, PK_SETTLE_MS on, and copied only then */
			const long w = last_find < 0 ? pk_looks_wait(&LOOKS, now) : -1;

			N.live = false;          /* a notice with no display to go on */
			O.retry = now + (w >= 0 ? (w < 10 ? 10 : w) : 1000);
			return over_wanted() ? (int)(O.retry - now) : -1;
		}
	} else if (!held()) {
		O.retry = now + 1000;
		return over_wanted() || N.live ? 200 : -1;
	}
	if (B.live) {
		alive = osdbar_envelope((uint32_t)(now - B.shown), (uint32_t)(now - B.changed), &a, &rise);
		if (!alive) a = 0, rise = 0;
		bar_look(&look, now);
	}
	if (!G.cursor) {
		/* The overlay plane's fallback: the bar alone, as before the
		 * window held more; a notice has nowhere to go */
		N.live = false;
		if (B.live && (a != B.drawn_a || rise != B.drawn_rise || memcmp(&look, &B.drawn, sizeof look) ||
		               B.theme != B.drawn_theme)) {
			if (!put(&look, a, rise) && alive) return G.fd < 0 ? -1 : 16;
			B.drawn_a = a;
			B.drawn_rise = rise;
			B.drawn = look;
			B.drawn_theme = B.theme;
		}
	} else {
		const uint32_t *note = N.live ? (N.own ? N.own : N.px[N.theme]) : NULL;
		bool bar = B.live && a > 0;

		changed = !S.on || bar != S.bar || note != S.note || disc != S.disc ||
		          (O.hud_on && O.hw ? O.gen : 0) != S.hud;
		if (bar && !changed)
			changed = a != S.a || rise != S.rise || memcmp(&look, &S.look, sizeof look) || B.theme != S.theme;
		if (changed) {
			if (!bar && !note && !disc && !(O.hud_on && O.hw)) {
				/* Nothing to show this frame (the line has nothing to
				 * say yet, the disc blinks off): the window empty */
				if (S.on) {
					struct drm_mode_cursor c = { .flags = DRM_MODE_CURSOR_BO, .crtc_id = G.crtc };

					ioctl(G.fd, DRM_IOCTL_MODE_CURSOR, &c);
				}
				S.on = false;
			} else if (scene_put(B.live ? &look : NULL, a, rise, disc)) {
				S.on = true;
			} else if (G.fd < 0) {
				return -1;
			} else if (alive) {
				return 16;
			}
			S.bar = bar;
			S.a = a;
			S.rise = rise;
			S.look = look;
			S.theme = B.theme;
			S.note = note;
			S.disc = disc;
			S.hud = O.hud_on && O.hw ? O.gen : 0;
		}
	}
	if (B.live && !alive) {
		bar_gone();
		B.live = false;
		/* The others go on; nothing else up, and the window goes off now */
		if (!N.live && !over_wanted()) {
			release(false);
			return -1;
		}
		return 0;
	}
	if (B.live) {
		/* The boost charging, opening, filling or folding, or VOL+ held on
		 * the top while its hold charges: every frame until it stops, as
		 * the launcher draws it (plat_osd_until) */
		if (B.kind != OSDBAR_POWER && pk_boost_moving(&BO, (uint32_t)now)) return 16;
		{
			uint32_t bw_ = osdbar_next_change((uint32_t)(now - B.shown), (uint32_t)(now - B.changed));

			wait = bw_ ? (int)bw_ : 16;
		}
	}
	if (N.live && (wait < 0 || N.until - now < wait)) wait = (int)(N.until - now);
	if (ow >= 0 && (wait < 0 || ow < wait)) wait = ow;
	return wait;
}

/* ---- the keys ---------------------------------------------------------------- */

/* The display's gamma table for the rung and the Screen Colors
 * (src/panelgamma.h): the two under the backlight's floor are the table's,
 * 9/17 and 13/17 of the floor's light.
 * Set on the port's display, borrowed for the bar just shown - a master's
 * ioctl, as the bar's are. The launcher set it before the port started and
 * sets it again when it comes back; this is for the steps in between. */
/* Night Mode as the launcher left it (src/nightmode.h): its warmth and
 * nights, read at the start and at each step, and the warmth the table has
 * (night_on): the clock is looked at once a second (night_tick) */
static panelgamma_night night;
static int night_on;
static time_t night_looked;

static void port_gamma(void)
{
	int raw = bright_ladder[L.bright], n;
	panelgamma_color c;

	if (!held()) {
		fprintf(stderr, "portkeys: no display borrowed; the gamma table for brightness %d "
		        "waits for the launcher\n", raw);
		return;
	}
	/* With the player's Screen Colors and Night Mode, as the launcher left
	 * them, and the brightness boost's lift, which is this one's (pkkeys.h) */
	panelgamma_screen_read(PANELGAMMA_COLOR_FILE, &c, &night);
	c.night = night_on = panelgamma_night_at(&night, (long long)time(NULL));
	c.lift = pk_boost_lift(&BO);
	n = panelgamma_apply_color(G.fd, raw, &c);
	if (n > 0)
		fprintf(stderr, "portkeys: the display's gamma table set for brightness %d, lift %d\n", raw,
		        c.lift);
	else if (n < 0)
		fprintf(stderr, "portkeys: could not set the display's gamma table for brightness %d\n",
		        raw);
}

/* Night Mode's hour over the port, once a second: the table for the rung,
 * the Screen Colors and the warmth due now, at once when that warmth is not
 * the one on (one blink, no fade). On the port's display, borrowed for the
 * bar if it is up, else a master's copy taken for the load and let go. */
static void night_tick(void)
{
	time_t now = time(NULL);
	panelgamma_color c;
	bool borrowed;
	int w, n;

	if (now == night_looked) return;
	night_looked = now;
	w = panelgamma_night_at(&night, (long long)now);
	if (w == night_on) return;
	night_on = w;
	borrowed = held();
	if (!borrowed && !master_copy()) {
		fprintf(stderr, "portkeys: Night Mode %s, but no display to load the table on\n",
		        w ? "on" : "off");
		return;
	}
	panelgamma_color_read(PANELGAMMA_COLOR_FILE, &c);
	c.night = w;
	c.lift = pk_boost_lift(&BO);
	n = panelgamma_apply_color(G.fd, bright_ladder[L.bright], &c);
	fprintf(stderr, "portkeys: Night Mode %s%s\n", w ? "on" : "off",
	        n < 0 ? ": could not set the display's gamma table" : "");
	if (!borrowed) release(false);
}

/* Milliseconds to Night Mode's next change, or -1 when none is due */
static int night_wait(void)
{
	long long now = (long long)time(NULL), next = panelgamma_night_next(&night, now);

	if (!next) return -1;
	return next - now > 3600 ? 3600 * 1000 : (int)(next - now) * 1000 + 50;
}

/* The brightness boost kept from before, on the display as portkeys starts.
 * The launcher left its table with the lift as it handed over, so this is for
 * a display that lost it meanwhile; it is left alone when it holds what is
 * wanted. On a master's copy taken for the load and let go, as night_tick's;
 * with no program on the display yet, the next step loads it. */
static void lift_start(void)
{
	panelgamma_color c;
	int n;

	if (!pk_boost_lift(&BO)) return;
	if (!master_copy()) {
		fprintf(stderr, "portkeys: no display yet for the brightness boost's lift %d; the next step "
		        "loads it\n", pk_boost_lift(&BO));
		return;
	}
	panelgamma_color_read(PANELGAMMA_COLOR_FILE, &c);
	c.night = night_on;
	c.lift = pk_boost_lift(&BO);
	n = panelgamma_apply_color(G.fd, bright_ladder[L.bright], &c);
	fprintf(stderr, "portkeys: the brightness boost's lift %d%s\n", c.lift,
	        n < 0 ? ": could not set the display's gamma table" : n ? ", loaded" : ", already on");
	release(false);
}

/* The boost's buzz as it opens (tools/pkkeys.h's pk_buzz_pct), on the
 * joypad's motor, as the launcher gives it (plat_rumble_buzz,
 * src/device/pixel2.c): the motor is the joypad's force feedback (PWM0, the
 * joypad driver's), so the joypad's node, found by its FF_RUMBLE as portkeys
 * starts (buzz_node), is opened again to write at the first buzz and kept.
 * The effect is this file's alone: a port's own rumble (SDL's, on its own
 * file) plays beside it. The joypad held over your PICO-8 (EVIOCGRAB) still
 * takes it: a grab keeps the joypad's events from other readers, not writes
 * to it, as px2-input's injected presses show. ff-memless ends it after
 * PK_BUZZ_MS and tells the driver 0, whatever portkeys does next. Before
 * this, a boost opened over a port gave no buzz at all. A screenshot over
 * your PICO-8 buzzes the same way (shot_feel). */
static char buzz_node[300];   /* as the scan's path */
static int buzz_fd = -1, buzz_id = -1;
static bool buzz_failed;

/* One buzz, PK_BUZZ_MS at `pct` of full strength; `what` for the log */
static void buzz(int pct, const char *what)
{
	struct ff_effect e;
	struct input_event ev;
	const char *why = NULL;

	if (pct <= 0) return;      /* Rumble Off: nothing to feel */
	if (buzz_fd < 0 && buzz_node[0]) buzz_fd = open(buzz_node, O_RDWR | O_NONBLOCK | O_CLOEXEC);
	if (buzz_fd < 0) {
		why = buzz_node[0] ? strerror(errno) : "no joypad with a motor was found";
	} else {
		memset(&e, 0, sizeof e);
		e.type = FF_RUMBLE;
		e.id = (int16_t)buzz_id;
		/* The driver runs the motor from the strong one; both, as SDL sends */
		e.u.rumble.strong_magnitude = pk_buzz_strength(pct);
		e.u.rumble.weak_magnitude = pk_buzz_strength(pct);
		e.replay.length = PK_BUZZ_MS;
		if (ioctl(buzz_fd, EVIOCSFF, &e) < 0) {
			why = strerror(errno);
			buzz_id = -1;
		} else {
			buzz_id = e.id;
			memset(&ev, 0, sizeof ev);
			ev.type = EV_FF;
			ev.code = (uint16_t)buzz_id;
			ev.value = 1;
			if (write(buzz_fd, &ev, sizeof ev) != (ssize_t)sizeof ev) why = strerror(errno);
		}
	}
	if (!why)
		fprintf(stderr, "portkeys: the %s's buzz, %d ms at %d%%\n", what, PK_BUZZ_MS, pct);
	else if (!buzz_failed)
		fprintf(stderr, "portkeys: no buzz for the %s (%s): %s\n", what, buzz_node[0] ? buzz_node : "-", why);
	buzz_failed = buzz_failed || why;
}

static void boost_buzz(int did)
{
	buzz(pk_buzz_pct(db, did), "boost");
}

/* One step from tools/pkkeys.c, through the boost past the top (pk_press):
 * the level, saved when it moved, its line in the log, the hardware and the
 * bar. A press writes the hardware even at an end of the scale, as it always
 * has, which puts the level back if the port moved the mixer itself; a repeat
 * that moved nothing writes nothing, and keeps the bar up. The boost's cells
 * moving are kept, and the volume's gain written, by pk_press; their lift is
 * loaded here, on the display the bar has just borrowed; and a press that
 * opens it buzzes (boost_buzz). */
static void press(const pk_step *s)
{
	char line[96];
	int did = pk_press(db, &L, &BO, s, (uint32_t)now_ms(), line, sizeof line);
	bool moved = did & BOOST_LEVEL, vol = s->kind != PK_BRIGHT;

	if (line[0]) fputs(line, stderr);
	if (!vol) {
		if (moved || !s->held) apply_brightness();
		bar_show(OSDBAR_BRIGHT, L.bright, BRIGHT_MAX);
		if (moved || !s->held || (did & BOOST_GAIN)) port_gamma();
	} else {
		if (moved || !s->held) apply_volume();
		bar_show(OSDBAR_VOLUME, L.vol, VOL_MAX);
	}
	/* And on the side lights, as the launcher does (src/sidelights.h) */
	if ((vol ? L.vol : L.bright) >= 0) {
		side_lights_level_line(line, sizeof line, vol, vol ? L.vol : L.bright,
		                       vol ? VOL_MAX : BRIGHT_MAX);
		side_lights_say_to(SIDE_LIGHTS_FIFO, line);
	}
	boost_buzz(did);
}

/* The boost's clock, on every pass of the loop after its steps
 * (pk_boost_tick): VOL+ let go, a hold on the top charging and, held
 * BOOST_HOLD_MS, opening; presses that stopped draining. A hold that opens it
 * is shown and loaded as a press that does: the bar, and for the backlight
 * the lift; the gain is in the file already. And the buzz, as the
 * launcher's as it opens (boost_buzz). */
static void boost_clock(const pkkeys *k)
{
	const uint32_t now = (uint32_t)now_ms();
	int i;

	for (i = 0; i < 2; i++) {
		const pk_kind kind = i ? PK_BRIGHT : PK_VOLUME;
		const int did = pk_boost_tick(db, &BO, k, kind, now);

		if (!(did & BOOST_GAIN)) continue;
		if (kind == PK_BRIGHT) {
			fprintf(stderr, "portkeys: brightness boost 0 -> %d (MENU+VOL+ held)\n", pk_boost_lift(&BO));
			bar_show(OSDBAR_BRIGHT, L.bright, BRIGHT_MAX);
			port_gamma();
		} else {
			fprintf(stderr, "portkeys: volume boost +0 -> +%d dB (VOL+ held)\n", pk_boost_gain(&BO));
			bar_show(OSDBAR_VOLUME, L.vol, VOL_MAX);
		}
		boost_buzz(did);
	}
}

/* The battery as the launcher reads it (plat_battery, src/device/pixel2.c):
 * the level from the fuel gauge, and for the charger the cable, not the
 * battery's status, which says Discharging on the cable too; the status only
 * when the charger has no online file. /tmp/zyperos_fake_batt, a level, stands
 * in for both, as it does for the launcher. False with no level to read. And
 * off the charger its voltage, voltage_avg in mV (plat_charge_read's), 0 for
 * none: read with the fake level too, which says nothing of a voltage. */
static int read_level(const char *path)
{
	char buf[16];
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	ssize_t r;

	if (fd < 0) return -1;
	r = read(fd, buf, sizeof buf - 1);
	close(fd);
	if (r <= 0) return -1;
	buf[r] = '\0';
	return atoi(buf);
}

static bool batt_level(int *pct, bool *charging)
{
	char st[16];
	int v = read_level("/tmp/zyperos_fake_batt"), fd;
	ssize_t r;

	*charging = false;
	if (v >= 0) {
		*pct = v;
		return true;
	}
	if ((*pct = read_level("/sys/class/power_supply/battery/capacity")) < 0) return false;
	v = read_level("/sys/class/power_supply/rk817-charger/online");
	if (v == 0 || v == 1) {
		*charging = v == 1;
		return true;
	}
	if ((fd = open("/sys/class/power_supply/battery/status", O_RDONLY | O_CLOEXEC)) >= 0) {
		r = read(fd, st, sizeof st - 1);
		close(fd);
		if (r > 0) {
			st[r] = '\0';
			*charging = !strncmp(st, "Charging", 8) || !strncmp(st, "Full", 4);
		}
	}
	return true;
}

static bool batt_now(int *pct, bool *charging, int *mv)
{
	bool ok = batt_level(pct, charging);
	int uv = ok && !*charging ? read_level("/sys/class/power_supply/battery/voltage_avg") : -1;

	*mv = uv > 0 ? uv / 1000 : 0;
	return ok;
}

/* ---- the in-game menu, over a port that has one -----------------------------
 *
 * OpenBOR has no libretro core, so it runs as a program of its own, as the
 * standalone Flycast does when a card adds it, and as every port and
 * PortMaster itself do, and the launcher, which draws the in-game menu over a
 * game Diatom runs, has exited for it. With --menu (launch.sh gives it for
 * any "run:" program and any port, src/portmenu.h), MENU on its own
 * (pkkeys_menu_tapped: pressed and let go with no brightness step under it,
 * as Diatom reads it) brings the launcher back for its menu, src/portmenu.h:
 *
 *   1. The port stops where it is, its whole process group (SIGSTOP): the
 *      game is paused, its sound with it, and its last frame stays lit, since
 *      nothing flips it; a helper its script started (gptokeyb) stops with
 *      it. OpenBOR takes a second's gap as one tick of game time
 *      (getinterval), so nothing in the game moved meanwhile.
 *   2. That frame is saved, as the panel shows it, for the menu's backdrop
 *      (still_save), and the port's display is let go of: DROP_MASTER on the
 *      borrowed copy of its file, which root may do for a file it did not
 *      open, as the bar's borrow is (see the top of this file).
 *   3. The launcher starts with --port-menu, takes the display as it takes it
 *      from the boot splash, shows the in-game menu over the saved frame and
 *      exits with what was chosen. It reads the keys itself meanwhile, the
 *      volume ones too, and saves the levels in the same database; this
 *      process waits for it and reads nothing. Over a program MP4 plays on
 *      over (your PICO-8), MP4's follower is stopped meanwhile (--follow,
 *      follow_hold), as SELECT there opens MP4 in the menu.
 *   4. The display goes back to the port (SET_MASTER on the same copy), its
 *      frame is put back on the panel if the launcher left another, what was
 *      pressed for the menu is emptied out of the input queues of every
 *      process in the group (port_input_flush), and it goes on (SIGCONT).
 *      The levels are
 *      read again, and the keys here resynced (in the loop).
 *   5. Quit: launch.sh ends the port, as it ends one for POWER but without
 *      powering off (SIGUSR2), and the shelf comes back. POWER in the menu,
 *      or the battery run flat there: SIGUSR1, as POWER over the port.
 *
 * While the port plays none of this costs it anything: a MENU tap is a key
 * read here like any other, and the port's frames are its own. */

static const char *menu_elf, *menu_tag, *menu_game;   /* --menu ELF TAG GAME */
/* --follow PID: MP4's follower beside a program that keeps MP4 playing
 * (your PICO-8; zyperos.elf --mp4-follow, src/mp4hand.h), stopped while the
 * menu is up, the menu's MP4 being the one that talks to MP4's daemon then
 * (SELECT there), and let go on after it, when it takes the queue the menu
 * left. 0 for every other program. */
static pid_t follow_pid;

static void follow_hold(bool hold)
{
	if (follow_pid > 0) kill(follow_pid, hold ? SIGSTOP : SIGCONT);
}

/* Over your PICO-8 (port_hotkeys): the keyboard for CTRL-R (pkkbd.h), made
 * as this starts; and what the launcher handed over, read again after each
 * menu (keys_load, with the hotkeys below) */
static int kbd = -1;
static void keys_load(void);
/* The joypad let go of, and the pad PICO-8 reads all up (tools/pkvpad.h),
 * before anything else may read it: the launcher's menu */
static void vpad_let_go(void);

static void nap_ms(int ms)
{
	struct timespec t = { ms / 1000, (long)(ms % 1000) * 1000000L };

	nanosleep(&t, NULL);
}

static void end_port(void);          /* with the power key, below */
static void wait_released(int ms);   /* with the keys, below */

/* The process stopped, as /proc says: state T. The name in its stat line can
 * hold spaces and parentheses, so the state is read after the last ')'. */
static bool stopped(int pid)
{
	char path[64], buf[512], *p;
	ssize_t r;
	int fd;

	snprintf(path, sizeof path, "/proc/%d/stat", pid);
	if ((fd = open(path, O_RDONLY | O_CLOEXEC)) < 0) return false;
	r = read(fd, buf, sizeof buf - 1);
	close(fd);
	if (r <= 0) return false;
	buf[r] = '\0';
	p = strrchr(buf, ')');
	return p && p[1] == ' ' && (p[2] == 'T' || p[2] == 't');
}

static bool write_all(int fd, const void *buf, size_t n)
{
	const uint8_t *b = buf;

	while (n) {
		ssize_t w = write(fd, b, n);

		if (w < 0 && errno == EINTR) continue;
		if (w <= 0) return false;
		b += w;
		n -= (size_t)w;
	}
	return true;
}

/* The frame on the panel, framebuffer `fb_id`, as the panel shows it. Its
 * buffer is mapped as the card's own dumb buffer, which Mesa makes a scanout
 * buffer as on this panel, or else as the dma-buf it is; a frame in any other
 * form than plain 32-bit rows is not read, and the menu then sits on the
 * theme's ground instead. Write-combined memory on the Pixel 2: still_save
 * copies its 1.2 MB once, a screenshot reads 128 x 128 of its pixels and no
 * more (shot_take). */
typedef struct {
	const uint8_t *px;           /* the first row */
	uint8_t       *map;
	size_t         size;
	uint32_t       w, h, pitch, handle;
	int            dmabuf;
} frame_map;

static void frame_close(frame_map *m)
{
	if (m->dmabuf >= 0) {
		struct dma_buf_sync sy = { DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ };

		ioctl(m->dmabuf, DMA_BUF_IOCTL_SYNC, &sy);
		close(m->dmabuf);
	}
	if (m->map && m->map != MAP_FAILED) munmap(m->map, m->size);
	/* The handle GETFB2 made, in the port's file: a new one of its own, so
	 * closing it leaves the port's handles to the same buffer as they were */
	if (m->handle) {
		struct drm_gem_close gc = { .handle = m->handle };

		ioctl(G.fd, DRM_IOCTL_GEM_CLOSE, &gc);
	}
	memset(m, 0, sizeof *m);
	m->dmabuf = -1;
}

static bool frame_open(uint32_t fb_id, frame_map *m)
{
	struct drm_mode_fb_cmd2 f;
	struct drm_mode_map_dumb md;
	size_t row;

	memset(m, 0, sizeof *m);
	m->dmabuf = -1;
	memset(&f, 0, sizeof f);
	f.fb_id = fb_id;
	if (ioctl(G.fd, DRM_IOCTL_MODE_GETFB2, &f) < 0 || !f.handles[0]) {
		fprintf(stderr, "portkeys: the port's frame cannot be read (%s)\n", strerror(errno));
		return false;
	}
	m->handle = f.handles[0];
	row = (size_t)f.width * 4;
	if ((f.pixel_format != 0x34325258 && f.pixel_format != 0x34325241) ||   /* XRGB8888, ARGB8888 */
	    ((f.flags & DRM_MODE_FB_MODIFIERS) && f.modifier[0] != 0) ||          /* not linear */
	    !f.width || !f.height || f.width > 4096 || f.height > 4096 || f.pitches[0] < row) {
		fprintf(stderr, "portkeys: the port's frame is format %08x, modifier %llx; not read\n",
		        f.pixel_format, (unsigned long long)f.modifier[0]);
		frame_close(m);
		return false;
	}
	m->size = (size_t)f.offsets[0] + (size_t)f.pitches[0] * f.height;
	m->map = MAP_FAILED;
	memset(&md, 0, sizeof md);
	md.handle = f.handles[0];
	if (ioctl(G.fd, DRM_IOCTL_MODE_MAP_DUMB, &md) == 0)
		m->map = mmap(NULL, m->size, PROT_READ, MAP_SHARED, G.fd, (off_t)md.offset);
	if (m->map == MAP_FAILED) {
		struct drm_prime_handle ph = { .handle = f.handles[0], .flags = DRM_CLOEXEC };

		if (ioctl(G.fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &ph) == 0) {
			m->dmabuf = ph.fd;
			m->map = mmap(NULL, m->size, PROT_READ, MAP_SHARED, m->dmabuf, 0);
		}
	}
	if (m->map == MAP_FAILED) {
		fprintf(stderr, "portkeys: the port's frame cannot be mapped (%s); not read\n", strerror(errno));
		frame_close(m);
		return false;
	}
	if (m->dmabuf >= 0) {
		struct dma_buf_sync sy = { DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ };

		ioctl(m->dmabuf, DMA_BUF_IOCTL_SYNC, &sy);
	}
	m->px = m->map + f.offsets[0];
	m->w = f.width;
	m->h = f.height;
	m->pitch = f.pitches[0];
	return true;
}

/* Saved to PORT_MENU_STILL, for the menu's backdrop */
static bool still_save(uint32_t fb_id)
{
	frame_map m;
	uint8_t *out, hdr[8];
	size_t row;
	uint32_t y;
	int fd;
	bool ok;

	if (!frame_open(fb_id, &m)) return false;
	row = (size_t)m.w * 4;
	if (!(out = malloc(row * m.h))) {
		frame_close(&m);
		return false;
	}
	for (y = 0; y < m.h; y++) memcpy(out + y * row, m.px + (size_t)y * m.pitch, row);
	memcpy(hdr, PORT_MENU_MAGIC, 4);
	hdr[4] = (uint8_t)m.w;  hdr[5] = (uint8_t)(m.w >> 8);
	hdr[6] = (uint8_t)m.h;  hdr[7] = (uint8_t)(m.h >> 8);
	frame_close(&m);
	fd = open(PORT_MENU_STILL ".tmp", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	ok = fd >= 0 && write_all(fd, hdr, sizeof hdr) && write_all(fd, out, row * (size_t)(hdr[6] | hdr[7] << 8));
	if (fd >= 0 && close(fd)) ok = false;
	ok = ok && rename(PORT_MENU_STILL ".tmp", PORT_MENU_STILL) == 0;
	if (!ok) unlink(PORT_MENU_STILL ".tmp");
	free(out);
	return ok;
}

/* The connector the panel's CRTC lights, for putting the port's frame back */
static uint32_t crtc_connector(void)
{
	struct drm_mode_card_res res;
	uint32_t ids[8];
	unsigned i;

	memset(&res, 0, sizeof res);
	res.count_connectors = 8;
	res.connector_id_ptr = (uintptr_t)ids;
	if (ioctl(G.fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) return 0;
	for (i = 0; i < res.count_connectors && i < 8; i++) {
		struct drm_mode_get_connector c = { .connector_id = ids[i] };
		struct drm_mode_get_encoder e;

		if (ioctl(G.fd, DRM_IOCTL_MODE_GETCONNECTOR, &c) < 0 || !c.encoder_id) continue;
		memset(&e, 0, sizeof e);
		e.encoder_id = c.encoder_id;
		if (ioctl(G.fd, DRM_IOCTL_MODE_GETENCODER, &e) == 0 && e.crtc_id == G.crtc) return ids[i];
	}
	return 0;
}

/* What was pressed while the menu was up was the launcher's, and the port has
 * it all queued for the moment it goes on: the A that chose Continue would be
 * an attack. So would a helper the port's script started beside it, which
 * reads the pad too, gptokeyb turning it into keys -
 * and would type it all into the game. Each event device any process of the
 * port's group has open has the clock it is read on changed and changed
 * back, which empties that queue and leaves the kernel's SYN_DROPPED in it
 * (evdev_set_clk_type). SDL reads that as events lost and asks the device
 * what is down now, so the port sees the buttons as they are and nothing
 * pressed for the menu; a helper that does not read SYN_DROPPED so (gptokeyb)
 * loses only a release that came while the menu was up. Each is left on the
 * wall clock, the kernel's own default, which SDL does not change. */
static int input_flush_pid(int pid)
{
	char dir[64];
	DIR *fds;
	struct dirent *fe;
	int flushed = 0, pidfd;

	snprintf(dir, sizeof dir, "/proc/%d/fd", pid);
	if (!(fds = opendir(dir))) return 0;
	if ((pidfd = (int)syscall(SYS_pidfd_open, pid, 0)) < 0) {
		closedir(fds);
		return 0;
	}
	while ((fe = readdir(fds))) {
		char link[352], target[64];
		ssize_t r;
		int fd, mono = CLOCK_MONOTONIC, wall = CLOCK_REALTIME;

		if (fe->d_name[0] == '.') continue;
		snprintf(link, sizeof link, "%s/%s", dir, fe->d_name);
		if ((r = readlink(link, target, sizeof target - 1)) <= 0) continue;
		target[r] = '\0';
		if (strncmp(target, "/dev/input/event", 16)) continue;
		if ((fd = (int)syscall(SYS_pidfd_getfd, pidfd, atoi(fe->d_name), 0)) < 0) continue;
		ioctl(fd, EVIOCSCLOCKID, &mono);
		ioctl(fd, EVIOCSCLOCKID, &wall);
		close(fd);
		flushed++;
	}
	close(pidfd);
	closedir(fds);
	return flushed;
}

/* The process group of a process, from its stat line (after the name) */
static int pgrp_of(int pid)
{
	char path[64], buf[512], *p, state;
	int ppid = 0, pg = -1;
	ssize_t r;
	int fd;

	snprintf(path, sizeof path, "/proc/%d/stat", pid);
	if ((fd = open(path, O_RDONLY | O_CLOEXEC)) < 0) return -1;
	r = read(fd, buf, sizeof buf - 1);
	close(fd);
	if (r <= 0) return -1;
	buf[r] = '\0';
	if (!(p = strrchr(buf, ')')) || sscanf(p + 1, " %c %d %d", &state, &ppid, &pg) != 3) return -1;
	return pg;
}

static void port_input_flush(int pg)
{
	DIR *proc = opendir("/proc");
	struct dirent *pe;
	int queues = 0, procs = 0;

	while (proc && (pe = readdir(proc))) {
		int pid = atoi(pe->d_name), n;

		if (pid <= 1 || pgrp_of(pid) != pg) continue;
		if ((n = input_flush_pid(pid)) > 0) procs++;
		queues += n;
	}
	if (proc) closedir(proc);
	fprintf(stderr, "portkeys: %d input queue(s) of %d process(es) emptied of the menu's presses\n",
	        queues, procs);
}

/* MemAvailable, in MB, -1 when /proc does not say */
static long mem_available_mb(void)
{
	char buf[2048], *p;
	int fd = open("/proc/meminfo", O_RDONLY | O_CLOEXEC);
	ssize_t r;

	if (fd < 0) return -1;
	r = read(fd, buf, sizeof buf - 1);
	close(fd);
	if (r <= 0) return -1;
	buf[r] = '\0';
	if (!(p = strstr(buf, "MemAvailable:"))) return -1;
	return atol(p + 13) / 1024;
}

/* MENU tapped: the in-game menu, as at the top of this section. 0 when there
 * was none (no --menu, or no port holding the display); 1 when it was up, and
 * this process's keys and levels are then to be read again. */
/* The port's group while nothing holds the display yet: the child of
 * launch.sh (this process's parent) that leads a group of its own, as the
 * port's script does under setsid. 0 for none. Splash and this process are
 * launch.sh's children too, in its group. */
static pid_t port_group(void)
{
	DIR *proc = opendir("/proc");
	struct dirent *pe;
	pid_t self = getpid(), parent = getppid(), found = 0;

	while (proc && (pe = readdir(proc))) {
		char path[64], line[512], *close_paren;
		int pid = atoi(pe->d_name), ppid = 0, pgrp = 0, fd;
		ssize_t n;

		if (pid <= 1 || pid == self) continue;
		snprintf(path, sizeof path, "/proc/%d/stat", pid);
		if ((fd = open(path, O_RDONLY | O_CLOEXEC)) < 0) continue;
		n = read(fd, line, sizeof line - 1);
		close(fd);
		if (n <= 0) continue;
		line[n] = '\0';
		/* After the name, which may hold spaces and parentheses */
		if (!(close_paren = strrchr(line, ')')) ||
		    sscanf(close_paren + 1, " %*c %d %d", &ppid, &pgrp) != 2)
			continue;
		if (ppid == parent && pgrp == pid) {
			found = pid;
			break;
		}
	}
	if (proc) closedir(proc);
	return found;
}

/* MENU while a port is still starting: no program holds the display yet,
 * and the loading screen splash holds is on the panel. The same in-game menu
 * over that screen, Continue named Keep Waiting (PORT_MENU_WAITING), so a
 * port that hangs as it starts is MENU and Quit away from the shelf, and one
 * that is only slow goes on (1.0.6; before, MENU did nothing until the port
 * drew, and POWER or MENU+START held were the only ways out). The port's
 * group is stopped while the menu is up, as over a running port: one that
 * reached its display meanwhile would find the menu's master in its way.
 * 1 when the menu was up. */
static int menu_waiting(void)
{
	struct drm_mode_crtc c;
	uint32_t conn;
	pid_t pg = port_group(), child;
	int fd, status = 0, how = PORT_MENU_CONTINUE, i;
	long t0 = now_ms();

	if ((fd = open(CARD, O_RDWR | O_CLOEXEC)) < 0) return 0;
	/* Opened while nobody is master (splash let go of it), this file has
	 * become master, and the menu could never take the display: let go.
	 * Nothing below needs it until the menu has ended. */
	ioctl(fd, DRM_IOCTL_DROP_MASTER, 0);
	G.fd = fd;
	if (!find_crtc(fd)) {
		G.fd = -1;
		close(fd);
		return 0;
	}
	/* For splash: what the log gets from here on is the menu's, not the
	 * port's (PORT_MENU_UP), until the loading screen is back */
	if ((i = open(PORT_MENU_UP, O_WRONLY | O_CREAT | O_CLOEXEC, 0644)) >= 0) close(i);
	fprintf(stderr, "portkeys: MENU while the port starts (group %d); the menu over its loading screen\n",
	        (int)pg);
	if (pg > 1) kill(-pg, SIGSTOP);
	memset(&c, 0, sizeof c);
	c.crtc_id = G.crtc;
	if (ioctl(fd, DRM_IOCTL_MODE_GETCRTC, &c) < 0) c.fb_id = 0;
	unlink(PORT_MENU_STILL);
	if (c.fb_id) still_save(c.fb_id);
	conn = crtc_connector();

	follow_hold(true);
	child = fork();
	if (child == 0) {
		int o = open("/proc/self/oom_score_adj", O_WRONLY | O_CLOEXEC);

		if (o >= 0) {
			if (write(o, PORT_MENU_OOM, strlen(PORT_MENU_OOM)) < 0) {}
			close(o);
		}
		execl(menu_elf, menu_elf, PORT_MENU_ARG, menu_tag, menu_game, PORT_MENU_WAITING,
		      (char *)NULL);
		_exit(127);
	}
	if (child < 0) fprintf(stderr, "portkeys: cannot start the menu (%s)\n", strerror(errno));
	while (child > 0 && waitpid(child, &status, 0) < 0 && errno == EINTR && !g_term) {}
	follow_hold(false);
	if (child > 0 && WIFEXITED(status) &&
	    (WEXITSTATUS(status) == PORT_MENU_QUIT || WEXITSTATUS(status) == PORT_MENU_POWER ||
	     WEXITSTATUS(status) == PORT_MENU_RESTART))
		how = WEXITSTATUS(status);

	/* The loading screen back on the panel: the launcher's picture went
	 * with it. Set even when the panel still shows the splash's buffer (the
	 * menu could not draw): a display that keeps its own copy of it (QEMU's)
	 * shows what splash wrote meanwhile, its after-a-minute line, only once
	 * set. Nobody holds the master now, so this file can */
	for (i = 0; i < 150 && ioctl(fd, DRM_IOCTL_SET_MASTER, 0) < 0; i++) usleep(2000);
	if (i < 150) {
		if (c.fb_id && c.mode_valid && conn) {
			c.set_connectors_ptr = (uintptr_t)&conn;
			c.count_connectors = 1;
			if (ioctl(fd, DRM_IOCTL_MODE_SETCRTC, &c) < 0)
				fprintf(stderr, "portkeys: cannot put the loading screen back (%s)\n", strerror(errno));
		}
		ioctl(fd, DRM_IOCTL_DROP_MASTER, 0);
	}
	G.fd = -1;
	close(fd);
	wait_released(1500);
	/* The menu has ended: the port's lines again, from when it goes on */
	unlink(PORT_MENU_UP);
	if (pg > 1) {
		port_input_flush(pg);
		kill(-pg, SIGCONT);
	}
	pk_levels_load(db, BACKLIGHT, &L);
	pk_boost_load(db, &L, &BO);
	fprintf(stderr, "portkeys: the menu was up %ld ms while the port started: %s\n", now_ms() - t0,
	        how == PORT_MENU_QUIT ? "Quit" : how == PORT_MENU_POWER ? "POWER" :
	        how == PORT_MENU_RESTART ? "start again" : "Keep Waiting");
	/* Quit, or a start again (a Display mode over your PICO-8): launch.sh
	 * tells the two apart by the restart file the menu wrote (port_quit) */
	if (how == PORT_MENU_QUIT || how == PORT_MENU_RESTART) {
		pid_t parent = getppid();

		if (parent > 1) kill(parent, SIGUSR2);
	} else if (how == PORT_MENU_POWER) {
		end_port();
	}
	return 1;
}

/* `extra` after the tag and the game, NULL for none: PORT_MENU_RESET_ARG for
 * Reset Game's hotkey, which asks its question alone (hot_do) */
static int menu_show_as(const char *extra)
{
	struct drm_mode_crtc c;
	uint32_t conn;
	pid_t pg, child;
	long t0 = now_ms();
	int pid, status = 0, how = PORT_MENU_CONTINUE, i;

	if (!menu_elf) return 0;
	/* The bar down and let go of, and the display found afresh; the blanks
	 * are the menu's while it is up, not the program's; and the joypad the
	 * launcher's again, nothing held on PICO-8's pad (vpad_let_go) */
	release(false);
	vbl_close();
	vpad_let_go();
	if (!master_copy_settled()) {
		/* Still starting, under its loading screen: the menu over that;
		 * a hotkey's question (`extra`) has no cart to be about yet */
		if (extra) {
			fprintf(stderr, "portkeys: a hotkey's question, but nothing on the display yet\n");
			return 0;
		}
		if (menu_waiting()) return 1;
		fprintf(stderr, "portkeys: MENU, but no program holds the display; no menu\n");
		return 0;
	}
	pid = G.pid;
	pg = getpgid(pid);
	if (pg <= 1 || pg == getpgrp()) {
		fprintf(stderr, "portkeys: MENU, but pid %d is not a port's; no menu\n", pid);
		release(false);
		return 0;
	}
	fprintf(stderr, "portkeys: MENU, the in-game menu over pid %d\n", pid);
	kill(-pg, SIGSTOP);
	for (i = 0; i < 50 && !stopped(pid); i++) usleep(2000);
	/* A refresh more, for a flip it had asked for to land: then the frame on
	 * the panel is the one it stopped on */
	usleep(20000);
	memset(&c, 0, sizeof c);
	c.crtc_id = G.crtc;
	if (ioctl(G.fd, DRM_IOCTL_MODE_GETCRTC, &c) < 0) c.fb_id = 0;
	unlink(PORT_MENU_STILL);
	if (c.fb_id) still_save(c.fb_id);
	conn = crtc_connector();
	if (ioctl(G.fd, DRM_IOCTL_DROP_MASTER, 0) < 0) {
		fprintf(stderr, "portkeys: cannot let go of the port's display (%s); no menu\n", strerror(errno));
		kill(-pg, SIGCONT);
		release(false);
		return 0;
	}

	/* The memory the menu starts into: a port such as Stardew Valley leaves
	 * little, and the menu, not the stopped game, is what the kernel takes
	 * if it runs out (the child's oom_score_adj, src/portmenu.h) */
	fprintf(stderr, "portkeys: %ld MB free for the menu\n", mem_available_mb());
	follow_hold(true);
	child = fork();
	if (child == 0) {
		int fd = open("/proc/self/oom_score_adj", O_WRONLY | O_CLOEXEC);

		if (fd >= 0) {
			if (write(fd, PORT_MENU_OOM, strlen(PORT_MENU_OOM)) < 0) {}
			close(fd);
		}
		execl(menu_elf, menu_elf, PORT_MENU_ARG, menu_tag, menu_game, extra, (char *)NULL);
		_exit(127);
	}
	if (child < 0) fprintf(stderr, "portkeys: cannot start the menu (%s)\n", strerror(errno));
	while (child > 0 && waitpid(child, &status, 0) < 0 && errno == EINTR) {}
	follow_hold(false);
	if (child > 0 && WIFEXITED(status) &&
	    (WEXITSTATUS(status) == PORT_MENU_QUIT || WEXITSTATUS(status) == PORT_MENU_POWER ||
	     WEXITSTATUS(status) == PORT_MENU_RESET || WEXITSTATUS(status) == PORT_MENU_RESTART))
		how = WEXITSTATUS(status);
	else if (child > 0 && !(WIFEXITED(status) && WEXITSTATUS(status) == PORT_MENU_CONTINUE))
		fprintf(stderr, "portkeys: the menu ended with status %d; the game goes on\n", status);

	/* The display back to the port, and its frame on the panel. The
	 * launcher's SDL puts back the frame it found as it closes its window;
	 * one that did not (a crash) left the panel dark or on its own picture */
	for (i = 0; i < 150 && ioctl(G.fd, DRM_IOCTL_SET_MASTER, 0) < 0; i++) usleep(2000);
	if (i == 150) {
		fprintf(stderr, "portkeys: cannot give the display back to the port (%s)\n", strerror(errno));
	} else if (c.fb_id && c.mode_valid && conn) {
		struct drm_mode_crtc now = { .crtc_id = G.crtc };

		if (ioctl(G.fd, DRM_IOCTL_MODE_GETCRTC, &now) < 0 || now.fb_id != c.fb_id || !now.mode_valid) {
			c.set_connectors_ptr = (uintptr_t)&conn;
			c.count_connectors = 1;
			if (ioctl(G.fd, DRM_IOCTL_MODE_SETCRTC, &c) < 0)
				fprintf(stderr, "portkeys: cannot put the port's frame back (%s)\n", strerror(errno));
		}
	}
	/* Nothing held as the game goes on: the A that chose Continue, the B or
	 * MENU that closed the menu. SDL asks the pad what is down once its
	 * queue is emptied, so a button still held would reach the game as a
	 * press, and gptokeyb would type its key */
	wait_released(1500);
	port_input_flush(pg);
	release(false);
	kill(-pg, SIGCONT);
	/* The launcher's volume and brightness keys, saved where these are, and
	 * the boosts it kept beside them, its gain written again (pkkeys.h);
	 * and On-Screen Stats, as the menu left it */
	pk_levels_load(db, BACKLIGHT, &L);
	pk_boost_load(db, &L, &BO);
	over_load();
	/* And the hotkeys, which the menu's Controls may have changed, with
	 * Auto Off's time (keys_load) */
	keys_load();
	fprintf(stderr, "portkeys: the menu was up %ld ms: %s\n", now_ms() - t0,
	        how == PORT_MENU_QUIT ? "Quit" : how == PORT_MENU_POWER ? "POWER" :
	        how == PORT_MENU_RESET ? "Reset" : how == PORT_MENU_RESTART ? "start again" : "Continue");
	/* Quit, or a start again (a Display mode over your PICO-8): launch.sh
	 * tells the two apart by the restart file the menu wrote (port_quit).
	 * The program going away is not borrowed from again (gone_pid) */
	if (how == PORT_MENU_RESTART) gone_pid = pid;
	if (how == PORT_MENU_QUIT || how == PORT_MENU_RESTART) {
		pid_t parent = getppid();

		if (parent > 1) kill(parent, SIGUSR2);
	} else if (how == PORT_MENU_POWER) {
		end_port();
	} else if (how == PORT_MENU_RESET) {
		/* PICO-8 going on, its queues emptied of the menu's presses: its
		 * own key for a restart of the cart (pkkbd.h) */
		if (pkkbd_ctrl_r(kbd, nap_ms)) fprintf(stderr, "portkeys: CTRL-R, the cart starts again\n");
		else fprintf(stderr, "portkeys: Reset, but no keyboard to send CTRL-R on\n");
	}
	return 1;
}

static int menu_show(void)
{
	return menu_show_as(NULL);
}

/* ---- your PICO-8's hotkeys -------------------------------------------------
 *
 * SELECT held and one more button, the player's own (src/hotkeys.h,
 * DESIGN.md 11.61), over your PICO-8 as over a Diatom game: the launcher
 * hands over the table in force for the cart (src/portmenu.h's
 * PORT_KEYS_FILE), the joypad's events go through Diatom's chord rule as
 * they are read (key_seen, tools/pkhot.c), and what they ask for is done
 * here after the pass (hot_do). PICO-8 sees neither SELECT nor the button
 * of a chord: it reads the pad portkeys gives it (below, vpad_*).
 *
 *   Screenshot     PICO-8's own 128 x 128 picture, as Diatom writes fake08's,
 *                  to Screenshots/PICO-8/<the cart> <time>.png, and the
 *                  pop-up with the shot in it (shot_take); a buzz and the
 *                  shutter as the frame is taken (shot_feel)
 *   Reset Game     asked first, as over a game: PICO-8 stopped and the
 *                  launcher's question over its frame (menu_show_as with
 *                  PORT_MENU_RESET_ARG); Reset is CTRL-R (pkkbd.h)
 *   Quit           as the menu's Quit: USR2 to launch.sh
 *   Next Shader,   the system's rows stepped (shader_next, display_next)
 *   Next Display
 *
 * Quick Save, Quick Load, Faster and Slower are no acts here (PKHOT_LIVE). */
static pkhot HOT;
static unsigned hot_pending;     /* the acts this pass's events made */
static char shots[1024];         /* PORT_KEYS_FILE's shots: the folder and the cart's name */

/* Auto Off over your PICO-8 (src/idle.h, the launcher's own clock): MENU >
 * Auto Off's time with no key pressed on any device, the charger holding it,
 * and the program ends and the handheld powers off as for POWER (end_port).
 * A game resumes where it was after Diatom's Auto Off; PICO-8 has no state
 * to come back to, so play since the cart's last save is lost, as with
 * POWER. idle_c.seconds is 0 over every other program (keys_load). */
static idle_clock idle_c;
static bool idle_input, idle_charging, idle_fired;
static void vpad_update(void);

static void keys_load(void)
{
	char line[1100], run[256] = "";
	FILE *f;

	pkhot_set(&HOT, NULL);
	idle_c.seconds = 0;
	shots[0] = '\0';
	if (!menu_tag || !port_hotkeys(menu_tag)) return;
	if ((f = fopen(PORT_KEYS_FILE, "r"))) {
		while (fgets(line, sizeof line, f)) {
			line[strcspn(line, "\n")] = '\0';
			if (!strncmp(line, "hotkeys\t", 8)) snprintf(run, sizeof run, "%.255s", line + 8);
			else if (!strncmp(line, "shots\t", 6)) snprintf(shots, sizeof shots, "%.1023s", line + 6);
		}
		fclose(f);
	}
	if (run[0] && !pkhot_set(&HOT, run))
		fprintf(stderr, "portkeys: hotkeys \"%s\" not read; none\n", run);
	/* The shutter of every voice, read now so a shot reads no card
	 * (shot_feel): res/sounds beside this program, as the launcher's */
	if (shots[0]) {
		char dir[600];
		ssize_t n = readlink("/proc/self/exe", dir, sizeof dir - 16);
		char *slash = n > 0 ? (dir[n] = '\0', strrchr(dir, '/')) : NULL;

		if (slash) snprintf(slash, sizeof dir - (size_t)(slash - dir), "/res/sounds");
		if (!slash || !pkshutter_load(dir))
			fprintf(stderr, "portkeys: no shutter for a screenshot (%s)\n", slash ? dir : "no path");
	}
	/* The launcher's MENU > Auto Off: two minutes until the player sets it
	 * (src/main.c's auto_off_load) */
	idle_c.seconds = pk_db_get(db, "autooff", 120);
	fprintf(stderr, "portkeys: hotkeys %s, Auto Off %d s\n", HOT.on ? run : "none", idle_c.seconds);
	/* The pad PICO-8 reads, for as long as a hotkey has a key (vpad_update) */
	vpad_update();
}

/* ---- the screenshot (tools/pkshot.h) ---- */

/* Every folder of `path` but its last part, made as needed */
static void make_dirs(const char *path)
{
	char d[1300];
	size_t i;

	snprintf(d, sizeof d, "%s", path);
	for (i = 1; d[i]; i++) {
		if (d[i] != '/') continue;
		d[i] = '\0';
		mkdir(d, 0755);
		d[i] = '/';
	}
}

/* PICO-8's 128 x 128 out of the frame on the panel (pkshot_pick), in the
 * rect the Display mode in force draws it in (src/p8draw.h, display.PICO8
 * read now: a start again can have changed it) */
static uint8_t g_shot[PKSHOT_BYTES];

static void shot_pick(const frame_map *m)
{
	char key[64], mode[32];
	const bool turned = m->h > m->w;
	dmode_rect r;

	snprintf(key, sizeof key, "display.%s", menu_tag);
	pkset_text(db, key, mode, sizeof mode);
	p8_draw_rect(mode[0] ? mode : NULL, (int)(turned ? m->h : m->w), (int)(turned ? m->w : m->h), P8_NEG_OK, &r);
	pkshot_pick(m->px, (int)m->w, (int)m->h, m->pitch, &r, g_shot);
}

/* The pop-up, "Screenshot saved" and the folder, with the shot laid in the
 * square the launcher left for it (PORT_SHOT_LIGHT, NOTICE_SLOT): nearest
 * pixels, and the badge's own rounded corners (src/notice.c's draw_badge)
 * cut over the slab, at the theme the bar would be in now */
static struct {
	uint32_t *px[DS_THEMES], *show;
	int w[DS_THEMES], h[DS_THEMES], slot[DS_THEMES][4];
} SN;

static void shot_notice_load(void)
{
	SN.px[DS_LIGHT] = dtov_read(PORT_SHOT_LIGHT, &SN.w[DS_LIGHT], &SN.h[DS_LIGHT], SN.slot[DS_LIGHT]);
	SN.px[DS_DARK] = dtov_read(PORT_SHOT_DARK, &SN.w[DS_DARK], &SN.h[DS_DARK], SN.slot[DS_DARK]);
}

static void shot_popup(void)
{
	const int t = pkset_bar_theme(appearance_read ? appearance_word : NULL, time(NULL), THEME_FILE);
	const int *sl = SN.slot[t], side = sl[2];
	const int w = SN.w[t], h = SN.h[t];

	if (!SN.px[t] || side <= 0 || sl[0] + side > w || sl[1] + side > h) {
		fprintf(stderr, "portkeys: no pop-up for the screenshot\n");
		return;
	}
	free(SN.show);
	if (!(SN.show = malloc((size_t)w * h * 4))) return;
	memcpy(SN.show, SN.px[t], (size_t)w * h * 4);
	pkshot_lay(SN.show, w, h, sl, g_shot);
	notice_show(SN.show, w, h, 1500);
}

/* Felt and heard as the frame is taken, never after the file (tools/pkfeel.h,
 * DESIGN.md 11.61), as a shot in a Diatom game is: the buzz (buzz) at MENU >
 * Sound > Rumble's share, and the shutter (tools/pkshutter.h) while MENU >
 * UI Sounds is a voice, on its own thread into the mix PICO-8 plays into,
 * beside it. Neither stops PICO-8 nor touches its sound, nor the frame
 * picked. The settings are read now: the in-game menu can change them. */
static void shot_feel(void)
{
	pk_feel f;

	pk_shot_feel(db, access("/proc/asound/card1/usbid", F_OK) == 0, jack_in() == 1, &f);
	buzz(f.buzz_pct, "screenshot");
	if (f.voice < 0)
		fprintf(stderr, "portkeys: no shutter (UI Sounds Off)\n");
	else if (!pkshutter_play(f.voice, f.device, f.left, f.right))
		fprintf(stderr, "portkeys: no shutter: %s\n", pkshutter_busy() ? "the last one still sounds" : "none was read");
}

static void shot_take(void)
{
	char when[32], path[1200], tmp[1210];
	struct drm_mode_crtc c;
	time_t now = time(NULL);
	struct tm tm;
	frame_map m;
	uint8_t *png;
	size_t len = 0;
	bool mine, ok;
	int fd;

	if (!shots[0]) {
		fprintf(stderr, "portkeys: screenshot: no folder handed over\n");
		return;
	}
	/* The display borrowed for the bar, or a master's copy for the read */
	mine = !held();
	if (mine && !master_copy_settled()) {
		fprintf(stderr, "portkeys: screenshot: no program holds the display\n");
		return;
	}
	memset(&c, 0, sizeof c);
	c.crtc_id = G.crtc;
	ok = ioctl(G.fd, DRM_IOCTL_MODE_GETCRTC, &c) == 0 && c.fb_id && frame_open(c.fb_id, &m);
	if (ok) {
		shot_pick(&m);
		frame_close(&m);
	}
	if (mine) release(false);
	if (!ok) return;
	shot_feel();
	localtime_r(&now, &tm);
	strftime(when, sizeof when, "%Y-%m-%d %H.%M.%S", &tm);
	snprintf(path, sizeof path, "%s %s.png", shots, when);
	snprintf(tmp, sizeof tmp, "%s.tmp", path);
	make_dirs(path);
	png = pkshot_png(g_shot, P8_SCREEN, P8_SCREEN, &len);
	fd = png ? open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644) : -1;
	ok = fd >= 0 && write_all(fd, png, len);
	if (fd >= 0 && close(fd)) ok = false;
	ok = ok && rename(tmp, path) == 0;
	if (!ok) unlink(tmp);
	free(png);
	fprintf(stderr, "portkeys: screenshot %s %s\n", ok ? "written" : "failed", path);
	if (ok) shot_popup();
}

/* Next Shader over your PICO-8: the shader its library draws (src/p8fx.h)
 * stepped on round the ones PICO-8 is offered, kept as the shelf's Shader
 * row keeps it (shader.PICO8) and in the file the library reads, so it
 * shows at once; and named over the game as Diatom's is ("Shader / CRT"),
 * the pop-up the launcher rendered for each as it handed over
 * (PORT_SHADER_POPUP), at the theme the bar would be in now */
static uint32_t *shader_px;

static void shader_next(void)
{
	char name[32], path[128];
	int t, w = 0, h = 0;

	if (!p8fx_next_shader(db, name, sizeof name)) {
		fprintf(stderr, "portkeys: Next Shader, but the settings cannot be written\n");
		return;
	}
	fprintf(stderr, "portkeys: hotkey, Next Shader: %s\n", name);
	t = pkset_bar_theme(appearance_read ? appearance_word : NULL, time(NULL), THEME_FILE);
	snprintf(path, sizeof path, PORT_SHADER_POPUP, name, t == DS_DARK ? "dark" : "light");
	free(shader_px);
	if ((shader_px = dtov_read(path, &w, &h, NULL))) notice_show(shader_px, w, h, 1500);
	else fprintf(stderr, "portkeys: no pop-up for %s\n", name);
}

/* Next Display over your PICO-8: a new mode means PICO-8 starts again, so
 * PICO-8 is stopped as for the menu and the launcher steps the Display row
 * and asks first, as the menu's Display row does (PORT_MENU_DISPLAY_ARG):
 * Start Again is PORT_MENU_RESTART, the program started again in the new
 * mode (launch.sh), Cancel the game on where it was. 1 when it was up. */
static int display_next(void)
{
	fprintf(stderr, "portkeys: hotkey, Next Display: asked first\n");
	return menu_show_as(PORT_MENU_DISPLAY_ARG);
}

/* What this pass's hotkeys asked for, in Diatom's order (diatom/src/main.c):
 * the screenshot, the next shader and display, then Reset Game and Quit. 1
 * when a menu was up, and the keys here are then to be read again. */
static int hot_do(unsigned acts)
{
	int menu = 0;

	if (acts & (1u << CHORD_SHOT)) {
		fprintf(stderr, "portkeys: hotkey, Screenshot\n");
		shot_take();
	}
	if (acts & (1u << CHORD_SHADER)) shader_next();
	if (acts & (1u << CHORD_DISPLAY)) menu |= display_next();
	if (acts & (1u << CHORD_QUIT)) {
		pid_t parent = getppid();

		fprintf(stderr, "portkeys: hotkey, Quit\n");
		if (parent > 1) kill(parent, SIGUSR2);
	} else if (acts & (1u << CHORD_RESET)) {
		fprintf(stderr, "portkeys: hotkey, Reset Game: asked first\n");
		menu |= menu_show_as(PORT_MENU_RESET_ARG);
	}
	return menu;
}

/* ---- the pad PICO-8 reads, while it has hotkeys (tools/pkvpad.h) ----------
 *
 * portkeys grabbed no pad: SELECT and a chord's button reached the cart as
 * they reached portkeys, so SELECT+A for a screenshot was A, New Game, too
 * (device test, rc.5). So while a hotkey has a key over your PICO-8,
 * portkeys holds the joypad (EVIOCGRAB: its events come here alone) and
 * gives PICO-8 a pad of its own, fed the joypad's buttons as Diatom's core
 * is fed them under chord.h's rule (pkhot_out), each as it is read: a chord
 * never reaches the cart, every other press does, in its order.
 *
 * Never a dead pad. The joypad is held only once PICO-8 has this pad open
 * (looked for among the files of the program's group) and nothing is down on
 * it, so no press is half PICO-8's; and let go of at once when PICO-8 closes
 * the pad, before the in-game menu (whose launcher reads the joypad), when
 * the hotkeys go, and as this ends. The kernel lets go of a grab and takes a
 * uinput pad away with the process, so a crash leaves PICO-8 the joypad as
 * before (it has it open all along, merged with this one into its player 1:
 * merge_joysticks 2, sd/pixel2/PICO-8.sh). While it holds the joypad,
 * zyhome, which reads it, hears nothing, so MENU+START held is this
 * process's then: zyhome's way out for a port (CTL_HOME_FLAG, USR2). An
 * injected press (px2-input) still comes here: a write to the joypad's node
 * reaches the one holding it. */
static int vpad_fd = -1, vpad_pad = -1;   /* the pad PICO-8 reads; the joypad, held */
static char vpad_node[64];
static bool vpad_held, vpad_home;
static uint32_t vpad_sent;                /* what the pad PICO-8 reads shows */
static long vpad_looked, vpad_since;      /* PICO-8's files last looked at; the pad made */
static int64_t vpad_grab_ms;              /* when the joypad was taken, on the devices' clock */
static pid_t vpad_reader;                 /* the process of PICO-8's that has it open */
static zychord ZC;
static int vpad_wait = -1;                /* vpad_poll's last answer, for the loop's poll */

static void vpad_forward(void)
{
	const uint32_t out = pkhot_out(&HOT);

	if (!vpad_held || out == vpad_sent) return;
	if (pkvpad_put(vpad_fd, vpad_sent, out)) vpad_sent = out;
}

static void vpad_let_go(void)
{
	if (!vpad_held) return;
	pkvpad_put(vpad_fd, vpad_sent, 0);
	vpad_sent = 0;
	if (vpad_pad >= 0) ioctl(vpad_pad, EVIOCGRAB, 0);
	vpad_held = false;
	vpad_pad = -1;
	vpad_looked = 0;             /* looked at again at the next pass */
	fprintf(stderr, "portkeys: the joypad let go of, PICO-8 reads it again\n");
}

static void vpad_update(void)
{
	const bool want = menu_tag && port_hotkeys(menu_tag) && HOT.on && HOT.live;

	if (want && vpad_fd < 0) {
		vpad_fd = pkvpad_open("/dev/uinput", vpad_node, sizeof vpad_node);
		vpad_since = now_ms();
		vpad_reader = 0;
	} else if (!want && vpad_fd >= 0) {
		vpad_let_go();
		pkvpad_close(vpad_fd);
		vpad_fd = -1;
		vpad_node[0] = '\0';
	}
}

/* Whether process `pid` has `node` open */
static bool pid_has_open(pid_t pid, const char *node)
{
	char dir[64];
	DIR *fds;
	struct dirent *fe;
	bool found = false;

	snprintf(dir, sizeof dir, "/proc/%d/fd", (int)pid);
	if (!(fds = opendir(dir))) return false;
	while (!found && (fe = readdir(fds))) {
		char link[352], target[64];
		ssize_t r;

		if (fe->d_name[0] == '.') continue;
		snprintf(link, sizeof link, "%s/%s", dir, fe->d_name);
		if ((r = readlink(link, target, sizeof target - 1)) <= 0) continue;
		target[r] = '\0';
		found = !strcmp(target, node);
	}
	closedir(fds);
	return found;
}

/* Whether PICO-8 reads the pad: the process of the program's group found
 * with it open before, while it still has it, else a look through the group */
static bool vpad_read(void)
{
	pid_t pg;
	DIR *proc;
	struct dirent *pe;

	if (!vpad_node[0]) return false;
	if (vpad_reader > 0 && pid_has_open(vpad_reader, vpad_node)) return true;
	vpad_reader = 0;
	if ((pg = port_group()) <= 1 || !(proc = opendir("/proc"))) return false;
	while (!vpad_reader && (pe = readdir(proc))) {
		const int pid = atoi(pe->d_name);

		if (pid > 1 && pgrp_of(pid) == pg && pid_has_open(pid, vpad_node)) vpad_reader = pid;
	}
	closedir(proc);
	return vpad_reader > 0;
}

/* Nothing of PICO-8's pad down on the joypad (MENU is none of it), as the
 * events read say: no EVIOCGKEY, which would take this process's queued
 * events of the joypad away with its answer */
static bool pad_all_up(void)
{
	return !(HOT.held & ~DIATOM_BIT(DIATOM_BTN_MENU));
}

/* MENU+START held while the joypad is held: zyhome's way out for a port */
static void pad_home(void)
{
	FILE *f = fopen(CTL_HOME_FLAG, "w");
	pid_t parent = getppid();

	if (f) fclose(f);
	fprintf(stderr, "portkeys: MENU+START held, back to the home\n");
	vpad_home = true;
	if (parent > 1) kill(parent, SIGUSR2);
}

/* A joypad event, after pkhot_key took it (key_seen), `ms` its time on the
 * devices' clock: forwarded, and MENU and START for MENU+START, while the
 * joypad is held. A press stamped before the joypad was taken may have
 * reached PICO-8 through it too, its let-go not: the joypad is let go of
 * again, PICO-8 has it whole, and it is taken at a later look. */
static void vpad_event(uint16_t code, int32_t value, int64_t ms)
{
	if (!vpad_held || value == 2) return;
	if (value == 1 && ms <= vpad_grab_ms && code != PK_BTN_MENU && pkhot_bit(code)) {
		fprintf(stderr, "portkeys: a press as the joypad was taken; let go of it again\n");
		vpad_let_go();
		return;
	}
	vpad_forward();
	if (code == PK_BTN_MENU || code == PKHOT_BTN_START)
		zychord_key(&ZC, code == PK_BTN_MENU, value != 0, now_ms());
}

/* After a resync of the joypad (resync_devices): the pad PICO-8 reads as
 * chord.h has the joypad now */
static void vpad_resync(void)
{
	vpad_forward();
}

/* The joypad gone (drop_device): its grab went with its file */
static void vpad_pad_gone(void)
{
	if (vpad_held) pkvpad_put(vpad_fd, vpad_sent, 0);
	vpad_sent = 0;
	vpad_held = false;
	vpad_pad = -1;
}

/* Once a pass: chord.h's turn between events (`chord_ms` on the devices'
 * clock), MENU+START's hold, and the joypad held or let go of as PICO-8
 * reads the pad or not. `pad` is the joypad's file, -1 for none; `clk` the
 * devices' clock. How long until it has more to do, -1 for nothing. */
static int vpad_poll(long now, int64_t chord_ms, int pad, clockid_t clk)
{
	int wait = -1, w, every;

	if (vpad_fd < 0) return -1;
	if (vpad_held) {
		pkhot_tick(&HOT, (uint64_t)chord_ms);
		vpad_forward();
		if (zychord_due(&ZC, now)) pad_home();
		if ((w = pkhot_wait(&HOT, (uint64_t)chord_ms)) >= 0) wait = w;
		if ((w = zychord_wait(&ZC, now)) >= 0 && (wait < 0 || w < wait)) wait = w;
	}
	/* Held: PICO-8 still reads the pad, every 2 s. Not yet: five looks a
	 * second for its first 10 s, as PICO-8 starts, then every 2 s */
	every = vpad_held || now - vpad_since > 10000 ? 2000 : 200;
	if (now - vpad_looked >= every) {
		const bool reads = vpad_read();

		vpad_looked = now;
		if (!vpad_held && reads && pad >= 0 && pad_all_up() && ioctl(pad, EVIOCGRAB, 1) == 0) {
			struct timespec t;

			clock_gettime(clk, &t);
			vpad_grab_ms = (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
			vpad_held = true;
			vpad_pad = pad;
			vpad_sent = 0;
			pkhot_resync(&HOT, 0);
			zychord_init(&ZC);
			fprintf(stderr, "portkeys: the joypad held; PICO-8 (pid %d) reads its pad through portkeys\n",
			        (int)vpad_reader);
		} else if (vpad_held && !reads) {
			fprintf(stderr, "portkeys: PICO-8 closed its pad\n");
			vpad_let_go();
		}
	}
	w = (int)(vpad_looked + every - now);
	if (w < 0) w = 0;
	if (wait < 0 || w < wait) wait = w;
	return wait;
}

/* Devices: the poll set's first n entries, and for each whether it has MENU
 * or the volume keys, whether the kernel's queue for it overflowed
 * (SYN_DROPPED) and what it sent since is still to be skipped, and whether
 * what is down on it must be queried after discarding this pass. */
static struct pollfd fds[10];
static bool has_menu[8], has_vol[8], dropped[8], resync[8];
static int n;
static pkkeys K;
/* A removed input invalidates the batch and its held state. */
static bool lost_menu, lost_vol;

/* Until no key is down on the joypad or the volume keys' device, as the
 * kernel says (EVIOCGKEY), `ms` at most. Marked unused for
 * tools/portkeys-flow-check.py, which builds these devices' code with a
 * menu_show of its own that never calls it. */
__attribute__((unused)) static void wait_released(int ms)
{
	long t0 = now_ms();

	for (;;) {
		bool held = false;
		int i;

		for (i = 0; i < n && !held; i++) {
			unsigned long down[NLONGS(KEY_MAX + 1)] = { 0 };
			size_t k;

			if (!(has_menu[i] || has_vol[i]) ||
			    ioctl(fds[i].fd, EVIOCGKEY(sizeof down), down) < 0)
				continue;
			for (k = 0; k < NLONGS(KEY_MAX + 1); k++)
				if (down[k]) held = true;
		}
		if (!held || now_ms() - t0 >= ms) {
			if (now_ms() - t0 > 20)
				fprintf(stderr, "portkeys: %s the buttons to be let go, %ld ms\n",
				        held ? "gave up waiting for" : "waited for", now_ms() - t0);
			return;
		}
		{
			struct timespec nap = { 0, 10 * 1000000L };

			nanosleep(&nap, NULL);
		}
	}
}

/* POWER over a port. ZyperOS reads the power key itself, and launch.sh stops
 * it for a port, so a press during Stardew Valley or OpenBOR did nothing. Its
 * device (the RK817's "rk805 pwrkey" on the Pixel 2) is read here on a file
 * of its own, apart from the keys above and their ordering: a press is the
 * power button's, whatever else is held, as it is in the launcher. The press
 * goes to launch.sh, which started this and is its parent, as SIGUSR1; it
 * ends the port, TERM and then KILL, and powers off as the launcher does
 * after its own power press. Never to init: a parent gone leaves this a child
 * of PID 1, where USR1 halts BusyBox's init. */
static int powfd = -1;

/* The port ends, and the handheld powers off: SIGUSR1 to launch.sh */
static void end_port(void)
{
	pid_t parent = getppid();

	if (parent > 1) kill(parent, SIGUSR1);
}

/* The battery run flat over a port, by the launcher's rule (battery_empty,
 * src/zyperdraw.c, and src/battempty.h): off the charger, BATT_EMPTY_PCT or
 * under on two readings in a row 5 s apart, or the voltage under the Pixel
 * 2's empty voltage (PX2_BATT_EMPTY_MV, src/device/pixel2_levels.h) on three
 * whatever the level says, so one stray reading never powers off. It ends
 * the port and powers off as POWER does, while the card can still be let go
 * of, where the PMIC's own cut would leave it mounted. Auto Off runs over
 * your PICO-8 alone (idle_tick): the launcher takes it over a game because
 * the game resumes where it was after the power-off (AUTO_OFF, src/main.c),
 * and a port has no resume, nor an install or a cutscene a player gone. */
#define BATT_EVERY_MS 5000
static long batt_next;
static batt_empty batt_run;
static batt_verdict batt_said;

/* One reading: the verdict on the reading that makes the battery empty, and
 * only then; BATT_FINE on every other */
static batt_verdict batt_flat(bool ok, int pct, bool charging, int mv)
{
	batt_verdict was = batt_said;

	batt_said = batt_empty_note(&batt_run, ok, pct, charging, mv, PX2_BATT_EMPTY_MV);
	return was == BATT_FINE ? batt_said : BATT_FINE;
}

/* Auto Off's look, once a pass of the loop (idle_c, over your PICO-8):
 * whether a key came since the last (key_seen), the charger as the battery
 * was last read. The clock starts again after a gap of more than
 * IDLE_GAP_MS between two looks, as the launcher's does after a time nobody
 * read the pad: the loop wakes at least that often while it counts, so a
 * gap is the in-game menu, whose launcher has the keys meanwhile. Once,
 * then launch.sh ends this process. */
static void idle_tick(long now)
{
	if (idle_check(&idle_c, (unsigned)now, idle_input, idle_charging) && !idle_fired) {
		idle_fired = true;
		fprintf(stderr, "portkeys: no key for %d s (Auto Off); the port ends\n", idle_c.seconds);
		end_port();
	}
	idle_input = false;
}

/* A press, or with MENU > Power Button on Hold a hold of POWERHOLD_MS, as
 * the launcher reads it (src/powerhold.h): PW is the key's state, and the
 * bar shows the hold's power kind for as long as it is under way, full when
 * it is done, gone at once when it is let go early (pw_bar). */
static powerhold PW;
static bool pw_shown;

static void pw_bar(long now)
{
	int pm = powerhold_permille(&PW, (unsigned)now);

	if (pm >= 0) {
		bar_show(OSDBAR_POWER, pm * OSDBAR_POWER_STEPS / 1000, OSDBAR_POWER_STEPS);
		pw_shown = true;
	} else if (pw_shown) {
		pw_shown = false;
		if (PW.fired) bar_show(OSDBAR_POWER, OSDBAR_POWER_STEPS, OSDBAR_POWER_STEPS);
		else bar_fade();
	}
}

static void power_read(void)
{
	struct input_event ev[16];
	ssize_t r;

	while ((r = read(powfd, ev, sizeof ev)) > 0) {
		size_t k;

		for (k = 0; k < (size_t)r / sizeof *ev; k++) {
			/* A repeat is neither a press nor a let-go */
			if (ev[k].type != EV_KEY || ev[k].code != KEY_POWER || ev[k].value == 2)
				continue;
			if (!powerhold_key(&PW, ev[k].value == 1, (unsigned)now_ms())) continue;
			fprintf(stderr, "portkeys: POWER, the port ends\n");
			end_port();
		}
	}
	if (r == 0 || (r < 0 && errno != EAGAIN && errno != EINTR)) {
		close(powfd);
		powfd = -1;
		PW = (powerhold){ 0 };
	}
	pw_bar(now_ms());
}

/* Every pass: a hold of POWER done ends the port as a press does */
static void power_due(void)
{
	long now = now_ms();

	if (powerhold_due(&PW, (unsigned)now)) {
		fprintf(stderr, "portkeys: POWER held, the port ends\n");
		end_port();
	}
	pw_bar(now);
}

/* After the in-game menu (menu_show), which read POWER itself: what the power
 * key's file queued meanwhile was the launcher's, and is let go of unread */
static void power_drain(void)
{
	struct input_event ev[16];
	ssize_t r = 1;

	while (powfd >= 0 && (r = read(powfd, ev, sizeof ev)) > 0) {}
	if (powfd >= 0 && (r == 0 || (r < 0 && errno != EAGAIN && errno != EINTR))) {
		close(powfd);
		powfd = -1;
	}
	/* And a hold under way when the menu came up was the launcher's too */
	PW = (powerhold){ 0 };
	pw_bar(now_ms());
}

/* The clock the devices stamp their events with: CLOCK_MONOTONIC, set on
 * each at open (EVIOCSCLOCKID), so a change of the wall clock - the RTC read
 * late, a time set over SSH - cannot reorder two events of one pass. If a
 * device will not take it, all of them go by the wall clock, where the
 * kernel starts them: the same clock for every device, which is what the
 * order needs, and pending events mature on the next pass unless an earlier new
 * event defers their suffix. Queue overflow discards that entire batch. */
static clockid_t ev_clock = CLOCK_MONOTONIC;

static bool ev_clock_us(int64_t *us)
{
	struct timespec t;

	if (clock_gettime(ev_clock, &t) < 0) {
		fprintf(stderr, "portkeys: cannot read the event clock: %s\n", strerror(errno));
		return false;
	}
	*us = (int64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000;
	return true;
}

/* Fail closed if even one reset fails: that fd might still use monotonic
 * timestamps while another uses realtime. No pass may mix those clocks. */
static bool event_clock_fallback(int clk_err)
{
	int clk = CLOCK_REALTIME;

	if (!clk_err) return true;
	fprintf(stderr, "portkeys: a device has no monotonic clock (%s); trying the wall clock\n",
	        strerror(clk_err));
	for (int i = 0; i < n; i++) {
		if (ioctl(fds[i].fd, EVIOCSCLOCKID, &clk) < 0) {
			fprintf(stderr, "portkeys: cannot set a shared event clock: %s\n", strerror(errno));
			return false;
		}
	}
	ev_clock = CLOCK_REALTIME;
	return true;
}

/* A device that is gone - unplugged, or its driver unbound - reports
 * POLLHUP and POLLERR for ever and a read of it fails: polled on, it would
 * spin a core. Its place goes to the last one. */
static void drop_device(int i)
{
	fprintf(stderr, "portkeys: an input device went away\n");
	if (fds[i].fd == jack) jack = -1;
	/* Discard its pending events with this pass; it cannot release its keys. */
	lost_menu |= has_menu[i];
	lost_vol |= has_vol[i];
	if (has_menu[i]) {
		pkhot_resync(&HOT, 0);
		vpad_pad_gone();
	}
	close(fds[i].fd);
	n--;
	fds[i] = fds[n];
	has_menu[i] = has_menu[n];
	has_vol[i] = has_vol[n];
	dropped[i] = dropped[n];
	resync[i] = resync[n];
}

/* Hands what was read to pkkeys, and does the steps it gives */
static bool keys_pass(const pk_key *keys, int nk, int64_t cutoff)
{
	pk_step s[PK_WAITING + PK_PASS_MAX];
	int i, m = pkkeys_pass(&K, keys, nk, cutoff, now_ms(), s, (int)(sizeof s / sizeof s[0]));

	if (m < 0) return false;
	for (i = 0; i < m; i++) press(&s[i]);
	return true;
}

/* Every key event of every device, before pkkeys picks its own: somebody is
 * there (Auto Off's clock, idle_tick), and on the joypad, SELECT's hotkeys
 * over your PICO-8, judged in the order the kernel queued them (pkhot.h) and
 * done after the pass (hot_do). The joypad's state is followed with no
 * hotkeys too, so a table given after a menu starts from the pad as it is. */
static void key_seen(int i, const struct input_event *e)
{
	idle_input = true;
	if (!has_menu[i]) return;
	hot_pending |= pkhot_key(&HOT, e->code, e->value,
	                         (uint64_t)((int64_t)e->input_event_sec * 1000 + e->input_event_usec / 1000));
	/* ...and, while portkeys holds the joypad, to the pad PICO-8 reads at
	 * once (vpad_event); START under MENU there is MENU+START's, so MENU let
	 * go after it opens no menu */
	vpad_event(e->code, e->value, (int64_t)e->input_event_sec * 1000 + e->input_event_usec / 1000);
	if (vpad_held && e->code == PKHOT_BTN_START && e->value == 1 && K.menu_held) pkkeys_menu_state(&K, true);
}

/* The joypad's file (the device with MENU), -1 when it is gone */
static int joypad_fd(void)
{
	for (int i = 0; i < n; i++)
		if (has_menu[i]) return fds[i].fd;
	return -1;
}

/* The joypad's buttons down, as pkhot counts them, from EVIOCGKEY's bits */
static uint32_t pad_held(const unsigned long *down)
{
	static const uint16_t codes[] = {
		PKHOT_BTN_SOUTH, PKHOT_BTN_EAST, PKHOT_BTN_NORTH, PKHOT_BTN_WEST, PKHOT_BTN_TL, PKHOT_BTN_TR,
		PKHOT_BTN_TL2, PKHOT_BTN_TR2, PKHOT_BTN_SELECT, PKHOT_BTN_START, PKHOT_BTN_UP,
		PKHOT_BTN_UP + 1, PKHOT_BTN_UP + 2, PKHOT_BTN_UP + 3, PK_BTN_MENU,
	};
	uint32_t held = 0;
	size_t k;

	for (k = 0; k < sizeof codes / sizeof codes[0]; k++)
		if (HAS(down, codes[k])) held |= pkhot_bit(codes[k]);
	return held;
}

/* Bound raw reads too: irrelevant events and EINTR must not keep this loop
 * inside one noisy device forever. Reaching the budget discards the batch,
 * even if that last read happened to empty the queue. */
#define PK_READ_CALLS 16

/* Read until EAGAIN, within the budget. Never apply steps here: all devices
 * must contribute before any step. False means EOF, malformed data or a
 * device error. `unreliable` requires discarding/resyncing the whole pass. */
static bool read_device(int i, pk_key *keys, int *nk, bool *unreliable, bool *plugged)
{
	struct input_event ev[64];
	ssize_t r;
	size_t k;

	for (int attempt = 0; attempt < PK_READ_CALLS; attempt++) {
		if ((r = read(fds[i].fd, ev, sizeof ev)) < 0) {
			if (errno == EINTR) continue;
			/* EAGAIN: all of it read. Anything else is a device gone, with
			 * events still queued perhaps (evdev_read says ENODEV) */
			return errno == EAGAIN;
		}
		if (!r || (size_t)r % sizeof ev[0]) return false;
		for (k = 0; k < (size_t)r / sizeof ev[0]; k++) {
			/* Lost events invalidate the whole pass. Skip the fragment
			 * through SYN_REPORT, then query state in resync_devices. */
			if (ev[k].type == EV_SYN && ev[k].code == SYN_DROPPED) {
				*unreliable = true;
				dropped[i] = true;
				continue;
			}
			if (dropped[i]) {
				if (ev[k].type == EV_SYN && ev[k].code == SYN_REPORT) {
					dropped[i] = false;
					resync[i] = true;
				}
				continue;
			}
			/* A plug in or out: the other output's window, as the
			 * launcher does (plat_audio_jack_poll) */
			if (ev[k].type == EV_SW && ev[k].code == SW_HEADPHONE_INSERT) {
				*plugged = true;
				continue;
			}
			if (ev[k].type == EV_KEY) key_seen(i, &ev[k]);
			if (ev[k].type != EV_KEY || !pkkeys_relevant(ev[k].code, ev[k].value)) continue;
			if (*nk == PK_PASS_MAX) {
				*unreliable = true;
				continue;
			}
			keys[(*nk)++] = (pk_key){ (int64_t)ev[k].input_event_sec * 1000000 + ev[k].input_event_usec,
			                          ev[k].code, ev[k].value };
		}
	}
	*unreliable = true;
	return true;
}

/* Lost ordering cancels every hold and pending event. Query every input,
 * since EVIOCGKEY also removes its queued key events. Recovered MENU may
 * select brightness for a later fresh press; a recovered volume hold never
 * synthesizes a press or repeat. A failed query removes that input. */
static void resync_devices(bool *plugged)
{
	pkkeys_init(&K);
	for (int i = 0; i < n; i++) {
		unsigned long down[NLONGS(KEY_MAX + 1)] = { 0 };

		if (dropped[i]) continue;  /* wait for SYN_REPORT before querying */
		resync[i] = false;
		if (has_menu[i] || has_vol[i]) {
			if (ioctl(fds[i].fd, EVIOCGKEY(sizeof down), down) < 0) {
				drop_device(i--);
				continue;
			}
			if (has_menu[i]) {
				pkkeys_menu_state(&K, K.menu_held || HAS(down, BTN_TRIGGER_HAPPY1));
				/* and SELECT's rule from what is down now (pkhot_resync) */
				pkhot_resync(&HOT, pad_held(down));
				vpad_resync();
			}
		}
		if (fds[i].fd == jack) *plugged = true;
	}
	lost_menu = lost_vol = false;
}

int main(int argc, char **argv)
{
	int i, clk_err = 0;
	DIR *d = opendir("/dev/input");
	struct dirent *e;

	/* --follow PID first, from launch.sh for a program MP4 plays on over
	 * (follow_hold) */
	if (argc > 2 && !strcmp(argv[1], "--follow")) {
		follow_pid = (pid_t)atoi(argv[2]);
		argv += 2;
		argc -= 2;
	}
	/* --menu ELF TAG GAME next, from launch.sh for a port with the in-game
	 * menu (OpenBOR, a port, PortMaster): the launcher to show it, the system's tag and the
	 * game, for menu_show */
	if (argc > 4 && !strcmp(argv[1], "--menu")) {
		menu_elf = argv[2];
		menu_tag = argv[3];
		menu_game = argv[4];
		argv += 4;
		argc -= 4;
	}
	/* Over your PICO-8, its keyboard for CTRL-R first of all, so PICO-8's
	 * SDL finds it as it starts (SDL watches for one made later too) */
	if (menu_tag && port_resets(menu_tag)) kbd = pkkbd_open("/dev/uinput");
	osdbar_size(PANEL_S, &bw, &bh);
	pkkeys_init(&K);
	pkhot_init(&HOT);
	mixer = open("/dev/snd/controlC0", O_RDWR | O_CLOEXEC);
	/* The launcher's device database, from launch.sh: $USERDATA_PATH/zyperos.db */
	if (argc > 1 && sqlite3_open_v2(argv[1], &db, SQLITE_OPEN_READWRITE, NULL) != SQLITE_OK) {
		fprintf(stderr, "portkeys: no settings at %s\n", argv[1]);
		sqlite3_close(db);
		db = NULL;
	}
	if (db) {
		sqlite3_busy_timeout(db, 200);
		/* As the launcher has it (src/db.c), for the same reason: a write
		 * on every step, a held key's repeats too, on the key path, and
		 * FULL costs 5x per write to keep the last nudge through a power
		 * cut. The journal mode is the file's and is WAL already; this one
		 * is the connection's, so it is said here too. */
		sqlite3_exec(db, "PRAGMA synchronous=NORMAL;", NULL, NULL, NULL);
	}
	/* As the launcher starts: the saved levels, or for the backlight the
	 * rung nearest what the panel is at. A volume never saved stays
	 * untouched until the first press, which starts from the middle. */
	pk_levels_load(db, BACKLIGHT, &L);
	/* And the boosts past the top kept beside them (pkkeys.h): the volume's
	 * gain written to the file the port's plugin reads, so the port starts
	 * with it; the backlight's lift below, with Night Mode's warmth */
	pk_boost_init(&BO, NULL);
	pk_looks_init(&LOOKS);
	if (!pk_boost_load(db, &L, &BO))
		fprintf(stderr, "portkeys: no file at %s for the volume boost's gain\n", BO.share_path);
	if (pk_boost_cells(&BO, PK_VOLUME) || pk_boost_cells(&BO, PK_BRIGHT))
		fprintf(stderr, "portkeys: kept from before, volume boost +%d dB, brightness boost %d\n",
		        pk_boost_gain(&BO), pk_boost_lift(&BO));
	/* Developer SSH for the port's length (pkssh.h), and TERM to end it */
	pkssh_init(db, argc > 1 ? argv[1] : NULL);
	{
		struct sigaction sa;

		memset(&sa, 0, sizeof sa);
		sa.sa_handler = on_term;
		sigaction(SIGTERM, &sa, NULL);
	}
	/* The bar's theme (bar_show): MENU > Appearance, from the same device
	 * database the launcher reads it from, and the zone from the card's
	 * library, from launch.sh: $SHARED_USERDATA_PATH/.zyperos/library.db */
	appearance_read = pkset_text(db, "appearance", appearance_word, sizeof appearance_word);
	/* And the player's look and Auto's hours, which nothing changes while a
	 * port runs either: the bar's colors in both themes, and the hours the
	 * clock turns it at (src/look.h, src/theme.h) */
	if (appearance_read) look_load_db(db);
	/* MENU > Power Button, a press or a hold, as the launcher saved it
	 * (src/powerhold.h); nothing changes it while a port runs */
	{
		char pb[8];

		if (pkset_text(db, POWERHOLD_KEY, pb, sizeof pb)) powerhold_set_mode(!strcmp(pb, "hold"));
	}
	pkset_zone_load(argc > 2 ? argv[2] : NULL);
	/* Night Mode: the launcher left the table right as the port started */
	{
		panelgamma_color c;

		panelgamma_screen_read(PANELGAMMA_COLOR_FILE, &c, &night);
		night_on = panelgamma_night_at(&night, (long long)time(NULL));
		night_looked = time(NULL);
	}
	lift_start();
	/* The heat notice the launcher left, in both themes (heat_check) */
	heat_init(&H);
	notice_load(DS_LIGHT, PORT_HEAT_LIGHT);
	notice_load(DS_DARK, PORT_HEAT_DARK);
	/* The low-battery disc and On-Screen Stats over your PICO-8 (over_tick) */
	O.allowed = menu_tag && port_menu_overlays(menu_tag);
	pkover_fps_init(&O.fps);
	over_load();
	/* Over your PICO-8: the hotkeys and Auto Off's time, and the
	 * screenshot's pop-up the launcher left (keys_load, shot_take) */
	keys_load();
	if (menu_tag && port_hotkeys(menu_tag)) shot_notice_load();

	/* Every event device with a volume key or MENU (BTN_TRIGGER_HAPPY1, on
	 * the joypad), and the headphone switch: found by what they report
	 * rather than by number, which follows probe order. The order found in
	 * no longer matters: the events are handled in the order of their times. */
	while (d && (e = readdir(d)) && n < 7) {
		char path[300];
		unsigned long keys[NLONGS(KEY_MAX)] = { 0 }, sw[NLONGS(SW_MAX + 1)] = { 0 };
		int fd, clk = CLOCK_MONOTONIC;

		if (strncmp(e->d_name, "event", 5)) continue;
		snprintf(path, sizeof path, "/dev/input/%s", e->d_name);
		/* Not blocking, so a pass reads each device to the end of its queue */
		if ((fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC)) < 0) continue;
		/* First, while nothing is queued yet: a change of clock empties a
		 * queue that has anything in it and says SYN_DROPPED, which is
		 * resynced like any other */
		if (ioctl(fd, EVIOCSCLOCKID, &clk) < 0 && !clk_err) clk_err = errno ? errno : EINVAL;
		ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys);
		ioctl(fd, EVIOCGBIT(EV_SW, sizeof sw), sw);
		/* The motor for the boost's buzz (boost_buzz): the first device
		 * with force feedback that rumbles, the joypad */
		if (!buzz_node[0]) {
			unsigned long ff[NLONGS(FF_MAX + 1)] = { 0 };

			if (ioctl(fd, EVIOCGBIT(EV_FF, sizeof ff), ff) >= 0 && HAS(ff, FF_RUMBLE))
				snprintf(buzz_node, sizeof buzz_node, "%s", path);
		}
		/* The power key, on a file of its own (power_read) */
		if (powfd < 0 && HAS(keys, KEY_POWER))
			powfd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (HAS(keys, KEY_VOLUMEUP) || HAS(keys, BTN_TRIGGER_HAPPY1) ||
		    (jack < 0 && HAS(sw, SW_HEADPHONE_INSERT))) {
			if (jack < 0 && HAS(sw, SW_HEADPHONE_INSERT)) jack = fd;
			has_menu[n] = HAS(keys, BTN_TRIGGER_HAPPY1);
			has_vol[n] = HAS(keys, KEY_VOLUMEUP) || HAS(keys, KEY_VOLUMEDOWN);
			fds[n].fd = fd;
			fds[n++].events = POLLIN;
		} else {
			close(fd);
		}
	}
	if (d) closedir(d);
	if (!n) return 1;
	if (!event_clock_fallback(clk_err)) return 1;
	/* The plug as the port starts: a plug in or out from here on is a
	 * change of output (plug_changed) */
	jack_was = jack_in();

	for (;;) {
		static pk_key keys[PK_PASS_MAX];
		pk_step s[2];
		int wait = bar_tick(), rep = pkkeys_wait(&K, now_ms()), m, nk = 0;
		int pi = -1, pw = -1, vi = -1;
		bool plugged = false, unreliable = false;
		int64_t cutoff;

		if (g_term) {
			pkssh_stop();
			return 0;
		}
		if (!n && G.fd < 0) {
			fprintf(stderr, "portkeys: no keys left to read\n");
			pkssh_stop();
			return 0;
		}
		/* Developer SSH's next look (pkssh.h) */
		{
			int sw = pkssh_tick(now_ms());

			if (sw >= 0 && (wait < 0 || sw < wait)) wait = sw;
		}
		/* A held key's next repeat, or at once when events wait for this
		 * pass (pkkeys_wait) */
		if (rep >= 0 && (wait < 0 || rep < wait)) wait = rep;
		/* POWER held in Hold mode: the bar's next segment, or the hold's
		 * end, which no event marks */
		{
			int hw = powerhold_next_ms(&PW, (unsigned)now_ms(), OSDBAR_POWER_STEPS);

			if (hw >= 0 && (wait < 0 || hw < wait)) wait = hw;
		}
		/* While the port's file is borrowed, look at it at least five times a
		 * second. held() (kcmp, and whether this is still the master) is asked
		 * in bar_tick, which runs when this returns, and the wait above is up
		 * to the end of the 1.5 s the bar shows when nothing is pressed: a port
		 * that gave the master up or closed the display meanwhile would be
		 * found at the end of it and not before, all that time with its file
		 * and its master kept alive by this copy (see the top of this file). */
		if (G.fd >= 0 && (wait < 0 || wait > 200)) wait = 200;
		/* Night Mode's next start or end */
		{
			int nw = night_wait();

			if (nw >= 0 && (wait < 0 || nw < wait)) wait = nw;
		}
		/* The battery, every BATT_EVERY_MS (batt_flat) */
		{
			long now = now_ms();
			int pct = -1, mv = 0;
			bool charging = false, ok;
			batt_verdict v;

			if (now - batt_next >= 0) {
				ok = batt_now(&pct, &charging, &mv);
				/* Auto Off's clock held on the charger (idle_tick) */
				if (ok) idle_charging = charging;
				/* The low-battery disc's level (lowbat_reading) */
				if (ok) lowbat_reading(pct, charging);
				v = batt_flat(ok, pct, charging, mv);
				if (v == BATT_EMPTY_VOLTAGE)
					fprintf(stderr, "portkeys: battery %d mV, under %d mV on %d readings in a row, at %d%%; empty, the port ends\n",
					        mv, PX2_BATT_EMPTY_MV, BATT_EMPTY_MV_READINGS, pct);
				else if (v == BATT_EMPTY_LEVEL)
					fprintf(stderr, "portkeys: battery %d%%, empty; the port ends\n", pct);
				if (v != BATT_FINE) end_port();
				batt_next = now + BATT_EVERY_MS;
			}
			if (wait < 0 || wait > batt_next - now) wait = (int)(batt_next - now);
		}
		/* Auto Off counting: a look well inside its gap (idle_tick) */
		if (idle_c.seconds > 0 && (wait < 0 || wait > (int)IDLE_GAP_MS - 100)) wait = (int)IDLE_GAP_MS - 100;
		/* The pad PICO-8 reads: chord.h's next turn, or the next look at it */
		if (vpad_wait >= 0 && (wait < 0 || vpad_wait < wait)) wait = vpad_wait;
		m = n;
		/* The port's end, while its display is borrowed */
		if (G.pidfd >= 0) {
			pi = m;
			fds[m].fd = G.pidfd;
			fds[m++].events = POLLIN;
		}
		/* And the power key */
		if (powfd >= 0) {
			pw = m;
			fds[m].fd = powfd;
			fds[m++].events = POLLIN;
		}
		/* And the next vertical blank, while On-Screen Stats counts the
		 * frames (over_vblank) */
		if (over_poll_fd() >= 0) {
			vi = m;
			fds[m].fd = over_poll_fd();
			fds[m++].events = POLLIN;
		}
		if (poll(fds, (nfds_t)m, wait) < 0) continue;
		if (pi >= 0 && (fds[pi].revents & (POLLIN | POLLHUP | POLLERR))) release(true);
		if (pw >= 0 && fds[pw].revents) power_read();
		if (vi >= 0 && fds[vi].revents) over_vblank();
		power_due();
		night_tick();

		/* poll is only a wakeup hint. A key may arrive on an fd after poll
		 * checked it but before another fd woke us. Read ALL open inputs,
		 * then handle a single ordered batch, within the stated bounds. */
		if (!ev_clock_us(&cutoff)) return 1;
		for (i = 0; i < n; i++) {
			if ((fds[i].revents & POLLNVAL) ||
			    !read_device(i, keys, &nk, &unreliable, &plugged) ||
			    (fds[i].revents & (POLLHUP | POLLERR))) {
				unreliable = true;
				drop_device(i--);
			}
		}
		for (i = 0; i < n; i++) unreliable |= dropped[i] || resync[i];
		unreliable |= lost_menu || lost_vol;
		if (unreliable || !keys_pass(keys, nk, cutoff)) resync_devices(&plugged);

		/* MENU on its own: the in-game menu, over a port that has one
		 * (menu_show). What was pressed while it was up was the launcher's,
		 * POWER and the volume keys too, so this side's keys start again
		 * from what is down now. */
		if (pkkeys_menu_tapped(&K) && !vpad_home && menu_show()) {
			power_drain();
			resync_devices(&plugged);
		}
		/* SELECT's hotkeys over your PICO-8 (hot_do): Reset Game's question
		 * is a menu too, after which the keys start again in the same way */
		if (hot_pending) {
			const unsigned acts = hot_pending;

			hot_pending = 0;
			if (hot_do(acts)) {
				power_drain();
				resync_devices(&plugged);
			}
		}
		idle_tick(now_ms());
		/* The pad PICO-8 reads: chord.h between events, MENU+START's hold,
		 * the joypad held or let go of (vpad_poll); its next look bounds
		 * the next poll's wait */
		vpad_wait = vpad_poll(now_ms(), cutoff / 1000, joypad_fd(), ev_clock);

		if (plugged) plug_changed();

		/* A held key's repeats, on the launcher's clock */
		m = pkkeys_tick(&K, now_ms(), s, 2);
		for (i = 0; i < m; i++) press(&s[i]);
		/* And the boost's, after this pass's steps (boost_clock) */
		boost_clock(&K);
	}
}
