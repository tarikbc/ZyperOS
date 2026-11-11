/* SPDX-License-Identifier: GPL-3.0-only */
/* The GKD Pixel 2: a 640x480 panel that is physically 480x640 portrait and
 * mounted turned, buttons read straight from the kernel, the RK817 codec, a
 * sysfs backlight and battery, and no radio. Its system is plastron, our own
 * Buildroot image: mainline Linux, Panfrost, SDL2 drawing through KMS.
 *
 * ZyperOS draws exactly as it does on the Brick, at 1024x768 landscape, into an
 * offscreen texture; plat_present scales that to 640x480 and turns it onto the
 * panel. Measured on this GPU 2026-10-01: a turned present holds the panel's
 * full 60 fps.
 *
 * The display is shared with Diatom by passing DRM master:
 * device_display_release before a game presents, device_display_take when the
 * launcher has the screen back. */
#include "atomic.h"
#include "charging.h"
#include "audioout.h"
#include "clamp.h"
#include "db.h"
#include "device.h"
#include "fileio.h"
#include "platform.h"
#include "portmenu.h"
#include "spawn.h"

#include <SDL_syswm.h>

#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <linux/input.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

const char *P_ROOT = "/mnt/SDCARD/ZyperOS";
const char *P_CARD = "/mnt/SDCARD";
const char *P_ROMS = "/mnt/SDCARD/Roms";
const char *P_USERDATA = "/mnt/SDCARD/.userdata/pixel2";
const char *P_SHARED = "/mnt/SDCARD/.userdata/shared";
const char *P_WEB = "/mnt/SDCARD/ZyperOS/res/web";
const char *P_FONT = "/mnt/SDCARD/ZyperOS/menu.ttf";

/* ---- the boot screen's theme ------------------------------------------------- */

/* theme.txt on the card's boot partition, PX2BOOT, the FAT one U-Boot reads
 * boot.scr and bootstate.txt from (src/boottheme.h). Mounted for the write
 * and let go: nothing else keeps it mounted, and System Update mounts it the
 * same way (update/slots.c). Written whole under another name and renamed
 * over, so a cut write leaves the old file, which boot.scr reads as it was. */
#define BOOT_MOUNT "/run/zyperos-boot-theme"

int plat_boot_theme(const char *text)
{
	char card[64] = "/dev/mmcblk0", dev[80], path[128], tmp[128], line[160];
	FILE *f = fopen("/etc/default/datapart", "r");
	size_t len = strlen(text);
	int fd, ok;

	while (f && fgets(line, sizeof line, f)) {
		if (strncmp(line, "CARD=", 5)) continue;
		line[5 + strcspn(line + 5, "\n \t\"'")] = 0;
		if (line[5]) snprintf(card, sizeof card, "%s", line + 5);
	}
	if (f) fclose(f);
	len = strlen(card);
	snprintf(dev, sizeof dev, "%s%s1", card, len && card[len - 1] >= '0' && card[len - 1] <= '9' ? "p" : "");
	mkdir(BOOT_MOUNT, 0700);
	if (mount(dev, BOOT_MOUNT, "vfat", MS_NOATIME | MS_NOSUID | MS_NODEV | MS_NOEXEC, "")) return -1;
	snprintf(path, sizeof path, "%s/theme.txt", BOOT_MOUNT);
	snprintf(tmp, sizeof tmp, "%s/theme.new", BOOT_MOUNT);
	len = strlen(text);
	fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	ok = fd >= 0 && write(fd, text, len) == (ssize_t)len && fsync(fd) == 0;
	if (fd >= 0 && close(fd)) ok = 0;
	ok = ok && rename(tmp, path) == 0;
	if (!ok) unlink(tmp);
	sync();
	umount(BOOT_MOUNT);
	return ok ? 1 : -1;
}

/* ---- the screen ------------------------------------------------------------ */

static SDL_Window   *win;
static SDL_Renderer *ren;
static SDL_Texture  *screen_tex;       /* ZYPEROS_SCREEN_W x H, what ZyperOS draws on */
static int           panel_w, panel_h; /* the panel as it is, portrait */
/* The panel's turn: -90 on the Pixel 2, whose 480x640 panel is mounted
 * turned; none on a landscape display, which is QEMU's (plastron's
 * qemu_defconfig, docs/qemu.md), where the same picture goes on unturned. */
static double        panel_turn = -90;
static int           drm_fd = -1;      /* SDL's, for passing the display to Diatom */

SDL_Renderer *plat_renderer(void) { return ren; }

/* DRM master, by its two ioctls from the kernel's drm.h rather than libdrm,
 * the same way the codec's controls are reached below: two numbers do not
 * need a library. */
#define PX_DRM_IOCTL_SET_MASTER  _IO('d', 0x1e)
#define PX_DRM_IOCTL_DROP_MASTER _IO('d', 0x1f)

/* The boot animation is plastron's splash, which holds the display while it
 * plays. ZyperOS starts alongside it and never waits for it: SDL builds
 * everything without the display (SDL_KMSDRM_REQUIRE_DRM_MASTER off), and the
 * first present tells the splash to stop and takes over from it. */
static void signal_splash(int sig)
{
	DIR *proc = opendir("/proc");
	struct dirent *e;

	while (proc && (e = readdir(proc))) {
		char path[64], comm[32] = { 0 };
		int fd;

		if (e->d_name[0] < '1' || e->d_name[0] > '9') continue;
		snprintf(path, sizeof path, "/proc/%s/comm", e->d_name);
		if ((fd = open(path, O_RDONLY | O_CLOEXEC)) < 0) continue;
		if (read(fd, comm, sizeof comm - 1) > 0 && !strcmp(comm, "splash\n"))
			kill(atoi(e->d_name), sig);
		close(fd);
	}
	if (proc) closedir(proc);
}

/* The splash stops on the frame it is showing and lets go of the display;
 * master being free again is the answer, so this tries until it is. A frame
 * and a flip at most, and bounded, so a stuck splash costs a log line and not
 * the launcher. */
static void take_from_splash(void)
{
	Uint32 t0 = SDL_GetTicks();

	signal_splash(SIGUSR1);
	while (drm_fd >= 0 && ioctl(drm_fd, PX_DRM_IOCTL_SET_MASTER, 0) != 0 &&
	       SDL_GetTicks() - t0 < 300)
		SDL_Delay(1);
	fprintf(stderr, "video: took the display from the splash in %u ms\n",
	        (unsigned)(SDL_GetTicks() - t0));
	/* On the kernel's clock, beside the splash's "panel lit", so the boot
	 * from power to shelf reads off one log. */
	{
		struct timespec now;
		char line[64];
		int fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);

		clock_gettime(CLOCK_BOOTTIME, &now);
		snprintf(line, sizeof line, "zyperos: first frame at %.3f s\n",
		         now.tv_sec + now.tv_nsec / 1e9);
		if (fd >= 0) {
			if (write(fd, line, strlen(line)) < 0) { /* not fatal */ }
			close(fd);
		}
	}
}

/* A video under this frame: plat_video_under. One frame's worth, cleared by
 * the present that shows it. */
static struct {
	bool         on;
	SDL_Texture *tex;
	SDL_Rect     dst;                  /* in screen coordinates */
	SDL_Color    bg;
} under;

void plat_video_under(SDL_Texture *tex, const SDL_Rect *dst, SDL_Color bg)
{
	SDL_BlendMode mode;

	under.on = true;
	under.tex = tex;
	under.dst = dst ? *dst : (SDL_Rect){ 0, 0, 0, 0 };
	under.bg = bg;
	/* The screen clear, so the picture shows through wherever this frame
	 * draws nothing. */
	SDL_SetRenderTarget(ren, screen_tex);
	SDL_GetRenderDrawBlendMode(ren, &mode);
	SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
	SDL_SetRenderDrawColor(ren, 0, 0, 0, 0);
	SDL_RenderClear(ren);
	SDL_SetRenderDrawBlendMode(ren, mode);
}

/* The video picture at the panel's own size: its rectangle on the 1024x768
 * screen scaled to the 640x480 one, and turned with it about the panel's
 * middle - so it lands exactly where the screen's own scaled picture puts it. */
