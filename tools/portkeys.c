/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* portkeys: the GKD Pixel 2's volume keys while a port runs, and the bar that
 * says what they did.
 *
 * ZyperOS and Diatom read the volume keys themselves, and launch.sh stops both
 * for a port (or a standalone emulator), so for as long as Stardew Valley or
 * DraStic ran, VOL+ and VOL- did nothing. launch.sh starts this beside the
 * port and stops it after.
 *
 *   VOL+ / VOL-          the volume, a position at a time
 *   MENU held + VOL      the backlight instead, a rung at a time
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
 * the bar turns at 7:00 and 19:00 over a port as the shelf would (pkset.c,
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
 * No exclusive grab: the port and gptokeyb still see every key. */
#include <dirent.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/kcmp.h>
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
#include <time.h>
#include <unistd.h>

#include "device/pixel2_levels.h"
#include "osdbar.h"
#include "pkkeys.h"
#include "pkset.h"
#include "sidelights.h"
#include "status.h"
#include "themefile.h"

#ifndef SYS_pidfd_open
#define SYS_pidfd_open 434
#endif
#ifndef SYS_pidfd_getfd
#define SYS_pidfd_getfd 438
#endif
#ifndef SYS_kcmp
#define SYS_kcmp 272                 /* asm-generic's, which is aarch64's */
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
static sqlite3 *db;
/* MENU > Appearance as the launcher saved it, "" for never; read once, since
 * nothing can change it while a port runs. appearance_read is whether the
 * database answered at all: if not, the bar goes by THEME_FILE. */
static char appearance_word[16];
static bool appearance_read;

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
	int drawn_kind, drawn_level, drawn_theme;
} B;