static void show_under(void)
{
	float s = 640.0f / ZYPEROS_SCREEN_W;
	float w = under.dst.w * s, h = under.dst.h * s;
	/* Its middle from the landscape picture's middle, then the panel's turn:
	 * a quarter turn counter-clockwise on the Pixel 2, (dx, dy) going to
	 * (dy, -dx); none on a landscape display (QEMU's). */
	float dx = (under.dst.x + under.dst.w / 2.0f) * s - 320.0f;
	float dy = (under.dst.y + under.dst.h / 2.0f) * s - 240.0f;
	float cx = panel_w / 2.0f + (panel_turn ? dy : dx);
	float cy = panel_h / 2.0f + (panel_turn ? -dx : dy);
	SDL_FRect r = { cx - w / 2, cy - h / 2, w, h };

	SDL_SetRenderDrawColor(ren, under.bg.r, under.bg.g, under.bg.b, 255);
	SDL_RenderClear(ren);
	if (under.tex) SDL_RenderCopyExF(ren, under.tex, NULL, &r, panel_turn, NULL, SDL_FLIP_NONE);
}

/* Black around the picture, then the picture: 1024x768 down to 640x480 and a
 * quarter turn counter-clockwise, which on this panel ("Left Side Up") is the
 * right way up - checked by eye 2026-10-01 with a marker drawn top-left.
 *
 * Over a video, the screen is mostly clear (plat_video_under) and goes on top
 * of it. What was drawn into the clear texture is premultiplied by alpha -
 * SDL's blend writes color times alpha into it - so it goes over with ONE,
 * not SRC_ALPHA, or every soft edge over the picture would come out dark. */
static void show_screen(void)
{
	SDL_Rect dest = { (panel_w - 640) / 2, (panel_h - 480) / 2, 640, 480 };

	SDL_SetRenderTarget(ren, NULL);
	if (under.on) {
		static SDL_BlendMode premul;

		if (!premul)
			premul = SDL_ComposeCustomBlendMode(
				SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
				SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
		show_under();
		SDL_SetTextureBlendMode(screen_tex, premul);
		SDL_RenderCopyEx(ren, screen_tex, NULL, &dest, panel_turn, NULL, SDL_FLIP_NONE);
		SDL_SetTextureBlendMode(screen_tex, SDL_BLENDMODE_BLEND);
		under.on = false;
	} else {
		SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
		SDL_RenderClear(ren);
		SDL_RenderCopyEx(ren, screen_tex, NULL, &dest, panel_turn, NULL, SDL_FLIP_NONE);
	}
	SDL_RenderPresent(ren);
	SDL_SetRenderTarget(ren, screen_tex);
}

/* The draw color is put back, because ZyperOS sets it and expects it to stay.
 *
 * The first present after taking the display from the splash shows TWICE.
 * SDL's first frame on a display it did not own when it started never reaches
 * the panel: filmed on 2026-10-01, the logo went to black for exactly one
 * second, which is the shelf loop's heartbeat redraw - ZyperOS draws only when
 * something changes, so a lost first frame stayed lost until then. The frame
 * is still in screen_tex, so the second showing costs one copy, and when it
 * returns ZyperOS's picture is on glass.
 *
 * Then the splash is ended. Its logo is off the screen by then, so nothing
 * shows; and gone, it leaves SDL nothing to restore when ZyperOS exits, which
 * had put the logo back up at power-off. */
void plat_present(void)
{
	static bool shown;
	Uint8 r, g, b, a;

	SDL_GetRenderDrawColor(ren, &r, &g, &b, &a);
	plat_overlay_present(ren);
	if (!shown) {
		take_from_splash();
		show_screen();
		show_screen();
		signal_splash(SIGTERM);
		shown = true;
	} else {
		show_screen();
	}
	SDL_SetRenderDrawColor(ren, r, g, b, a);
}

void plat_draw_to_screen(void) { SDL_SetRenderTarget(ren, screen_tex); }

/* show_screen's picture, read back from the panel's own buffer before it is
 * presented, so the file is what the panel shows: 480x640, turned (640x480
 * on a landscape display). */
bool plat_save_screen(const char *path)
{
	SDL_Rect dest = { (panel_w - 640) / 2, (panel_h - 480) / 2, 640, 480 };
	size_t n = (size_t)panel_w * (size_t)panel_h * 4;
	void *px = malloc(n);
	FILE *f;
	bool ok = false;

	SDL_SetRenderTarget(ren, NULL);
	SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
	SDL_RenderClear(ren);
	SDL_RenderCopyEx(ren, screen_tex, NULL, &dest, panel_turn, NULL, SDL_FLIP_NONE);
	if (px && SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_XRGB8888, px, panel_w * 4) == 0 &&
	    (f = fopen(path, "wb"))) {
		ok = fwrite(px, 1, n, f) == n;
		if (fclose(f) != 0) ok = false;
	}
	free(px);
	SDL_RenderPresent(ren);
	SDL_SetRenderTarget(ren, screen_tex);
	return ok;
}

/* The still portkeys saves (src/portmenu.h): "ZPST", the width and the
 * height, then the panel's rows as it showed them. On the turned panel a
 * landscape pixel (x, y) is the panel's (y, W - 1 - x), W the landscape width,
 * as plat_save_screen turns it there, so it is turned back here; on a
 * landscape display (QEMU's) it is the picture as it is. */
SDL_Texture *plat_load_screen(const char *path)
{
	FILE *f = fopen(path, "rb");
	unsigned char hdr[8];
	uint32_t *in = NULL;
	SDL_Surface *sf = NULL;
	SDL_Texture *tex = NULL;
	int w, h, x, y;
	bool turned;

	if (!f) return NULL;
	if (fread(hdr, 1, sizeof hdr, f) != sizeof hdr || memcmp(hdr, PORT_MENU_MAGIC, 4)) goto done;
	w = hdr[4] | hdr[5] << 8;
	h = hdr[6] | hdr[7] << 8;
	if (w != panel_w || h != panel_h) {
		fprintf(stderr, "port menu: the still is %dx%d, the panel %dx%d; none\n", w, h, panel_w, panel_h);
		goto done;
	}
	turned = panel_turn != 0;
	if (!(in = malloc((size_t)w * h * 4)) || fread(in, 4, (size_t)w * h, f) != (size_t)w * h) goto done;
	if (!(sf = SDL_CreateRGBSurfaceWithFormat(0, turned ? h : w, turned ? w : h, 32,
	                                           SDL_PIXELFORMAT_ARGB8888)))
		goto done;
	for (y = 0; y < sf->h; y++) {
		uint32_t *row = (uint32_t *)((uint8_t *)sf->pixels + (size_t)y * sf->pitch);

		for (x = 0; x < sf->w; x++)
			row[x] = 0xFF000000u | (turned ? in[(size_t)(sf->w - 1 - x) * w + y]
			                               : in[(size_t)y * w + x]);
	}
	tex = SDL_CreateTextureFromSurface(ren, sf);
done:
	if (sf) SDL_FreeSurface(sf);
	free(in);
	fclose(f);
	return tex;
}

bool plat_video_init(void)
{
	SDL_DisplayMode mode;
	SDL_SysWMinfo info;
	SDL_RendererInfo ri;

	/* Linear, so the scale from 1024x768 to 640x480 is smooth rather than
	 * dropping every third line. */
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
	SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengles2");
	/* Batched, as SDL batches when no driver is named: naming one turns it
	 * off (SDL_CreateRenderer, for a program that may call GL itself, which
	 * this one never does). Unbatched, every SDL call was its own GL draw,
	 * set up and flushed on its own: each one-unit row of a rounded corner,
	 * each glyph. Batched, a run of fills or of one texture's copies is one
	 * draw, and the same pixels come out. */
	SDL_SetHint(SDL_HINT_RENDER_BATCHING, "1");
	/* The boot animation holds the display until the first present; see
	 * take_from_splash. */
	SDL_SetHint(SDL_HINT_KMSDRM_REQUIRE_DRM_MASTER, "0");
	if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
		fprintf(stderr, "video init: %s\n", SDL_GetError());
		return false;
	}
	if (SDL_GetDesktopDisplayMode(0, &mode) != 0) {
		fprintf(stderr, "display mode: %s\n", SDL_GetError());
		return false;
	}
	panel_w = mode.w;
	panel_h = mode.h;
	panel_turn = panel_h > panel_w ? -90 : 0;
	win = SDL_CreateWindow("ZyperOS", 0, 0, panel_w, panel_h,
	                       SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_SHOWN);
	if (!win) {
		fprintf(stderr, "window: %s\n", SDL_GetError());
		return false;
	}
	/* KMS draws a pointer unless told not to. */
	SDL_ShowCursor(SDL_DISABLE);
	ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (!ren) {
		fprintf(stderr, "renderer: %s\n", SDL_GetError());
		return false;
	}
	screen_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET,
	                               ZYPEROS_SCREEN_W, ZYPEROS_SCREEN_H);
	if (!screen_tex) {
		fprintf(stderr, "screen texture: %s\n", SDL_GetError());
		return false;
	}
	SDL_SetRenderTarget(ren, screen_tex);
	SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

	SDL_VERSION(&info.version);
	if (SDL_GetWindowWMInfo(win, &info) && info.subsystem == SDL_SYSWM_KMSDRM)
		drm_fd = info.info.kmsdrm.drm_fd;
	else
		fprintf(stderr, "video: no DRM handle; games cannot take the display\n");

	if (SDL_GetRendererInfo(ren, &ri) == 0)
		fprintf(stderr, "renderer: %s, driver: %s, vsync %s, panel %dx%d\n",
		        ri.name, SDL_GetCurrentVideoDriver(),
		        (ri.flags & SDL_RENDERER_PRESENTVSYNC) ? "granted" : "DECLINED (requested)",
		        panel_w, panel_h);
	return true;
}

void plat_video_quit(void)
{
	if (screen_tex) { SDL_DestroyTexture(screen_tex); screen_tex = NULL; }
	plat_osd_forget();
	if (ren) { SDL_DestroyRenderer(ren); ren = NULL; }
	if (win) { SDL_DestroyWindow(win); win = NULL; }
	drm_fd = -1;
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

/* Diatom is about to present: it cannot while this process holds master. */
void device_display_release(void)
{
	if (drm_fd >= 0) ioctl(drm_fd, PX_DRM_IOCTL_DROP_MASTER, 0);
}

/* Diatom has given the display back (it drops master before PAUSED and EXIT).
 * The next plat_present flips onto the CRTC Diatom left lit. */
void device_display_take(void)
{
	if (drm_fd >= 0 && ioctl(drm_fd, PX_DRM_IOCTL_SET_MASTER, 0) != 0)
		fprintf(stderr, "video: could not take the display back\n");
}

/* ---- input ----------------------------------------------------------------- */

#define BITS_PER_LONG   (8 * (int)sizeof(long))
#define NLONGS(max)     (((max) + BITS_PER_LONG) / BITS_PER_LONG)
#define BIT_IS_SET(a,b) (((a)[(b) / BITS_PER_LONG] >> ((b) % BITS_PER_LONG)) & 1UL)

static int fd_pad = -1;     /* "gkd_pixel2_joypad": the face, d-pad, shoulders, FUNCTION */
static int fd_keys = -1;    /* "gpio-keys": volume */
static int fd_power = -1;   /* "rk805 pwrkey" */
static int jack_fd = -1;    /* "rk817_int Headphones": the jack switch, for the settings */
static int dbg_input;
static bool l2_down, r2_down;  /* the two together ask for the battery level (below) */
static bool l2r2_chorded;      /* ...and have, since both were last up */
static bool l2_tap, r2_tap;    /* a tap reported last poll ends this one */

/* Kernel code -> ZyperOS button, by what is printed on the key. Measured
 * 2026-10-01 pressing each in a known order: the A, B, X and Y caps send
 * SOUTH, EAST, NORTH and WEST - named for their labels, not for where they
 * sit, since A is on the right and B at the bottom. FUNCTION is MENU, and L2
 * and R2 are the battery level together and a tap each alone, all three
 * handled apart below. */
static const struct { int code; in_button b; } keymap[] = {
	{ BTN_DPAD_UP, IN_UP },       { BTN_DPAD_DOWN, IN_DOWN },
	{ BTN_DPAD_LEFT, IN_LEFT },   { BTN_DPAD_RIGHT, IN_RIGHT },
	{ BTN_SOUTH, IN_ACCEPT },     { BTN_EAST, IN_BACK },
	{ BTN_NORTH, IN_X },          { BTN_WEST, IN_Y },
	{ BTN_TL, IN_L1 },            { BTN_TR, IN_R1 },
	{ BTN_SELECT, IN_SELECT },    { BTN_START, IN_START },
};
#define CODE_FUNCTION BTN_TRIGGER_HAPPY1

/* Every input device this file wants, found by what each reports, not by
 * number: there is no udev here, and enumeration order is not a promise.
 *
 * In one pass, keeping every node it wants and closing only the ones it does
 * not. Closing an evdev descriptor waits out an RCU grace period in the kernel
 * (evdev_detach_client), 10 to 50 ms each on this device while the boot is
 * busy, and the scan this replaced - one per device, closing every node it
 * passed over - spent about 190 ms of the boot in close(), measured
 * 2026-10-02. On this device every node is one of the four, so none is
 * closed at all. */
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
		if      (fd_pad < 0   && BIT_IS_SET(keys, BTN_SOUTH))           fd_pad = fd;
		else if (fd_keys < 0  && BIT_IS_SET(keys, KEY_VOLUMEUP))        fd_keys = fd;
		else if (fd_power < 0 && BIT_IS_SET(keys, KEY_POWER))           fd_power = fd;
		else if (jack_fd < 0  && BIT_IS_SET(sw, SW_HEADPHONE_INSERT))   jack_fd = fd;
		else kept = false;
		if (!kept) close(fd);
	}
}

bool plat_input_init(void)
{
	char marker[512];

	snprintf(marker, sizeof marker, "%s/.input_debug", P_ROOT);
	dbg_input = getenv("ZYPEROS_INPUT_DEBUG") != NULL || access(marker, F_OK) == 0;
	if (fd_pad < 0 || fd_keys < 0 || fd_power < 0) input_scan();
	if (fd_pad < 0)   fprintf(stderr, "input: no gamepad found\n");
	if (fd_keys < 0)  fprintf(stderr, "input: no volume keys found\n");
	if (fd_power < 0) fprintf(stderr, "input: no power key found\n");
	return true;
}

static void drain(int fd)
{
	struct input_event ev;

	while (fd >= 0 && read(fd, &ev, sizeof ev) == (ssize_t)sizeof ev) { }
}

/* Throw away everything that queued up while a game owned the screen. */
void plat_input_flush(void)
{
	SDL_Event e;

	SDL_PumpEvents();
	while (SDL_PollEvent(&e)) { }
	drain(fd_pad);
	drain(fd_keys);
	drain(fd_power);
	l2_down = r2_down = false;
}

/* No wait here, unlike the Brick: the keys are read straight from the kernel,
 * with no daemon or joystick layer coming up in bursts, so whatever the boot
 * produced is already queued and emptying the queues is enough. Measured
 * 2026-10-02: the Brick's 350 ms caught nothing here, power-button boots
 * included. */
void plat_input_settle(in_state *st)
{
	plat_input_flush();
	memset(st, 0, sizeof *st);
}

/* The descriptors stay open: they are display-independent. */
void plat_input_quit(void) { }

/* FUNCTION is MENU, and also the modifier that makes the volume keys
 * brightness: this device has no brightness keys, and MENU plus volume is
 * brightness in games too, so the same chord does the same thing on both
 * sides of a launch.
 *
 * A key that is both cannot report MENU when it goes down, because nothing yet
 * says whether a volume key will follow. So MENU is reported when FUNCTION
 * comes UP, unless a volume key came in between. That costs a tap's length
 * before a menu opens, accepted on 2026-10-01; in games MENU has always worked
 * this way. Holding both volume keys with FUNCTION is holding both brightness
 * keys, which is the Brick's pocket lock, so Muse's lock works unchanged. */
static bool fn_down;            /* FUNCTION is held */
static bool fn_chorded;         /* and a volume key was used under it */
static bool menu_release_next;  /* a MENU tap reported last poll ends this one */
static in_button vol_as[2] = { IN_NONE, IN_NONE };  /* what each volume key went down as */

/* The lights on the side are plastron's gauge's: the launcher only signals it,
 * SIGUSR1 to flash the level and SIGHUP to read MENU > Side Lights again. */
static void signal_gauge(int sig)
{
	DIR *d = opendir("/proc");
	struct dirent *e;

	if (!d) return;
	while ((e = readdir(d))) {
		char path[48], comm[32];
		ssize_t n;
		int fd;

		if (e->d_name[0] < '1' || e->d_name[0] > '9') continue;
		snprintf(path, sizeof path, "/proc/%.16s/comm", e->d_name);
		if ((fd = open(path, O_RDONLY | O_CLOEXEC)) < 0) continue;
		n = read(fd, comm, sizeof comm - 1);
		close(fd);
		if (n <= 0) continue;
		comm[n] = '\0';
		if (!strcmp(comm, "gauge\n")) {
			kill((pid_t)atoi(e->d_name), sig);
			break;
		}
	}
	closedir(d);
}