static int bw, bh;               /* the bar as the player sees it: 298 x 58 */

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
static bool buffers(void)
{
	int i;

	G.cw = (bh + 15) / 16 * 16;
	for (i = 0; i < 2; i++) {
		struct drm_mode_create_dumb c = { .width = (uint32_t)G.cw, .height = (uint32_t)bw, .bpp = 32 };
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

/* Every process but this one with the display open, until one of the copies
 * is the master's. */
static bool borrow(void)
{
	DIR *proc = opendir("/proc");
	struct dirent *pe;
	int self = getpid();

	while (proc && (pe = readdir(proc))) {
		int pid = atoi(pe->d_name), pidfd;
		char dir[64];
		DIR *fds;
		struct dirent *fe;

		if (pid <= 1 || pid == self) continue;
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
			if (!buffers()) {
				fprintf(stderr, "portkeys: no buffers for the bar\n");
				release(false);
				return false;
			}
			/* The cursor window, until it says otherwise - unless the
			 * driver padded the rows after all, which only an overlay
			 * plane can be told of (ADDFB2 takes the pitch) */
			G.cursor = G.pitch == (uint32_t)G.cw * 4;
			if (!G.cursor) {
				fprintf(stderr, "portkeys: rows of %u bytes, not %d; using an overlay plane\n",
				        G.pitch, G.cw * 4);
				if (!overlay_setup()) {
					fprintf(stderr, "portkeys: no overlay plane either; no bar\n");
					release(false);
					return false;
				}
			}
			B.drawn_a = -1;     /* nothing of ours on these buffers yet */
			fprintf(stderr, "portkeys: showing the bar over pid %d (%dx%d)\n", pid, G.pw, G.ph);
			return true;
		}
		closedir(fds);
		if (pidfd >= 0) close(pidfd);
	}
	if (proc) closedir(proc);
	return false;
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
static bool put(float alpha, float rise)
{
	static uint32_t flat[OSDBAR_W * OSDBAR_H];
	int next = G.cur ^ 1, x, y, lx, ly, px, py;
	uint32_t *dst = G.map[next];

	/* In the theme bar_show resolved at the last press, the one the shelf
	 * would be in at that minute, so over a port the bar is the shelf's. */
	osdbar_render(flat, bw, bh, bw, PANEL_S, B.theme, B.kind, B.level, B.max, alpha, true);
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

static void bar_show(int kind, int level, int max)
{
	long now = now_ms();
	float a, rise;

	if (!held() && !borrow()) return;
	/* The theme is worked out here, on every press, and not once at start:
	 * a port can run for hours, and Auto turns at 7:00 and 19:00 whether the
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
	if (!B.live || !osdbar_envelope((uint32_t)(now - B.shown), (uint32_t)(now - B.changed), &a, &rise))
		B.shown = now;
	B.kind = kind;
	B.level = level;
	B.max = max;
	B.changed = now;
	B.live = true;
}

/* One frame of the bar's life, if it changed; how long until the next. While
 * it slides or fades that is a refresh, 16 ms, and a frame the window did not
 * take is tried again at the next. Every frame first asks whether the copy is
 * still the port's file and still the master. When the bar has gone, a clear
 * frame, then the window off and the copy let go of: the next press borrows
 * it again. Turning the window off and on again is an ordinary commit at each
 * end of a show, where a window kept on with nothing in it would have cost
 * none after the first; but a copy kept between shows keeps the port's
 * master with it (see the top of this file). */
static int bar_tick(void)
{
	long now = now_ms();
	float a, rise;
	bool alive;

	if (!B.live || !held()) return -1;
	alive = osdbar_envelope((uint32_t)(now - B.shown), (uint32_t)(now - B.changed), &a, &rise);
	if (!alive) a = 0, rise = 0;
	if (a != B.drawn_a || rise != B.drawn_rise || B.kind != B.drawn_kind || B.level != B.drawn_level ||
	    B.theme != B.drawn_theme) {
		if (!put(a, rise) && alive) return G.fd < 0 ? -1 : 16;
		B.drawn_a = a;
		B.drawn_rise = rise;
		B.drawn_kind = B.kind;
		B.drawn_level = B.level;
		B.drawn_theme = B.theme;
	}
	if (!alive) {
		release(false);
		return -1;
	}
	{
		uint32_t wait = osdbar_next_change((uint32_t)(now - B.shown), (uint32_t)(now - B.changed));
		return wait ? (int)wait : 16;
	}
}

/* ---- the keys ---------------------------------------------------------------- */

/* One step from tools/pkkeys.c: the level, saved when it moved, its line in
 * the log, the hardware and the bar. A press writes the hardware even at an
 * end of the scale, as it always has, which puts the level back if the port
 * moved the mixer itself; a repeat that moved nothing writes nothing, and
 * keeps the bar up. */
static void press(const pk_step *s)
{
	char line[96];
	bool moved = pk_apply(db, &L, s, line, sizeof line);
	bool vol = s->kind != PK_BRIGHT;

	if (line[0]) fputs(line, stderr);
	if (!vol) {
		if (moved || !s->held) apply_brightness();
		bar_show(OSDBAR_BRIGHT, L.bright, BRIGHT_MAX);
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
}

/* The battery as the launcher reads it (plat_battery, src/device/pixel2.c):
 * the level from the fuel gauge, and for the charger the cable, not the
 * battery's status, which says Discharging on the cable too; the status only
 * when the charger has no online file. /tmp/zyperos_fake_batt, a level, stands
 * in for both, as it does for the launcher. False with no level to read. */
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

static bool batt_now(int *pct, bool *charging)
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

/* Devices: the poll set's first n entries, and for each whether it has MENU
 * or the volume keys, whether the kernel's queue for it overflowed
 * (SYN_DROPPED) and what it sent since is still to be skipped, and whether
 * what is down on it must be queried after discarding this pass. */
static struct pollfd fds[9];
static bool has_menu[8], has_vol[8], dropped[8], resync[8];
static int n;
static pkkeys K;
/* A removed input invalidates the batch and its held state. */
static bool lost_menu, lost_vol;

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
 * src/main.c): BATT_EMPTY_PCT or under, off the charger, on two readings in a
 * row 5 s apart, so one stray reading never powers off. It ends the port and
 * powers off as POWER does, while the card can still be let go of, where the
 * PMIC's own cut would leave it mounted. Auto Off does not run over a port:
 * the launcher takes it only because a game resumes where it was after the
 * power-off (AUTO_OFF, src/main.c), and a port has no resume. */
#define BATT_EVERY_MS 5000
static long batt_next;
static int batt_seen;

/* One reading: true on the second empty one in a row, and only then */
static bool batt_empty(int pct, bool charging)
{
	if (!charging && pct >= 0 && pct <= BATT_EMPTY_PCT) batt_seen++;
	else batt_seen = 0;
	return batt_seen == 2;
}

static void power_read(void)
{
	struct input_event ev[16];
	ssize_t r;

	while ((r = read(powfd, ev, sizeof ev)) > 0) {
		size_t k;

		for (k = 0; k < (size_t)r / sizeof *ev; k++) {
			if (ev[k].type != EV_KEY || ev[k].code != KEY_POWER || ev[k].value != 1)
				continue;
			fprintf(stderr, "portkeys: POWER, the port ends\n");
			end_port();
		}
	}
	if (r == 0 || (r < 0 && errno != EAGAIN && errno != EINTR)) {
		close(powfd);
		powfd = -1;
	}
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
			if (has_menu[i]) pkkeys_menu_state(&K, K.menu_held || HAS(down, BTN_TRIGGER_HAPPY1));
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

	osdbar_size(PANEL_S, &bw, &bh);
	pkkeys_init(&K);
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
	/* The bar's theme (bar_show): MENU > Appearance, from the same device
	 * database the launcher reads it from, and the zone from the card's
	 * library, from launch.sh: $SHARED_USERDATA_PATH/.zyperos/library.db */
	appearance_read = pkset_text(db, "appearance", appearance_word, sizeof appearance_word);
	pkset_zone_load(argc > 2 ? argv[2] : NULL);

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

	for (;;) {
		static pk_key keys[PK_PASS_MAX];
		pk_step s[2];
		int wait = bar_tick(), rep = pkkeys_wait(&K, now_ms()), m, nk = 0;
		int pi = -1, pw = -1;
		bool plugged = false, unreliable = false;
		int64_t cutoff;

		if (!n && G.fd < 0) {
			fprintf(stderr, "portkeys: no keys left to read\n");
			return 0;
		}
		/* A held key's next repeat, or at once when events wait for this
		 * pass (pkkeys_wait) */
		if (rep >= 0 && (wait < 0 || rep < wait)) wait = rep;
		/* While the port's file is borrowed, look at it at least five times a
		 * second. held() (kcmp, and whether this is still the master) is asked
		 * in bar_tick, which runs when this returns, and the wait above is up
		 * to the end of the 1.5 s the bar shows when nothing is pressed: a port
		 * that gave the master up or closed the display meanwhile would be
		 * found at the end of it and not before, all that time with its file
		 * and its master kept alive by this copy (see the top of this file). */
		if (G.fd >= 0 && (wait < 0 || wait > 200)) wait = 200;
		/* The battery, every BATT_EVERY_MS (batt_empty) */
		{
			long now = now_ms();
			int pct;
			bool charging;

			if (now - batt_next >= 0) {
				if (batt_now(&pct, &charging) && batt_empty(pct, charging)) {
					fprintf(stderr, "portkeys: battery %d%%, empty; the port ends\n", pct);
					end_port();
				}
				batt_next = now + BATT_EVERY_MS;
			}
			if (wait < 0 || wait > batt_next - now) wait = (int)(batt_next - now);
		}
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
		if (poll(fds, (nfds_t)m, wait) < 0) continue;
		if (pi >= 0 && (fds[pi].revents & (POLLIN | POLLHUP | POLLERR))) release(true);
		if (pw >= 0 && fds[pw].revents) power_read();

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

		if (plugged) apply_volume();

		/* A held key's repeats, on the launcher's clock */
		m = pkkeys_tick(&K, now_ms(), s, 2);
		for (i = 0; i < m; i++) press(&s[i]);
	}
}