/* L2 and R2 together flash the battery level on them. Seen here only outside a
 * game, since in one Diatom has the pad (and on GBA they are mGBA's turbo
 * shoulders), and what a game left queued is thrown away unread.
 *
 * Alone, each is a button of the launcher's, IN_L2 and IN_R2, the menu
 * music's track before and after on the shelf (2026-11-06). Reported
 * as it is let go, the way FUNCTION is MENU, and only when the other was not
 * pressed with it since both were last up: asking for the level is not also
 * a change of song. The tap is down for one poll, like MENU's. */
static void ask_gauge(void) { signal_gauge(SIGUSR1); }

static void key_event(in_state *st, const struct input_event *ev)
{
	size_t i;

	if (ev->type != EV_KEY || ev->value == 2) return;
	if (dbg_input) fprintf(stderr, "[in] code=%d val=%d\n", ev->code, ev->value);

	if (ev->code == CODE_FUNCTION) {
		if (ev->value) {
			fn_down = true;
			fn_chorded = false;
		} else {
			fn_down = false;
			if (!fn_chorded) {
				plat_set_btn(st, IN_MENU, true);
				menu_release_next = true;
			}
		}
		return;
	}
	if (ev->code == KEY_VOLUMEUP || ev->code == KEY_VOLUMEDOWN) {
		int k = ev->code == KEY_VOLUMEUP ? 0 : 1;

		if (ev->value) {
			if (fn_down) {
				vol_as[k] = k == 0 ? IN_BRIGHTUP : IN_BRIGHTDN;
				fn_chorded = true;
			} else {
				vol_as[k] = k == 0 ? IN_VOLUP : IN_VOLDN;
			}
			plat_set_btn(st, vol_as[k], true);
		} else if (vol_as[k] != IN_NONE) {
			/* Released as whatever it went down as, so letting go of
			 * FUNCTION first does not leave a brightness key stuck down. */
			plat_set_btn(st, vol_as[k], false);
			vol_as[k] = IN_NONE;
		}
		return;
	}
	if (ev->code == KEY_POWER) {
		plat_set_btn(st, IN_POWER, ev->value == 1);
		return;
	}
	if (ev->code == BTN_TL2 || ev->code == BTN_TR2) {
		bool both = l2_down && r2_down, l2 = ev->code == BTN_TL2;

		*(l2 ? &l2_down : &r2_down) = ev->value == 1;
		if (!both && l2_down && r2_down) {
			ask_gauge();
			l2r2_chorded = true;
		}
		if (!ev->value && !l2r2_chorded) {
			plat_set_btn(st, l2 ? IN_L2 : IN_R2, true);
			*(l2 ? &l2_tap : &r2_tap) = true;
		}
		if (!l2_down && !r2_down) l2r2_chorded = false;
		return;
	}
	for (i = 0; i < sizeof keymap / sizeof keymap[0]; i++)
		if (keymap[i].code == ev->code) {
			plat_set_btn(st, keymap[i].b, ev->value == 1);
			return;
		}
}

static void poll_fd(int fd, in_state *st)
{
	struct input_event ev;

	while (fd >= 0 && read(fd, &ev, sizeof ev) == (ssize_t)sizeof ev)
		key_event(st, &ev);
}

void plat_input_wait(unsigned ms)
{
	struct pollfd p[3];
	nfds_t n = 0;

	if (fd_pad >= 0)   p[n++] = (struct pollfd){ .fd = fd_pad,   .events = POLLIN };
	if (fd_keys >= 0)  p[n++] = (struct pollfd){ .fd = fd_keys,  .events = POLLIN };
	if (fd_power >= 0) p[n++] = (struct pollfd){ .fd = fd_power, .events = POLLIN };
	if (!n) { SDL_Delay(ms); return; }
	/* A signal ends it early too, which is what a stop should do */
	poll(p, n, (int)ms);
}

void plat_input_poll(in_state *st)
{
	SDL_Event e;

	memset(st->pressed, 0, sizeof st->pressed);
	if (plat_terminating()) st->quit_requested = true;
	if (menu_release_next) {
		plat_set_btn(st, IN_MENU, false);
		menu_release_next = false;
	}
	if (l2_tap) { plat_set_btn(st, IN_L2, false); l2_tap = false; }
	if (r2_tap) { plat_set_btn(st, IN_R2, false); r2_tap = false; }
	/* Whoever pumps input owns the volume; see the Brick's file. */
	plat_audio_jack_poll();
	while (SDL_PollEvent(&e))
		if (e.type == SDL_QUIT) st->quit_requested = true;
	poll_fd(fd_pad, st);
	poll_fd(fd_keys, st);
	poll_fd(fd_power, st);
}

bool device_power_pressed(void)
{
	struct input_event ev;
	bool pressed = false;

	while (fd_power >= 0 && read(fd_power, &ev, sizeof ev) == (ssize_t)sizeof ev)
		if (ev.type == EV_KEY && ev.code == KEY_POWER && ev.value == 1)
			pressed = true;
	return pressed;
}

/* ---- volume and brightness ------------------------------------------------- */

/* The codec's controls by ioctl on /dev/snd/controlC0, the kernel's struct
 * vendored as the Brick's file does it (see there for why). */
struct px_ctl_elem_id {
	unsigned int  numid;
	int           iface;
	unsigned int  device;
	unsigned int  subdevice;
	unsigned char name[44];
	unsigned int  index;
};
struct px_aes_iec958 {
	unsigned char status[24], subcode[147], pad, dig_subframe[4];
};
struct px_ctl_elem_value {
	struct px_ctl_elem_id id;
	unsigned int indirect: 1;
	union {
		union { long value[128]; long *value_ptr; } integer;
		union { long long value[64]; long long *value_ptr; } integer64;
		union { unsigned int item[128]; unsigned int *item_ptr; } enumerated;
		union { unsigned char data[512]; unsigned char *data_ptr; } bytes;
		struct px_aes_iec958 iec958;
	} value;
	unsigned char reserved[128];
};
#define PX_CTL_ELEM_WRITE _IOWR('U', 0x13, struct px_ctl_elem_value)

/* And for making a control: the integer case of the info struct, its union at
 * full size for the same reason, and the TLV header alone, since the request
 * number holds the header's size and not the data's. */
struct px_ctl_elem_info {
	struct px_ctl_elem_id id;
	int           type;
	unsigned int  access;
	unsigned int  count;
	int           owner;
	union {
		struct { long min, max, step; } integer;
		unsigned char reserved[128];
	} value;
	unsigned char reserved[64];
};
struct px_ctl_tlv {
	unsigned int numid;
	unsigned int length;
	unsigned int tlv[];
};
#define PX_CTL_ELEM_INFO  _IOWR('U', 0x11, struct px_ctl_elem_info)
#define PX_CTL_ELEM_ADD   _IOWR('U', 0x17, struct px_ctl_elem_info)
#define PX_CTL_TLV_WRITE  _IOWR('U', 0x1b, struct px_ctl_tlv)
#define PX_CTL_ELEM_UNLOCK _IOW('U', 0x15, struct px_ctl_elem_id)

/* The levels' numbers - the codec's windows, the DAC's, the backlight's
 * rungs - and why each is what it is: src/device/pixel2_levels.h, shared with
 * tools/portkeys.c, which keeps these keys while a port runs. */
#include "pixel2_levels.h"
_Static_assert(VOL_MAX == PLAT_VOL_MAX, "pixel2_levels.h vs PLAT_VOL_MAX");
_Static_assert(BRIGHT_MAX == PLAT_BRIGHT_MAX, "bright_ladder vs PLAT_BRIGHT_MAX");

static int mixer_fd = -1;
static int cur_vol = -1, cur_bright = -1;
static int jack_was = -1;

static void write_file(const char *path, const char *text)
{
	int fd = open(path, O_WRONLY | O_CLOEXEC);

	if (fd < 0) return;
	if (write(fd, text, strlen(text)) < 0) { /* best effort */ }
	close(fd);
}

static int read_int(const char *path)
{
	char buf[16];

	return file_head(path, buf, sizeof buf) > 0 ? atoi(buf) : -1;
}

/* Found by input_scan with the buttons, which plat_input_init runs first. */
static void jack_open(void)
{
	if (jack_fd < 0) input_scan();
	if (jack_fd < 0) fprintf(stderr, "settings: no headphone jack input node\n");
}

static int jack_present(void)
{
	unsigned long bits[NLONGS(SW_MAX)];

	if (jack_fd < 0) return 0;
	memset(bits, 0, sizeof bits);
	if (ioctl(jack_fd, EVIOCGSW(sizeof bits), bits) < 0) return 0;
	return BIT_IS_SET(bits, SW_HEADPHONE_INSERT) ? 1 : 0;
}

static void ctl_write(const char *name, long raw)
{
	struct px_ctl_elem_value c;

	if (mixer_fd < 0) return;
	memset(&c, 0, sizeof c);
	c.id.iface = 2;                                 /* SNDRV_CTL_ELEM_IFACE_MIXER */
	snprintf((char *)c.id.name, sizeof c.id.name, "%s", name);
	/* Both channels: one written alone reads as a balance problem. */
	c.value.integer.value[0] = raw;
	c.value.integer.value[1] = raw;
	if (ioctl(mixer_fd, PX_CTL_ELEM_WRITE, &c) < 0)
		fprintf(stderr, "settings: mixer rejected '%s' = %ld\n", name, raw);
}

static void apply_volume(int v)
{
	int hp = jack_present();
	long raw = px2_gain_raw(v, hp);

	jack_was = hp;
	cur_vol = v;
	ctl_write(GAIN_CTL, raw);
	ctl_write(USB_CTL, px2_usb_raw(v));
}

/* alsa-lib makes the USB control itself the first time something plays
 * through a DAC, at 0 dB, the loudest there is. Made here first, before Muse
 * or a game can play, it is kept as it is, at the player's level. It has to
 * be the shape alsa-lib checks for, or it is replaced: two channels, 0-255,
 * and a dB scale. A control added by hand stays until the next boot, so a
 * restart finds it there. */
static void usb_volume_create(void)
{
	struct px_ctl_elem_info info;
	unsigned int tlv[6];

	if (mixer_fd < 0) return;
	memset(&info, 0, sizeof info);
	info.id.iface = 2;                              /* SNDRV_CTL_ELEM_IFACE_MIXER */
	snprintf((char *)info.id.name, sizeof info.id.name, "%s", USB_CTL);
	if (ioctl(mixer_fd, PX_CTL_ELEM_INFO, &info) == 0) return;
	info.type   = 2;                                /* SNDRV_CTL_ELEM_TYPE_INTEGER */
	info.access = 0x3 | 0x20;                       /* READWRITE, TLV_WRITE */
	info.count  = 2;
	info.owner  = 1;                                /* one element, on adding */
	info.value.integer.max = USB_RAW_MAX;
	if (ioctl(mixer_fd, PX_CTL_ELEM_ADD, &info) < 0) {
		fprintf(stderr, "settings: could not add '%s'\n", USB_CTL);
		return;
	}
	tlv[0] = info.id.numid;
	tlv[1] = 4 * sizeof tlv[0];
	tlv[2] = 1;                                     /* SNDRV_CTL_TLVT_DB_SCALE */
	tlv[3] = 2 * sizeof tlv[0];
	tlv[4] = (unsigned int)USB_DB_MIN;
	tlv[5] = USB_DB_STEP;
	if (ioctl(mixer_fd, PX_CTL_TLV_WRITE, tlv) < 0)
		fprintf(stderr, "settings: no dB scale for '%s'\n", USB_CTL);
	/* The kernel locks a control added by hand to whoever added it, for as
	 * long as they hold the card open, and this holds it open for good: a
	 * game's volume presses, written by Diatom, were refused until the menu
	 * had the launcher write the level (seen 2026-10-05). */
	if (ioctl(mixer_fd, PX_CTL_ELEM_UNLOCK, &info.id) < 0)
		fprintf(stderr, "settings: could not unlock '%s'\n", USB_CTL);
	/* Added at 0, silence: the middle until a level is known. */
	ctl_write(USB_CTL, px2_usb_raw(VOL_MAX / 2));
}

static void apply_brightness(int b)
{
	char text[8];

	cur_bright = b;
	snprintf(text, sizeof text, "%d\n", bright_ladder[b]);
	write_file(BACKLIGHT, text);
}

static void levels_save(void)
{
	db_set_int(db_dev(), "volume", cur_vol);
	db_set_int(db_dev(), "brightness", cur_bright);
}

void plat_settings_init(void)
{
	int v, b, raw;

	mixer_fd = open("/dev/snd/controlC0", O_RDWR | O_CLOEXEC);
	if (mixer_fd < 0) fprintf(stderr, "settings: no /dev/snd/controlC0\n");
	jack_open();

	/* The player's last choice wins, as on the Brick; failing that, whatever
	 * the panel is already at, to the nearest rung. */
	v = db_get_int(db_dev(), "volume", -1);
	b = db_get_int(db_dev(), "brightness", -1);
	if (b < 0 && (raw = read_int(BACKLIGHT)) >= 0) {
		int i;

		b = 0;
		for (i = 1; i <= BRIGHT_MAX; i++)
			if (abs(bright_ladder[i] - raw) < abs(bright_ladder[b] - raw)) b = i;
	}
	cur_vol    = v >= 0 ? clampi(v, 0, VOL_MAX)    : -1;
	cur_bright = b >= 0 ? clampi(b, 0, BRIGHT_MAX) : BRIGHT_MAX / 2;
	usb_volume_create();
	if (cur_vol >= 0) apply_volume(cur_vol);
	apply_brightness(cur_bright);
	/* Lit, whatever the launcher before this one left: it turns the light off
	 * for Muse's lock and for the shelf on the charger (src/chargedark.h),
	 * and one killed in either kept the panel dark until the next boot. */
	plat_backlight(true);
}

/* Off and back on at the same level: bl_power, the backlight's own switch, so
 * the brightness is not touched. Muse's pocket lock. */
void plat_backlight(bool on)
{
	write_file(BACKLIGHT_POWER, on ? "0\n" : "4\n");
}

void plat_backlight_dim(bool dim)
{
	char text[8];

	if (cur_bright < 0) return;
	snprintf(text, sizeof text, "%d\n", bright_ladder[dim ? 0 : cur_bright]);
	write_file(BACKLIGHT, text);
}

/* The windows differ by jack, so the level is re-applied when a plug goes in
 * or out, as on the Brick. */
void plat_audio_jack_poll(void)
{
	if (!aout_should_reapply(jack_was, jack_present() != 0, cur_vol >= 0)) return;
	apply_volume(cur_vol);
}

bool plat_headphones_present(void) { return jack_present() != 0; }

/* The RK817 is card 0, so a card 1 that has a USB id is a USB-C DAC: plastron's
 * usbrole makes the port a host when one is plugged in, and the kernel's USB
 * audio driver adds it. One stat, asked each time the output is decided. */
bool plat_usb_audio_present(void)
{
	return access("/proc/asound/card1/usbid", F_OK) == 0;
}

/* Files over the USB cable: plastron gives usb0 this address and hands the
 * computer one of its own (S40usbgadget), so the screen shows the same thing
 * every time, cable in or not. Whether a computer is there is left to the
 * screen's browser row: the USB controller went on saying "configured" with
 * the cable pulled (2026-10-05), and the row already says it.
 *
 * Not while the port is a host, which usbrole makes it for a Wi-Fi adapter or
 * a DAC: the one port can't be on a computer's cable then. With an adapter,
 * files go over the air instead, Over The Hare as on the Brick. One small
 * read of the role switch, asked as menus are built. */
#define CABLE_ADDR "10.42.0.1"
#define USB_ROLE   "/sys/class/usb_role/ff300000.usb-role-switch/role"

bool plat_cable_link(char *addr, size_t n)
{
	char role[16] = "";
	int fd = open(USB_ROLE, O_RDONLY | O_CLOEXEC);

	if (fd >= 0) {
		if (read(fd, role, sizeof role - 1) < 0) role[0] = '\0';
		close(fd);
	}
	if (!strncmp(role, "host", 4)) return false;
	if (addr && n) snprintf(addr, n, "%s", CABLE_ADDR);
	return true;
}

/* What is on the port, from the PHY's extcon (phy-rockchip-inno-usb2): its
 * charger detection marks a computer's port SDP or CDP and a wall charger
 * DCP, with or without the gadget bound, and USB-HOST is the ID pin of a host
 * cable. Every extcon's "state" is read, lines like "SDP=1". */
usb_peer plat_usb_peer(void)
{
	DIR *d;
	struct dirent *e;
	bool host = false, data = false, dcp = false;
	char in[8] = "";
	int fd;

	/* No power coming in is nothing on the port, whatever the extcon still
	 * says: the RK817's input answers on its own interrupt, at once, where
	 * the PHY's detection runs on a timer and could keep a quick replug's
	 * old answer, so a cable out and in again within a second looked like
	 * one plug (2026-11-05) */
	if ((fd = open("/sys/class/power_supply/rk817-charger/online", O_RDONLY | O_CLOEXEC)) >= 0) {
		if (read(fd, in, sizeof in - 1) < 0) in[0] = '\0';
		close(fd);
		if (in[0] == '0') return USB_PEER_NONE;
	}
	if (!(d = opendir("/sys/class/extcon"))) return USB_PEER_NONE;
	while ((e = readdir(d))) {
		char path[300], buf[256];
		ssize_t got;
		int fd;

		if (e->d_name[0] == '.') continue;
		snprintf(path, sizeof path, "/sys/class/extcon/%s/state", e->d_name);
		if ((fd = open(path, O_RDONLY | O_CLOEXEC)) < 0) continue;
		got = read(fd, buf, sizeof buf - 1);
		close(fd);
		if (got <= 0) continue;
		buf[got] = '\0';
		if (strstr(buf, "USB-HOST=1")) host = true;
		if (strstr(buf, "SDP=1") || strstr(buf, "CDP=1")) data = true;
		if (strstr(buf, "DCP=1")) dcp = true;
	}
	closedir(d);
	if (host) return USB_PEER_NONE;
	if (data) return USB_PEER_COMPUTER;
	return dcp ? USB_PEER_CHARGER : USB_PEER_NONE;
}

/* The gadget plastron's S40usbgadget makes, bound to the controller by
 * writing its name into UDC, and let go by writing nothing. S40usbgadget
 * binds it at boot only in Developer Mode (usblink.h). */
#define GADGET_UDC "/sys/kernel/config/usb_gadget/px2/UDC"

static bool dhcpd_running(void);

/* usb0 up, with 10.42.0.1/30 and udhcpd, as S40usbgadget does at boot. The
 * kernel registers NCM's usb0 only when the gadget is first bound, so on a
 * release image, which binds nothing at boot, S40usbgadget's
 * "ip link set usb0 up", address and udhcpd all failed: no usb0 yet. Bound
 * later for File Transfer, usb0 came up down, with no address, so NCM never
 * told the computer it was connected (a Mac's en8 "inactive", the steps'
 * "no driver" hint). Only what is missing is done: an address already there
 * is left alone, never flushed. */
static void cable_netdev_up(void)
{
	char buf[32];
	unsigned long flags = 0;
	int fd;
	ssize_t got;

	if ((fd = open("/sys/class/net/usb0/flags", O_RDONLY | O_CLOEXEC)) < 0) return;
	got = read(fd, buf, sizeof buf - 1);
	close(fd);
	if (got > 0) { buf[got] = '\0'; flags = strtoul(buf, NULL, 16); }
	if (!(flags & 1)) {                        /* IFF_UP */
		char *argv[] = { "/sbin/ip", "link", "set", "usb0", "up", NULL };

		fprintf(stderr, "usb: usb0 was down, up (%d)\n", spawn_capture(argv, NULL, 0, 3));
	}
	if ((fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0)) >= 0) {
		struct ifreq ifr;
		bool has;

		memset(&ifr, 0, sizeof ifr);
		snprintf(ifr.ifr_name, sizeof ifr.ifr_name, "usb0");
		ifr.ifr_addr.sa_family = AF_INET;
		has = ioctl(fd, SIOCGIFADDR, &ifr) == 0;
		close(fd);
		if (!has) {
			char *argv[] = { "/sbin/ip", "addr", "add", "10.42.0.1/30", "brd", "+",
			                 "dev", "usb0", NULL };

			fprintf(stderr, "usb: usb0 had no address, 10.42.0.1/30 (%d)\n",
			        spawn_capture(argv, NULL, 0, 3));
		}
	}
	if (!dhcpd_running() && access("/etc/udhcpd.conf", R_OK) == 0) {
		char *argv[] = { "/usr/sbin/udhcpd", "/etc/udhcpd.conf", NULL };

		fprintf(stderr, "usb: udhcpd started for the cable (%d)\n", spawn_capture(argv, NULL, 0, 3));
	}
}

bool plat_usb_link(int op)
{
	char cur[64] = "";
	int fd = open(GADGET_UDC, O_RDONLY | O_CLOEXEC);

	if (fd < 0) return false;                  /* no gadget: S40 has not run */
	if (read(fd, cur, sizeof cur - 1) < 0) cur[0] = '\0';
	close(fd);
	if (op == 2) {
		plat_usb_link(0);
		return plat_usb_link(1);
	}
	if (op < 0 || (op == 1) == (cur[0] != '\0' && cur[0] != '\n'))
		return cur[0] != '\0' && cur[0] != '\n';
	if (op == 1) {
		DIR *d = opendir("/sys/class/udc");
		struct dirent *e;
		char udc[256] = "";

		if (!d) return false;
		while ((e = readdir(d)))
			if (e->d_name[0] != '.') { snprintf(udc, sizeof udc, "%s", e->d_name); break; }
		closedir(d);
		if (!udc[0] || (fd = open(GADGET_UDC, O_WRONLY | O_CLOEXEC)) < 0) return false;
		if (write(fd, udc, strlen(udc)) < 0) { close(fd); return false; }
		close(fd);
		fprintf(stderr, "usb: the link to the computer is on (%s)\n", udc);
		cable_netdev_up();
		return true;
	}
	if ((fd = open(GADGET_UDC, O_WRONLY | O_CLOEXEC)) < 0) return true;
	/* An empty line unbinds; a write of nothing at all is ignored */
	if (write(fd, "\n", 1) < 0) { close(fd); return true; }
	close(fd);
	fprintf(stderr, "usb: the link to the computer is off: charging only\n");
	return false;
}

/* File Transfer's steps over the cable (linksteps.h), from the device's own
 * files: the controller's state, usb0's carrier, which comes on when the
 * computer's network driver selects NCM's data interface (a Windows computer
 * with no driver for it takes the device, "configured", and never does), and
 * the neighbour table. udhcpd gives the computer 10.42.0.2 (/etc/udhcpd.conf;
 * the computer's side of the link has a fixed MAC address, so it is the same
 * one every plug), and a datagram to it makes the kernel ask for it by ARP:
 * a computer that took the address answers, whatever its firewall says. */
#define CABLE_PEER "10.42.0.2"

/* udhcpd, which S40usbgadget starts at boot, is the computer's only way to
 * an address: one that died leaves every computer on the cable without one
 * until a restart. Looked for by name in /proc. */
static bool dhcpd_running(void)
{
	DIR *d = opendir("/proc");
	struct dirent *e;
	bool found = false;

	if (!d) return true;                 /* cannot tell: leave it */
	while (!found && (e = readdir(d))) {
		char path[300], comm[32] = "";
		int fd;
		ssize_t got;

		if (e->d_name[0] < '1' || e->d_name[0] > '9') continue;
		snprintf(path, sizeof path, "/proc/%s/comm", e->d_name);
		if ((fd = open(path, O_RDONLY | O_CLOEXEC)) < 0) continue;
		got = read(fd, comm, sizeof comm - 1);
		close(fd);
		if (got > 0) comm[got] = '\0';
		found = !strncmp(comm, "udhcpd\n", 7);
	}
	closedir(d);
	return found;
}

static bool read_small(const char *path, char *buf, size_t n)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	ssize_t got;

	if (fd < 0) return false;
	got = read(fd, buf, n - 1);
	close(fd);
	if (got < 0) got = 0;
	buf[got] = '\0';
	return got > 0;
}

bool plat_cable_sense(link_sense *o)
{
	static unsigned asked_at, dhcpd_at, netdev_at;
	char buf[2048];
	DIR *d;
	struct dirent *e;
	unsigned now = plat_now_ms();

	memset(o, 0, sizeof *o);
	o->computer = plat_usb_peer() == USB_PEER_COMPUTER;
	/* A computer whose port took the sink side gives no power, so the PHY
	 * sees no computer: usbrole says something waits for power */
	if (!o->computer) {
		usb_power p;

		o->waits = plat_usb_power(&p) && usbpower_waits(&p);
	}
	if (o->computer && (d = opendir("/sys/class/udc"))) {
		while ((e = readdir(d))) {
			char path[300];

			if (e->d_name[0] == '.') continue;
			snprintf(path, sizeof path, "/sys/class/udc/%s/state", e->d_name);
			if (read_small(path, buf, sizeof buf) && link_udc_configured(buf)) o->configured = true;
		}
		closedir(d);
	}
	/* A gadget bound before this launcher (or by an older one) with usb0
	 * left down: brought up here too, every 2 s at most */
	if (o->configured && (!netdev_at || now - netdev_at >= 2000u)) {
		netdev_at = now ? now : 1;
		cable_netdev_up();
	}
	o->carrier = o->configured &&
	             read_small("/sys/class/net/usb0/carrier", buf, sizeof buf) && buf[0] == '1';
	if (!o->carrier) return true;
	o->address = read_small("/proc/net/arp", buf, sizeof buf) &&
	             link_arp_complete(buf, "usb0", NULL, 0);
	/* No address yet: udhcpd started again if it is gone, every 5 s at most */
	if (!o->address && (!dhcpd_at || now - dhcpd_at >= 5000u)) {
		dhcpd_at = now ? now : 1;
		if (!dhcpd_running() && access("/etc/udhcpd.conf", R_OK) == 0) {
			char *argv[] = { "/usr/sbin/udhcpd", "/etc/udhcpd.conf", NULL };

			fprintf(stderr, "usb: udhcpd was not running, started again (%d)\n",
			        spawn_capture(argv, NULL, 0, 3));
		}
	}
	if (!o->address && (!asked_at || now - asked_at >= 1000u)) {
		int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);

		asked_at = now ? now : 1;
		if (fd >= 0) {
			struct sockaddr_in to;

			memset(&to, 0, sizeof to);
			to.sin_family = AF_INET;
			to.sin_port = htons(9);                /* discard */
			inet_pton(AF_INET, CABLE_PEER, &to.sin_addr);
			setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, "usb0", 5);
			if (sendto(fd, "", 0, MSG_DONTWAIT, (struct sockaddr *)&to, sizeof to) < 0) { }
			close(fd);
		}
	}
	return true;
}

/* plastron's usbrole: its state, USB Accessory and the setting (usbpower.h) */
#define USBROLE_RUN "/run/usbrole"

bool plat_usb_power(usb_power *out) { return usbpower_read(USBROLE_RUN, out); }

bool plat_usb_power_grant(unsigned plug)
{
	bool ok = usbpower_request(USBROLE_RUN, plug);

	fprintf(stderr, "usb: power the device on the port (plug %u)%s\n", plug, ok ? "" : ": not written");
	return ok;
}

bool plat_usb_power_mode(bool automatic)
{
	bool ok = usbpower_mode(USBROLE_RUN, automatic);

	fprintf(stderr, "usb: accessories %s%s\n", automatic ? "powered automatically" : "asked about",
	        ok ? "" : ": not written");
	return ok;
}

/* The USB controller lets go of the cable and takes it again, so the computer
 * sees a new device plugged in. Seen 2026-10-08: the Mac dropped the Pixel as
 * if unplugged while the controller here went on saying "configured" and never
 * offered itself again; a restart was the only way back. A third of a second
 * apart, long enough for the computer to notice it went. */
void plat_cable_reconnect(void)
{
	static const struct timespec gap = { 0, 300 * 1000000L };
	DIR *d = opendir("/sys/class/udc");
	struct dirent *e;

	if (!d) return;
	while ((e = readdir(d))) {
		char path[300];
		int fd;

		if (e->d_name[0] == '.') continue;
		snprintf(path, sizeof path, "/sys/class/udc/%s/soft_connect", e->d_name);
		if ((fd = open(path, O_WRONLY | O_CLOEXEC)) < 0) continue;
		if (write(fd, "disconnect", 10) < 0) { /* left as it was */ }
		close(fd);
		nanosleep(&gap, NULL);
		if ((fd = open(path, O_WRONLY | O_CLOEXEC)) < 0) continue;
		if (write(fd, "connect", 7) < 0) { /* the next screen tries again */ }
		close(fd);
		fprintf(stderr, "wire: the cable connection made fresh\n");
		break;
	}
	closedir(d);
}

/* No mute switch on this device. */
bool plat_mute_poll(bool own_volume) { (void)own_volume; return false; }
bool plat_muted(void) { return false; }

void device_levels_forget(void) { jack_was = -1; }

int plat_volume_get(void)     { return cur_vol; }
int plat_brightness_get(void) { return cur_bright; }

/* A press of a level key: the bar on the screen, and the level on the side
 * lights, at an end of the scale too, as the bar is */
static void say_level(bool volume, int val, int max)
{
	char line[48];

	side_lights_level_line(line, sizeof line, volume, val, max);
	plat_side_lights_say(line);
}

void plat_volume_nudge(int delta)
{
	if (mixer_fd < 0) return;
	apply_volume(clampi((cur_vol < 0 ? 0 : cur_vol) + delta, 0, VOL_MAX));
	levels_save();
	plat_osd_show(2, cur_vol, VOL_MAX);
	say_level(true, cur_vol, VOL_MAX);
}

void plat_brightness_nudge(int delta)
{
	apply_brightness(clampi(cur_bright + delta, 0, BRIGHT_MAX));
	levels_save();
	plat_osd_show(1, cur_bright, BRIGHT_MAX);
	say_level(false, cur_bright, BRIGHT_MAX);
}

void plat_volume_set_pct(int pct)
{
	if (mixer_fd < 0) return;
	apply_volume(clampi((pct * VOL_MAX + 50) / 100, 0, VOL_MAX));
	levels_save();
}

void plat_brightness_set(int level)
{
	apply_brightness(clampi(level, 0, BRIGHT_MAX));
	levels_save();
}

/* No radio on the board. Wi-Fi is a USB adapter, there while one is plugged
 * in: plastron loads its driver then (usbrole), and wlan0 is the sign. Asked
 * as menus are built, and one access() is all it costs. No Bluetooth. */
bool plat_has_wifi(void)      { return access("/sys/class/net/wlan0", F_OK) == 0; }
bool plat_has_bluetooth(void) { return false; }
/* One speaker: Audio Channels' Auto plays it mono (src/audioout.h) */
bool plat_speaker_mono(void) { return true; }

/* FUNCTION with volume up or down (key_event). */
bool plat_brightness_on_volume(void) { return true; }
bool plat_menu_held(void) { return fn_down; }
bool plat_tap_held(in_button b) { return b == IN_L2 ? l2_down : b == IN_R2 ? r2_down : false; }

/* The side lights, and L2+R2 asking gauge for the level (key_event). */
bool plat_battery_lights(void) { return true; }

/* MENU > Side Lights: one line on the card, in the file plastron's gauge reads
 * at boot and again on SIGHUP (package/gauge/src/gauge.c; the line's words,
 * src/sidelights.h). Replaced whole (src/atomic.h), so the gauge never reads
 * half a line; a missing file is every part on both sides. */
#define SIDE_LIGHTS_FILE "/mnt/SDCARD/.userdata/pixel2/side-lights"

side_lights plat_side_lights(void)
{
	char text[128] = "";
	int fd = open(SIDE_LIGHTS_FILE, O_RDONLY | O_CLOEXEC);
	ssize_t n;

	if (fd < 0) return LIGHTS_ALL;
	n = read(fd, text, sizeof text - 1);
	close(fd);
	text[n > 0 ? n : 0] = '\0';
	return side_lights_parse(text);
}

void plat_side_lights_set(side_lights on)
{
	char line[96];
	FILE *f = atomic_open(SIDE_LIGHTS_FILE, 0644);

	if (!f) return;
	side_lights_line(on, line, sizeof line);
	fprintf(f, "%s\n", line);
	if (atomic_commit(f, SIDE_LIGHTS_FILE)) signal_gauge(SIGHUP);
}

/* The gauge's FIFO, which it makes at its start (src/sidelights.h) */
void plat_side_lights_say(const char *line) { side_lights_say_to(SIDE_LIGHTS_FIFO, line); }

/* The DDR's ceiling, which under plastron's performance governor is its rate:
 * 328 MHz, the slower of its two working rates, or 0, no ceiling (1056 MHz).
 * The same file Diatom sets for a game's power profile and gives back at 0
 * when the game ends. 194 MHz saved only 8 mW more than 328 on an idle shelf,
 * and a switch takes about 1.5 ms (2026-11-04). */
void plat_ram_idle(bool idle)
{
	write_file("/sys/class/devfreq/dmc/max_freq", idle ? "328000000" : "0");
}

/* ---- the chip's temperature ---- */

/* The RK3326's two sensors as the kernel names their zones (px30.dtsi):
 * soc-thermal, the CPU's, and gpu-thermal. Both slow their part at 70 and
 * 85 C and power the handheld off at 115 C (critical, an orderly power-off:
 * see src/heat.h). Found once by type, so a kernel that numbers its zones
 * differently is still read right; a zone not there is left out. */
#define FAKE_TEMP "/tmp/zyperos_fake_temp"

bool plat_chip_temp(int *mc)
{
	static char zone[2][48];
	static int  nzones = -1;
	int v = read_int(FAKE_TEMP), best = -1, i;

	if (v >= 0) {
		*mc = v < 1000 ? v * 1000 : v;
		return true;
	}
	if (nzones < 0) {
		nzones = 0;
		for (i = 0; i < 8 && nzones < 2; i++) {
			char path[64], type[32] = "";
			int fd;
			ssize_t n;

			snprintf(path, sizeof path, "/sys/class/thermal/thermal_zone%d/type", i);
			if ((fd = open(path, O_RDONLY | O_CLOEXEC)) < 0) continue;
			n = read(fd, type, sizeof type - 1);
			close(fd);
			type[n > 0 ? n : 0] = '\0';
			if (strncmp(type, "soc-thermal", 11) && strncmp(type, "gpu-thermal", 11)) continue;
			snprintf(zone[nzones++], sizeof zone[0],
			         "/sys/class/thermal/thermal_zone%d/temp", i);
		}
	}
	for (i = 0; i < nzones; i++)
		if ((v = read_int(zone[i])) > best) best = v;
	if (best < 0) return false;
	*mc = best;
	return true;
}

/* ---- charging ---- */

/* What a charger is, from the USB PHY's BC1.2 answer, which plastron's DT
 * hands the charger as an extcon (leds-charging, 2026-11-05): DCP a wall
 * charger, SDP or CDP a computer. rk817-charger's own usb_type is no help: it
 * says DCP whatever is plugged in. */
static ch_kind charge_kind(void)
{
	char path[64], buf[256];
	int i, fd;
	ssize_t n;

	for (i = 0; i < 4; i++) {
		snprintf(path, sizeof path, "/sys/class/extcon/extcon%d/state", i);
		if ((fd = open(path, O_RDONLY | O_CLOEXEC)) < 0) continue;
		n = read(fd, buf, sizeof buf - 1);
		close(fd);
		buf[n > 0 ? n : 0] = '\0';
		if (strstr(buf, "DCP=1")) return CH_WALL;
		if (strstr(buf, "SDP=1") || strstr(buf, "CDP=1")) return CH_COMPUTER;
	}
	return CH_UNKNOWN;
}

bool plat_charge_read(ch_reading *r)
{
	char line[64] = "", st[16] = "";
	int fd = open("/tmp/zyperos_fake_charge", O_RDONLY | O_CLOEXEC), v;
	ssize_t n;

	if (fd >= 0) {
		n = read(fd, line, sizeof line - 1);
		close(fd);
		line[n > 0 ? n : 0] = '\0';
		if (ch_fake_parse(line, r)) return true;
	}
	memset(r, 0, sizeof *r);
	if ((v = read_int("/sys/class/power_supply/battery/capacity")) < 0) return false;
	r->pct = v;
	/* Into the battery positive: rk817_charger says Discharging below 0 */
	v = read_int("/sys/class/power_supply/battery/current_avg");
	/* -1 is read_int's no answer, and a current of -1 uA is none either */
	r->ma_known = v != -1;
	r->ma = r->ma_known ? v / 1000 : 0;
	r->mv = read_int("/sys/class/power_supply/battery/voltage_avg") / 1000;
	v = read_int("/sys/class/power_supply/battery/charge_now");
	r->now_mah = v >= 0 ? v / 1000 : -1;
	v = read_int("/sys/class/power_supply/battery/charge_full");
	r->full_mah = v > 0 ? v / 1000 : -1;
	r->online = read_int("/sys/class/power_supply/rk817-charger/online") == 1;
	if ((fd = open("/sys/class/power_supply/battery/status", O_RDONLY | O_CLOEXEC)) >= 0) {
		n = read(fd, st, sizeof st - 1);
		close(fd);
		st[n > 0 ? n : 0] = '\0';
		r->full = !strncmp(st, "Full", 4);
		/* plastron's rk817 says so while the charge limit holds it */
		r->held = r->online && !strncmp(st, "Not charging", 12);
	}
	r->kind = r->online ? charge_kind() : CH_UNKNOWN;
	return true;
}

/* MENU > Charge Limit: plastron's rk817 driver's end threshold, where it
 * stops charging, and its start threshold, CHARGE_LIMIT_GAP under it, where
 * it charges again: 80% holds between 78 and 80%. Or the fake gauge's file.
 * The driver puts the start 2 under the end itself (RK817_CHARGE_LIMIT_GAP
 * in plastron's 9-px2-rk817-charge-limit.patch says why 2 is safe for the
 * battery), but a system from before that puts it 5 under, 75 to 80%, so
 * the launcher writes the start too. plastron's `make charge-limit-check`
 * holds the two numbers equal. */
#define CHARGE_LIMIT      "/sys/class/power_supply/battery/charge_control_end_threshold"
#define CHARGE_START      "/sys/class/power_supply/battery/charge_control_start_threshold"
#define CHARGE_LIMIT_GAP  2
#define CHARGE_LIMIT_FAKE "/tmp/zyperos_fake_charge_limit"

bool plat_charge_limit_supported(void)
{
	return access(CHARGE_LIMIT, W_OK) == 0 || access("/tmp/zyperos_fake_charge", F_OK) == 0;
}

void plat_charge_limit_set(int pct)
{
	char v[8];

	if (pct < 50 || pct > 100) pct = 100;
	snprintf(v, sizeof v, "%d\n", pct);
	if (access(CHARGE_LIMIT, W_OK) == 0) {
		write_file(CHARGE_LIMIT, v);
		/* After the end: the driver checks the start against it, and
		 * writing the end moves the start too. 100 is no limit, with no
		 * start to keep. */
		if (pct < 100) {
			snprintf(v, sizeof v, "%d\n", pct - CHARGE_LIMIT_GAP);
			write_file(CHARGE_START, v);
		}
	} else if (access("/tmp/zyperos_fake_charge", F_OK) == 0) {
		/* write_file is for sysfs, which has the file already */
		FILE *f = fopen(CHARGE_LIMIT_FAKE, "w");

		if (f) {
			fputs(v, f);
			fclose(f);
		}
	}
}

/* ---- battery ---- */

/* A level written to /tmp/zyperos_fake_batt stands in for the gauge, off the
 * charger, for testing the low and empty battery: written from a shell, so it
 * can change while a game runs, and gone at power-off. */
const char *plat_battery_info_path(void)
{
	return "/sys/class/power_supply/battery";
}

bool plat_battery(int *pct, bool *charging)
{
	int fake = read_int("/tmp/zyperos_fake_batt");
	char st[32] = { 0 };
	FILE *s;
	int v;

	if (charging) *charging = false;
	if (fake >= 0) {
		if (pct) *pct = fake;
		return true;
	}
	if (pct) {
		if ((v = read_int("/sys/class/power_supply/battery/capacity")) < 0) return false;
		*pct = v;
	}
	if (charging) {
		/* The cable, not the battery's status: rk817_charger says
		 * Discharging whenever the battery's current is below zero, cable
		 * in or not, and a full battery on the cable sits a few mA below
		 * zero for hours. That read as off the charger, and Auto Off
		 * powered a plugged-in device down, twice on 2026-11-04. */
		v = read_int("/sys/class/power_supply/rk817-charger/online");
		if (v == 0 || v == 1) {
			*charging = v == 1;
			return true;
		}
		/* Only fall back when online is unavailable. An explicit zero
		 * wins over a status value that still says Charging or Full. */
		if ((s = fopen("/sys/class/power_supply/battery/status", "r"))) {
			bool read = fgets(st, sizeof st, s) != NULL;
			if (read)
				*charging = strncmp(st, "Charging", 8) == 0 || strncmp(st, "Full", 4) == 0;
			fclose(s);
			if (read) return true;
		}
	}
	return pct != NULL;
}
