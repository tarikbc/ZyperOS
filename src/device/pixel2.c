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
#include "charging_rule.h"
#include "audioout.h"
#include "clamp.h"
#include "db.h"
#include "device.h"
#include "displayback.h"
#include "fileio.h"
#include "nightkeep.h"
#include "levels.h"
#include "panelgamma.h"
#include "phonelink.h"
#include "platform.h"
#include "portmenu.h"
#include "screenfx_draw.h"
#include "spawn.h"
#include "stickpad.h"
#include "thermal.h"

#include <SDL_syswm.h>

/* What tells this file's RK3326 handhelds apart, for the ones that build it
 * with their own (src/device/r36s.c): the card's folder, the codes the A and
 * B caps and the MENU key send, and whether L2 and R2 are taps for the side
 * lights' battery chord. The Pixel 2's here. */
#ifndef RK_ID
#define RK_ID            "pixel2"
#define RK_CODE_A        BTN_SOUTH
#define RK_CODE_B        BTN_EAST
#define RK_CODE_FUNCTION BTN_TRIGGER_HAPPY1
#define RK_L2R2_TAPS     1
#endif
/* Whether the handheld has sticks (the R36S's: the left one moves as the
 * d-pad, their clicks are L3 and R3). None on the Pixel 2, whose pad is read
 * as it always was. */
#ifndef RK_STICKS
#define RK_STICKS        0
#endif

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <linux/input.h>
#include <signal.h>
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

const char *P_ROOT = "/mnt/SDCARD/ZyperOS";
const char *P_CARD = "/mnt/SDCARD";
const char *P_ROMS = "/mnt/SDCARD/Roms";
const char *P_USERDATA = "/mnt/SDCARD/.userdata/" RK_ID;
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
/* The panel's CRTC as SDL found it, for lighting it again after a game that
 * crashed (src/displayback.h); and whether the display is still not ours
 * after one, with when that began */
static displayback_panel px_panel;
static bool          display_lost;
static Uint32        display_lost_at;

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
/* The port menu (src/main.c, port_menu_main): over a port still starting,
 * the loading screen splash --still holds must outlive the menu, which
 * portkeys puts back for Keep Waiting (tools/portkeys.c, menu_waiting).
 * Only the boot animation is a splash to stop and end. */
extern bool g_port_menu;

/* The display's gamma table as the backlight's value asks, while this
 * process holds the display (below, after the levels) */
static bool display_held;
static void display_gamma(const char *when);
/* The first frame is up and the splash ended (plat_present): a video init
 * after it is the launcher's own again, after a game in its own process */
static bool video_shown;

static void take_from_splash(void)
{
	Uint32 t0 = SDL_GetTicks();

	if (!g_port_menu) signal_splash(SIGUSR1);
	while (drm_fd >= 0 && ioctl(drm_fd, PX_DRM_IOCTL_SET_MASTER, 0) != 0 &&
	       SDL_GetTicks() - t0 < 300)
		SDL_Delay(1);
	fprintf(stderr, "video: took the display from the splash in %u ms\n",
	        (unsigned)(SDL_GetTicks() - t0));
	/* Before the first frame: the gamma table as the backlight's value asks
	 * (panelgamma.h) - the dimming one at the two rungs under the floor,
	 * else none, which also empties the table a splash from a system image
	 * up to 1.0.4 filled. The value is the player's, set by the splash from
	 * zybright or by plat_settings_init. With the table already right, a
	 * few ioctls; else a refresh's wait while the controller loads it. */
	display_held = true;
	display_gamma("taking the display from the splash");
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

/* The display back after a game: master, asked for for up to max_ms, then a
 * picture on the panel's CRTC for the next flip to replace. A game that
 * crashed holds master until the kernel closes its DRM file, and takes its
 * picture off the panel as that closes (src/displayback.h). Says what it
 * did; false while master is still someone else's, *err the refusal. */
static bool take_back(unsigned max_ms, int *err)
{
	int waited = displayback_master(drm_fd, max_ms, err), lit;

	if (waited < 0) return false;
	if (waited > 0)
		fprintf(stderr, "video: the display was free %d ms after the game; took it back\n",
		        waited);
	/* Diatom set the table for its own steps; this is the backlight's value
	 * now, which a game that crashed mid-step may have left untabled */
	display_held = true;
	display_gamma("taking the display back");
	/* With no panel found (said once, at the start), nothing to light */
	lit = px_panel.crtc ? displayback_relight(drm_fd, &px_panel) : 0;
	if (lit > 0)
		fprintf(stderr, "video: the game took its picture off the panel; "
		        "put black up for the next frame to replace\n");
	else if (lit < 0)
		fprintf(stderr, "video: could not put a picture back on the panel: %s\n",
		        strerror(errno));
	return true;
}

/* A video under this frame: plat_video_under. One frame's worth, cleared by
 * the present that shows it. */
static struct {
	bool         on;
	SDL_Texture *tex;
	SDL_Rect     src;                  /* the part of tex to draw; w 0 for all of it */
	SDL_Rect     dst;                  /* in screen coordinates */
	SDL_Color    bg;
} under;

void plat_video_under(SDL_Texture *tex, const SDL_Rect *src, const SDL_Rect *dst, SDL_Color bg)
{
	SDL_BlendMode mode;

	under.on = true;
	under.tex = tex;
	under.src = src ? *src : (SDL_Rect){ 0, 0, 0, 0 };
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
	if (under.tex)
		SDL_RenderCopyExF(ren, under.tex, under.src.w > 0 ? &under.src : NULL, &r, panel_turn,
		                  NULL, SDL_FLIP_NONE);
}

/* screen_tex at dest, 1024x768 down to 640x480 and turned onto the panel,
 * through the screens' shader when one is on (src/screenfx.h): CRT Curved's
 * bend in place of the plain copy, then the mask over it at the panel's own
 * pixels, one more pass. Off is the plain copy alone, as it always was. */
static void draw_picture(const SDL_Rect *dest, bool fx)
{
	fx = fx && sfx_on();
	if (fx && sfx_curved()) sfx_draw_curved(ren, screen_tex, dest, panel_turn);
	else SDL_RenderCopyEx(ren, screen_tex, NULL, dest, panel_turn, NULL, SDL_FLIP_NONE);
	if (fx) sfx_draw_mask(ren, dest->w, dest->h, dest, panel_turn);
}

/* The shader goes on at the panel, in show_screen: a frame read back before
 * it (a shot) is read from screen_tex, which has none */
void plat_screen_finish(void) { }
bool plat_screen_shader_in_frame(void) { return false; }

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
		/* A video plays as it is, at the panel's pixels: no shader */
		draw_picture(&dest, false);
		/* ...and back to opaque, as it was made (below), for every frame
		 * without a video. It was left blending: after the first MP4 video
		 * every frame went over the black with its alpha, and whatever the
		 * screen held at alpha 0 - an RGB texture's pixels, which this SDL
		 * writes so, the Shader list's picture and the paused game's - came
		 * out black until the launcher started again (2026-11-19, on the
		 * 1.1.0 test build: "Shaders preview box is not working. It's
		 * rendering black", after MP4's videos) */
		SDL_SetTextureBlendMode(screen_tex, SDL_BLENDMODE_NONE);
		under.on = false;
	} else {
		SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
		SDL_RenderClear(ren);
		draw_picture(&dest, true);
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
 * had put the logo back up at power-off.
 *
 * With the display not ours after a game (device_display_take gave up on its
 * bound), master is asked for once a frame, and the frame is held back until
 * it comes: SDL's flip would only be refused (EACCES), once a frame, in the
 * log. The frame stays in screen_tex and goes up the moment master does. */
void plat_present(void)
{
	Uint8 r, g, b, a;

	SDL_GetRenderDrawColor(ren, &r, &g, &b, &a);
	plat_overlay_present(ren);
	if (!video_shown) {
		take_from_splash();
		show_screen();
		show_screen();
		if (!g_port_menu) signal_splash(SIGTERM);
		video_shown = true;
	} else if (display_lost) {
		int err = 0;

		if (take_back(0, &err)) {
			display_lost = false;
			fprintf(stderr, "video: took the display back at a frame, %u ms after the game\n",
			        (unsigned)(SDL_GetTicks() - display_lost_at));
			show_screen();
		}
	} else {
		show_screen();
	}
	SDL_SetRenderDrawColor(ren, r, g, b, a);
}

void plat_draw_to_screen(void) { SDL_SetRenderTarget(ren, screen_tex); }

/* The picture as plat_save_screen writes it, drawn in a texture of the
 * panel's size and read back from there: the panel itself is not touched,
 * so pages made only to be saved (Flash Drive's, which usbdrive shows
 * later) never flash past (2026-11-11: three pages and the atlas went over
 * the panel before "One moment"). */
bool plat_save_screen_hidden(const char *path)
{
	SDL_Rect dest = { (panel_w - 640) / 2, (panel_h - 480) / 2, 640, 480 };
	size_t n = (size_t)panel_w * (size_t)panel_h * 4;
	SDL_Texture *t = SDL_CreateTexture(ren, PLAT_TARGET_FORMAT, SDL_TEXTUREACCESS_TARGET, panel_w, panel_h);
	void *px = malloc(n);
	FILE *f;
	bool ok = false;

	if (t && px && SDL_SetRenderTarget(ren, t) == 0) {
		SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
		SDL_RenderClear(ren);
		draw_picture(&dest, true);
		if (SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_XRGB8888, px, panel_w * 4) == 0 &&
		    (f = fopen(path, "wb"))) {
			ok = fwrite(px, 1, n, f) == n;
			if (fclose(f) != 0) ok = false;
		}
	}
	free(px);
	SDL_SetRenderTarget(ren, screen_tex);
	if (t) SDL_DestroyTexture(t);
	return ok;
}

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
	draw_picture(&dest, true);
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
	screen_tex = SDL_CreateTexture(ren, PLAT_TARGET_FORMAT, SDL_TEXTUREACCESS_TARGET,
	                               ZYPEROS_SCREEN_W, ZYPEROS_SCREEN_H);
	if (!screen_tex) {
		fprintf(stderr, "screen texture: %s\n", SDL_GetError());
		return false;
	}
	/* Onto the panel opaque, whatever its alpha says (show_screen); a
	 * video's frame alone goes over its picture blended */
	SDL_SetTextureBlendMode(screen_tex, SDL_BLENDMODE_NONE);
	SDL_SetRenderTarget(ren, screen_tex);
	SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

	SDL_VERSION(&info.version);
	if (SDL_GetWindowWMInfo(win, &info) && info.subsystem == SDL_SYSWM_KMSDRM)
		drm_fd = info.info.kmsdrm.drm_fd;
	else
		fprintf(stderr, "video: no DRM handle; games cannot take the display\n");
	/* The panel's CRTC, while the splash still shows on it: the one SDL
	 * picked, for lighting it again after a game that crashed */
	if (drm_fd >= 0 && displayback_find(drm_fd, &px_panel) != 0)
		fprintf(stderr, "video: found no connected panel; after a game that crashes "
		        "the screen may stay dark\n");
	/* Again after a game in its own process (src/launch_screen.c's fallback,
	 * when the resident refused the game or died in it): no splash holds the
	 * display now, so it is this process's at once, with the table the
	 * backlight's value asks for - which that game's Diatom may have left
	 * at another rung's. Without this the launcher's steps set no table
	 * until the next game in the resident. */
	if (video_shown && drm_fd >= 0 && ioctl(drm_fd, PX_DRM_IOCTL_SET_MASTER, 0) == 0) {
		display_held = true;
		display_gamma("taking the display back after a game in its own process");
	}

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
	sfx_forget();
	if (ren) { SDL_DestroyRenderer(ren); ren = NULL; }
	if (win) { SDL_DestroyWindow(win); win = NULL; }
	drm_fd = -1;
	display_held = false;
	/* The black picture was made on SDL's DRM file, which closes here and
	 * takes it with it */
	memset(&px_panel, 0, sizeof px_panel);
	display_lost = false;
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

/* Diatom is about to present: it cannot while this process holds master. */
/* Diatom draws on the panel itself: the launcher waits as long as it likes */
unsigned device_game_wait(unsigned ms) { return ms; }

void device_display_release(void)
{
	if (drm_fd >= 0) ioctl(drm_fd, PX_DRM_IOCTL_DROP_MASTER, 0);
	display_held = false;
}

/* Diatom has given the display back (it drops master before PAUSED and EXIT),
 * and the next plat_present flips onto the CRTC Diatom left lit. Or the game
 * crashed: Diatom said EXIT from its signal handler and holds master until it
 * has died, and its picture goes off the panel as it does (src/displayback.h).
 * Asked for for a second, which a dying process needs only a few ms of; past
 * that, at each frame (plat_present).
 *
 * Asked for only once, as it first was, a crash's refusal stood: every flip
 * after it was refused, and the shelf never reached the panel until the
 * launcher was started again (LocoRoco in PPSSPP, 2026-11-13). */
#define TAKE_BACK_MS 1000

void device_display_take(void)
{
	Uint32 t0 = SDL_GetTicks();
	int err = 0;

	if (drm_fd < 0) return;
	display_lost = !take_back(TAKE_BACK_MS, &err);
	if (display_lost) {
		display_lost_at = t0;
		fprintf(stderr, "video: could not take the display back in %d ms (%s); "
		        "trying again at each frame\n", TAKE_BACK_MS, strerror(err));
	}
}

/* ---- input ----------------------------------------------------------------- */

#define BITS_PER_LONG   (8 * (int)sizeof(long))
#define NLONGS(max)     (((max) + BITS_PER_LONG) / BITS_PER_LONG)
#define BIT_IS_SET(a,b) (((a)[(b) / BITS_PER_LONG] >> ((b) % BITS_PER_LONG)) & 1UL)

static int fd_pad = -1;     /* "gkd_pixel2_joypad": the face, d-pad, shoulders, FUNCTION */
static int pad_node = -1;   /* its N in /dev/input/eventN, for the motor (plat_rumble_buzz) */
static int fd_keys = -1;    /* "gpio-keys": volume */
static int fd_power = -1;   /* "rk805 pwrkey" */
static int jack_fd = -1;    /* "rk817_int Headphones": the jack switch, for the settings */
static int dbg_input;
static bool l2_down, r2_down;  /* the two together ask for the battery level (below) */
static bool l2r2_chorded;      /* ...and have, since both were last up */
static bool l2_tap, r2_tap;    /* a tap reported last poll ends this one */

/* The left stick as the d-pad on the launcher's screens (src/stickpad.h),
 * where the pad has one: the R36S's, as ABS_X and ABS_Y with their range and
 * their flat, the dead zone its device tree gives. A direction is held while
 * its d-pad key or the stick holds it, so it repeats as the d-pad's does.
 * The right stick moves nothing here (L1 and R1 already page, and a thumb
 * resting on it under A B X Y would move the cursor). The Pixel 2 has no
 * stick (RK_STICKS 0): stick_on stays false and nothing changes there. */
_Static_assert((int)IN_LEFT == (int)STICK_LEFT && (int)IN_RIGHT == (int)STICK_RIGHT &&
               (int)IN_UP == (int)STICK_UP && (int)IN_DOWN == (int)STICK_DOWN,
               "stickpad's directions are the d-pad's buttons");
static stickpad stick;
static bool     stick_on;
static struct { int value, min, max, flat; } stick_axis[2];
static bool     dpad_key[STICK_DIRS];

/* Kernel code -> ZyperOS button, by what is printed on the key. Measured
 * 2026-10-01 pressing each in a known order: the A, B, X and Y caps send
 * SOUTH, EAST, NORTH and WEST - named for their labels, not for where they
 * sit, since A is on the right and B at the bottom. FUNCTION is MENU, and L2
 * and R2 are the battery level together and a tap each alone, all three
 * handled apart below. */
static const struct { int code; in_button b; } keymap[] = {
	{ BTN_DPAD_UP, IN_UP },       { BTN_DPAD_DOWN, IN_DOWN },
	{ BTN_DPAD_LEFT, IN_LEFT },   { BTN_DPAD_RIGHT, IN_RIGHT },
	{ RK_CODE_A, IN_ACCEPT },     { RK_CODE_B, IN_BACK },
	{ BTN_NORTH, IN_X },          { BTN_WEST, IN_Y },
	{ BTN_TL, IN_L1 },            { BTN_TR, IN_R1 },
#if RK_STICKS
	{ BTN_THUMBL, IN_L3 },        { BTN_THUMBR, IN_R3 },
#endif
	{ BTN_SELECT, IN_SELECT },    { BTN_START, IN_START },
};
#define CODE_FUNCTION RK_CODE_FUNCTION

/* Whether the pad has a left stick, and its two axes' range and flat */
static void stick_probe(void)
{
	unsigned long abs[NLONGS(ABS_MAX)];
	struct input_absinfo ai;
	static const int code[2] = { ABS_X, ABS_Y };
	int i;

	stick_on = false;
	memset(abs, 0, sizeof abs);
	if (!RK_STICKS || fd_pad < 0 || ioctl(fd_pad, EVIOCGBIT(EV_ABS, sizeof abs), abs) < 0) return;
	for (i = 0; i < 2; i++) {
		if (!BIT_IS_SET(abs, code[i]) || ioctl(fd_pad, EVIOCGABS(code[i]), &ai) < 0) return;
		stick_axis[i].value = ai.value;
		stick_axis[i].min = ai.minimum;
		stick_axis[i].max = ai.maximum;
		stick_axis[i].flat = ai.flat;
	}
	stick_on = true;
	fprintf(stderr, "input: the left stick moves as the d-pad (X %d..%d, Y %d..%d, flat %d)\n",
	        stick_axis[0].min, stick_axis[0].max, stick_axis[1].min, stick_axis[1].max, stick_axis[0].flat);
}

/* The stick where the last report left it: each direction it holds or lets
 * go, with the d-pad's key for that direction */
static void stick_apply(in_state *st)
{
	bool was[STICK_DIRS];
	int rx, ry, x, y, d;

	if (!stick_on) return;
	memcpy(was, stick.held, sizeof was);
	x = stickpad_centred(stick_axis[0].value, stick_axis[0].min, stick_axis[0].max, stick_axis[0].flat, &rx);
	y = stickpad_centred(stick_axis[1].value, stick_axis[1].min, stick_axis[1].max, stick_axis[1].flat, &ry);
	/* The two axes on one scale, the smaller range's */
	if (rx != ry && rx > 0 && ry > 0) {
		if (rx > ry) x = (int)((long)x * ry / rx);
		else y = (int)((long)y * rx / ry);
	}
	stickpad_update(&stick, x, y, rx < ry ? rx : ry);
	for (d = 0; d < STICK_DIRS; d++)
		if (stick.held[d] != was[d])
			plat_set_btn(st, (in_button)d, stick.held[d] || dpad_key[d]);
}

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
		/* Not the pad portkeys gives your PICO-8 while it holds the
		 * joypad (tools/pkvpad.h, "ZyperOS pad for PICO-8"), which is
		 * there while the in-game menu over PICO-8 runs: its devices are
		 * named so, and the joypad is the menu's then */
		if (BIT_IS_SET(keys, BTN_SOUTH)) {
			char name[64] = "";

			ioctl(fd, EVIOCGNAME(sizeof name), name);
			if (!strncmp(name, "ZyperOS ", 8)) keys[BTN_SOUTH / BITS_PER_LONG] &= ~(1UL << (BTN_SOUTH % BITS_PER_LONG));
		}
		if      (fd_pad < 0   && BIT_IS_SET(keys, BTN_SOUTH))           { fd_pad = fd; pad_node = i; stick_probe(); }
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

/* ---------- the motor: a step's buzz -------------------------------------- */

/* The joypad again, opened to write: the motor is its force feedback (PWM0,
 * the joypad driver's; diatom/port/rumble_evdev.c says how). Opened at the
 * first buzz and kept, so a step costs one upload and one write, never an
 * open or a close (a close waits out an RCU grace period, input_scan). Our
 * effect is ours alone: Diatom's, on its own descriptor, plays beside it,
 * held still while its game is paused. */
static int rumble_fd = -1;
static int rumble_id = -1;

bool plat_rumble_buzz(float scale, int ms)
{
	struct ff_effect e;
	struct input_event ev;
	uint16_t v;
	char path[32];

	if (scale <= 0.0f || ms <= 0) return true;      /* Off: nothing to feel */
	if (rumble_fd < 0) {
		if (pad_node < 0) return false;
		snprintf(path, sizeof path, "/dev/input/event%d", pad_node);
		if ((rumble_fd = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC)) < 0) {
			fprintf(stderr, "rumble: %s: %s\n", path, strerror(errno));
			return false;
		}
	}
	v = scale >= 1.0f ? 0xFFFF : (uint16_t)((float)0xFFFF * scale + 0.5f);
	memset(&e, 0, sizeof e);
	e.type = FF_RUMBLE;
	e.id = (int16_t)rumble_id;
	/* The driver runs the motor from the strong one; both, as SDL sends */
	e.u.rumble.strong_magnitude = v;
	e.u.rumble.weak_magnitude = v;
	/* ff-memless ends it after this long and tells the driver 0: a buzz
	 * stops itself, whatever the launcher does next */
	e.replay.length = (uint16_t)(ms > 2000 ? 2000 : ms);
	if (ioctl(rumble_fd, EVIOCSFF, &e) < 0) {
		fprintf(stderr, "rumble: upload: %s\n", strerror(errno));
		rumble_id = -1;
		return false;
	}
	rumble_id = e.id;
	memset(&ev, 0, sizeof ev);
	ev.type = EV_FF;
	ev.code = (uint16_t)rumble_id;
	ev.value = 1;
	return write(rumble_fd, &ev, sizeof ev) == (ssize_t)sizeof ev;
}

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
 * keys, which is the Brick's pocket lock, so MP4's lock works unchanged. */
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
		/* A press or a hold, as MENU > Power Button says (src/powerhold.h);
		 * a repeat, which this key does not send, would be no let-go */
		if (ev->value != 2) plat_set_btn(st, IN_POWER, ev->value == 1);
		return;
	}
	/* L2 and R2 as plain keys, on a handheld with no battery lights */
	if (!RK_L2R2_TAPS && (ev->code == BTN_TL2 || ev->code == BTN_TR2)) {
		plat_set_btn(st, ev->code == BTN_TL2 ? IN_L2 : IN_R2, ev->value == 1);
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
			const in_button b = keymap[i].b;

			/* A d-pad key: held while it or the stick holds it */
			if (b == IN_LEFT || b == IN_RIGHT || b == IN_UP || b == IN_DOWN) {
				dpad_key[b] = ev->value == 1;
				plat_set_btn(st, b, dpad_key[b] || stick.held[b]);
				return;
			}
			plat_set_btn(st, b, ev->value == 1);
			return;
		}
}

static void poll_fd(int fd, in_state *st)
{
	struct input_event ev;
	bool moved = false;

	while (fd >= 0 && read(fd, &ev, sizeof ev) == (ssize_t)sizeof ev) {
		/* The left stick, taken as a whole at each read */
		if (ev.type == EV_ABS && fd == fd_pad && stick_on && (ev.code == ABS_X || ev.code == ABS_Y)) {
			stick_axis[ev.code == ABS_Y].value = ev.value;
			moved = true;
			continue;
		}
		key_event(st, &ev);
	}
	if (moved) stick_apply(st);
}

void plat_input_wait(unsigned ms)
{
	struct pollfd p[3];
	nfds_t n = 0;

	if (fd_pad >= 0)   p[n++] = (struct pollfd){ .fd = fd_pad,   .events = POLLIN };
	if (fd_keys >= 0)  p[n++] = (struct pollfd){ .fd = fd_keys,  .events = POLLIN };
	if (fd_power >= 0) p[n++] = (struct pollfd){ .fd = fd_power, .events = POLLIN };
	/* POWER held in Hold mode: woken for the pill's next segment and the
	 * hold's end, which no event marks; and the work behind every screen
	 * (the downloads' lanes, plat_on_input_wait) has its step */
	ms = plat_input_wait_hook(plat_power_wait(ms));
	if (!n) { SDL_Delay(ms); return; }
	/* A signal ends it early too, which is what a stop should do */
	poll(p, n, (int)ms);
}

void plat_input_poll(in_state *st)
{
	SDL_Event e;

	memset(st->pressed, 0, sizeof st->pressed);
	st->power_tap = false;
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
	plat_power_poll(st);
}

/* The turn-off from the power key's queue, while a game or a program owns the
 * screen: a press, or a hold done, as MENU > Power Button says
 * (src/powerhold.h), on the same state as the screens' input, so a hold
 * begun on one side ends on the other */
bool device_power_pressed(void)
{
	struct input_event ev;
	bool pressed = false;

	while (fd_power >= 0 && read(fd_power, &ev, sizeof ev) == (ssize_t)sizeof ev)
		if (ev.type == EV_KEY && ev.code == KEY_POWER && ev.value != 2 &&
		    plat_power_event(ev.value == 1))
			pressed = true;
	return plat_power_due() || pressed;
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
 * through a DAC, at 0 dB, the loudest there is. Made here first, before MP4
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

/* MENU > Display > Screen Colors as the table carries them (panelgamma.h),
 * Standard until plat_settings_init reads the saved ones */
static panelgamma_color screen_color;

/* The table for the backlight's value now and the Screen Colors
 * (panelgamma.h): the dimming one at 9 and 13, none from 17 up in Standard.
 * Only while this process holds the display - at the start the splash still
 * does, and take_from_splash sets it then. */
static void display_gamma(const char *when)
{
	int raw, n;

	if (drm_fd < 0 || !display_held || (raw = read_int(BACKLIGHT)) < 0) return;
	n = panelgamma_apply_color(drm_fd, raw, &screen_color);
	if (n > 0)
		fprintf(stderr, "video: the display's gamma table set for brightness %d, %s\n",
		        raw, when);
	else if (n < 0)
		fprintf(stderr, "video: could not set the display's gamma table for brightness %d, "
		        "%s\n", raw, when);
}

/* The kept colors (night 0) and Night Mode as kept, with its nights about
 * now (src/nightmode.h): what the file says. Night Warmth's list shows its
 * cursor's warmth meanwhile (night_preview), which nothing else sees. */
static panelgamma_color screen_kept;
static night_cfg        night_kept;
static panelgamma_night night_sched;
static int              night_preview;
static time_t           night_looked, night_planned;

/* The colors and Night Mode for the other two writers, Diatom and portkeys,
 * which read the file each time they take the display (panelgamma.h), and
 * look at the nights in it once a second: the kept ones only, never a
 * preview */
static void screen_colors_share(void)
{
	if (panelgamma_screen_write(PANELGAMMA_COLOR_FILE, &screen_kept, &night_sched) != 0)
		fprintf(stderr, "settings: could not write %s\n", PANELGAMMA_COLOR_FILE);
}

/* The warmth on the display at `now`: the list's cursor's, or the kept
 * Night Mode's */
static int night_now(time_t now)
{
	return night_preview > 0 ? night_preview : panelgamma_night_at(&night_sched, (long long)now);
}

/* Night Mode's nights about `now` in this zone, and the file written again
 * when they are not the ones it has (or always, with `force`) */
static void night_plan(time_t now, bool force)
{
	panelgamma_night s;

	night_schedule(&night_kept, now, &s);
	night_planned = now;
	if (!force && !memcmp(&s, &night_sched, sizeof s)) return;
	night_sched = s;
	screen_colors_share();
}

/* Saturation: the BCSH block's knob on each of the PX30's two display
 * controllers (plastron's kernel patch 9-px2-px30-vop-bcsh-saturation),
 * 256 the picture as drawn: 5% a step, 128 (half) at -10 to 384 (one and a
 * half) at +10. The panel's is the VOP-B; the VOP-L, where it is bound (not
 * on the Pixel 2, seen 2026-11-16), takes the same value in its own units.
 * Kept by the kernel for every program after. A register write: no blink. */
#define SCREEN_SATURATION_FILE "/sys/devices/platform/ff460000.vop/saturation"
#define SCREEN_SATURATION_LIT  "/sys/devices/platform/ff470000.vop/saturation"
static int screen_saturation_on = 0;   /* the step on the knob now */

bool plat_screen_saturation_here(void)
{
	return access(SCREEN_SATURATION_FILE, W_OK) == 0;
}

static void screen_saturation_apply(int step)
{
	char text[8];

	step = clampi(step, -SCREEN_COLORS_RANGE, SCREEN_COLORS_RANGE);
	if (step == screen_saturation_on || access(SCREEN_SATURATION_FILE, W_OK) != 0) return;
	snprintf(text, sizeof text, "%d\n", 256 + 256 * 5 * step / 100);
	write_file(SCREEN_SATURATION_FILE, text);
	write_file(SCREEN_SATURATION_LIT, text);
	screen_saturation_on = step;
}

/* The table wanted, and when one was last loaded: a load waits out
 * SCREEN_COLORS_GAP_MS after the last (platform.h) */
static panelgamma_color screen_want;
static bool screen_held;
static Uint32 screen_loaded_at;

static void screen_colors_load(void)
{
	screen_color = screen_want;
	screen_held = false;
	screen_loaded_at = SDL_GetTicks();
	display_gamma("Screen Colors");
}

void plat_screen_colors_tick(bool now)
{
	if (screen_held && (now || SDL_GetTicks() - screen_loaded_at >= SCREEN_COLORS_GAP_MS))
		screen_colors_load();
}

void plat_screen_colors(const screen_colors *c, bool keep)
{
	panelgamma_color want = { c->temp, c->tint, c->contrast, c->gamma, 0, 0 };

	panelgamma_color_clamp(&want);
	if (keep) {
		screen_kept = want;
		screen_colors_share();
	}
	screen_saturation_apply(c->saturation);
	want.night = night_now(time(NULL));
	want.lift = screen_want.lift;   /* the brightness boost's, kept across */
	screen_want = want;
	screen_held = memcmp(&want, &screen_color, sizeof want) != 0;
	plat_screen_colors_tick(false);
}

/* The brightness boost's lift (src/levels.c, DESIGN.md 11.6): on the
 * display's table with the player's colors, at once - the step's one-frame
 * blink on the Pixel 2, none on the Brick, whose table loads at vblank */
void plat_brightness_lift(int lift)
{
	if (lift < 0) lift = 0;
	if (lift > PANELGAMMA_LIFT_MAX) lift = PANELGAMMA_LIFT_MAX;
	if (screen_want.lift == lift) return;
	screen_want.lift = lift;
	screen_held = memcmp(&screen_want, &screen_color, sizeof screen_want) != 0;
	plat_screen_colors_tick(true);
}

/* The warmth wanted now on the display, loaded at once (`at_once`: a kept
 * change, or the hour) or after the gap (the list's cursor running) */
static void night_want(bool at_once)
{
	screen_want.night = night_now(time(NULL));
	screen_held = memcmp(&screen_want, &screen_color, sizeof screen_want) != 0;
	plat_screen_colors_tick(at_once);
}

void plat_night(const struct night_cfg *n)
{
	night_kept = *n;
	night_cfg_clamp(&night_kept);
	night_preview = 0;
	night_plan(time(NULL), true);
	night_want(true);
	fprintf(stderr, "night: Night Mode %s, warmth %d, %02d:%02d to %02d:%02d\n",
	        night_mode_id(night_kept.mode), night_kept.warmth, night_kept.from / 60,
	        night_kept.from % 60, night_kept.until / 60, night_kept.until % 60);
}

void plat_night_preview(int warmth)
{
	night_preview = warmth < 0 ? 0 : warmth > PANELGAMMA_NIGHT_MAX ? PANELGAMMA_NIGHT_MAX : warmth;
	night_want(false);
}

bool plat_night_tick(void)
{
	time_t now = time(NULL);
	int w;

	if (now == night_looked) return false;
	night_looked = now;
	/* The nights again each minute, at once when the clock went back or
	 * the first night in the file ended: a zone chosen in Date & Time, the
	 * time set by hand, and the next night written for the other two */
	if (now - night_planned >= 60 || now < night_planned ||
	    (night_sched.n > 0 && (long long)now >= night_sched.until[0]))
		night_plan(now, false);
	w = night_now(now);
	if (w == screen_want.night) return false;
	night_want(true);
	fprintf(stderr, "night: Night Mode %s\n", w ? "on" : "off");
	return true;
}

static void apply_brightness(int b)
{
	char text[8];

	cur_bright = b;
	snprintf(text, sizeof text, "%d\n", bright_ladder[b]);
	write_file(BACKLIGHT, text);
	display_gamma("a brightness step");
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
	/* A position kept before 1.0.6 is on the old ladder, 17 at the bottom:
	 * moved once to the new rung nearest its light, or every player would
	 * start 1.0.6 two rungs darker, under the floor. "brightness_ladder" says
	 * the kept position is on this one, so it moves only the once. */
	if (db_get_int(db_dev(), "brightness_ladder", 0) < 2) {
		if (b >= 0) {
			int to = px2_bright_from_105(b);

			fprintf(stderr, "settings: brightness kept as rung %d of 1.0.5's ladder (%d); "
			        "now rung %d (%d)\n", b, bright_ladder_105[clampi(b, 0, BRIGHT_MAX)],
			        to, bright_ladder[to]);
			b = to;
			db_set_int(db_dev(), "brightness", b);
		}
		db_set_int(db_dev(), "brightness_ladder", 2);
	}
	if (b < 0 && (raw = read_int(BACKLIGHT)) >= 0) {
		int i;

		b = 0;
		for (i = 1; i <= BRIGHT_MAX; i++)
			if (abs(bright_ladder[i] - raw) < abs(bright_ladder[b] - raw)) b = i;
	}
	cur_vol    = v >= 0 ? clampi(v, 0, VOL_MAX)    : -1;
	cur_bright = b >= 0 ? clampi(b, 0, BRIGHT_MAX) : BRIGHT_MAX / 2;
	/* Screen Colors before the first table: written to the file even when
	 * Standard, so a launcher started again in the same boot leaves no
	 * older line for Diatom to read */
	screen_color.temp = db_get_int(db_dev(), "screen_temp", 0);
	screen_color.tint = db_get_int(db_dev(), "screen_tint", 0);
	screen_color.contrast = db_get_int(db_dev(), "screen_contrast", 0);
	screen_color.gamma = db_get_int(db_dev(), "screen_gamma", 0);
	panelgamma_color_clamp(&screen_color);
	screen_kept = screen_color;
	/* Night Mode's, and the file with both, before the first table */
	night_kept = night_load();
	night_plan(time(NULL), true);
	screen_color.night = night_now(time(NULL));
	screen_want = screen_color;
	/* Written whatever the knob holds: a launcher started again keeps
	 * nothing of the last one's (screen_saturation_on starts at 0) */
	screen_saturation_on = SCREEN_COLORS_RANGE + 1;
	screen_saturation_apply(db_get_int(db_dev(), "screen_saturation", 0));
	usb_volume_create();
	if (cur_vol >= 0) apply_volume(cur_vol);
	apply_brightness(cur_bright);
	/* Lit, whatever the launcher before this one left: it turns the light off
	 * for MP4's lock and for the shelf on the charger (src/chargedark.h),
	 * and one killed in either kept the panel dark until the next boot. */
	plat_backlight(true);
}

/* Off and back on at the same level: bl_power, the backlight's own switch, so
 * the brightness is not touched. MP4's pocket lock. */
void plat_backlight(bool on)
{
	write_file(BACKLIGHT_POWER, on ? "0\n" : "4\n");
}

void plat_backlight_dim(bool dim)
{
	char text[8];

	if (cur_bright < 0) return;
	/* Dim is the backlight's floor, and a rung under it stays as it is: its
	 * table is already on the display, and the floor would be brighter */
	snprintf(text, sizeof text, "%d\n",
	         dim && bright_ladder[cur_bright] > PANELGAMMA_FLOOR ? PANELGAMMA_FLOOR
	                                                             : bright_ladder[cur_bright]);
	write_file(BACKLIGHT, text);
}

void plat_display_plain(void)
{
	int n;

	if (drm_fd < 0 || !display_held) return;
	/* Saturation too: the program after this one sees the panel plain */
	screen_saturation_apply(0);
	n = panelgamma_clear(drm_fd);
	if (n > 0)
		fprintf(stderr, "video: the display's gamma table emptied for the program after this one\n");
	else if (n < 0)
		fprintf(stderr, "video: could not empty the display's gamma table\n");
}

/* The windows differ by jack, so the level is re-applied when a plug goes in
 * or out, as on the Brick. */
void plat_audio_jack_poll(void)
{
	if (!aout_should_reapply(jack_was, jack_present() != 0, cur_vol >= 0)) return;
	/* A plug in or out closes the volume boost: the new output starts at
	 * the top without it (src/levels.c), the first poll aside */
	if (jack_was >= 0) levels_output_changed();
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

/* A phone's USB tethering (phonelink.h). The phone's network interface, by
 * its driver, renamed PHONELINK_IF while still down (the kernel gave it
 * "usb0" when that was free, and USB File Transfer's gadget needs that
 * name), brought up, and a lease asked of the phone's DHCP server once, as
 * Wi-Fi's is (wifi.c): udhcpc stays and renews, its script sets the address
 * and a default route through the phone (whose internet the handheld then
 * has: the clock syncs, as on Wi-Fi). When the interface goes (the cable
 * out, tethering off), the client goes too. All that only with `act` (the
 * plug answered Phone); otherwise it is only looked at. A few small reads;
 * asked twice a second at most. */
#define PHONE_DHCP_PID "/var/run/udhcpc." PHONELINK_IF ".pid"

static pid_t phone_dhcp_running(void)
{
	char buf[16] = "";
	pid_t pid;

	if (!read_small(PHONE_DHCP_PID, buf, sizeof buf)) return 0;
	pid = (pid_t)atoi(buf);
	return pid > 0 && kill(pid, 0) == 0 ? pid : 0;
}

static void phone_dhcp_start(const char *ifname)
{
	pid_t pid = fork();

	if (pid < 0) return;
	if (pid == 0) {
		if (fork() == 0) {
			char *argv[] = { "/sbin/udhcpc", "-i", (char *)ifname, "-S", "-t", "10", "-T", "3",
			                 "-b", "-p", PHONE_DHCP_PID, NULL };
			int null = open("/dev/null", O_RDWR);

			if (null >= 0) { dup2(null, 1); dup2(null, 2); }
			execv(argv[0], argv);
			_exit(127);
		}
		_exit(0);
	}
	waitpid(pid, NULL, 0);
	fprintf(stderr, "usb: the phone's network %s: asking it for an address\n", ifname);
}

static bool netdev_up(const char *ifname)
{
	char path[96], buf[32];

	snprintf(path, sizeof path, "/sys/class/net/%s/flags", ifname);
	return read_small(path, buf, sizeof buf) && (strtoul(buf, NULL, 16) & 1);   /* IFF_UP */
}

bool plat_phone_link(bool act, char *ifname, size_t in, char *ip, size_t n)
{
	char names[16][IFNAMSIZ], drivers[16][32], name[IFNAMSIZ];
	const char *np[16], *dp[16];
	DIR *d = opendir("/sys/class/net");
	struct dirent *e;
	int count = 0, k, fd;

	if (ifname && in) ifname[0] = '\0';
	if (ip && n) ip[0] = '\0';
	while (d && count < 16 && (e = readdir(d))) {
		char path[300], link[256];
		ssize_t got;
		const char *base;

		if (e->d_name[0] == '.') continue;
		snprintf(names[count], sizeof names[count], "%.*s", IFNAMSIZ - 1, e->d_name);
		snprintf(path, sizeof path, "/sys/class/net/%s/device/driver", e->d_name);
		got = readlink(path, link, sizeof link - 1);
		link[got > 0 ? got : 0] = '\0';
		base = strrchr(link, '/');
		snprintf(drivers[count], sizeof drivers[count], "%.31s", base ? base + 1 : link);
		np[count] = names[count];
		dp[count] = drivers[count];
		count++;
	}
	if (d) closedir(d);
	if ((k = phonelink_pick(np, dp, count)) < 0) {
		pid_t pid = act ? phone_dhcp_running() : 0;

		if (pid) {
			kill(pid, SIGTERM);
			unlink(PHONE_DHCP_PID);
			fprintf(stderr, "usb: the phone's network went\n");
		}
		return false;
	}
	snprintf(name, sizeof name, "%s", names[k]);
	if (act && strcmp(name, PHONELINK_IF) && !netdev_up(name)) {
		char *argv[] = { "/sbin/ip", "link", "set", "dev", name, "name", PHONELINK_IF, NULL };
		int r = spawn_capture(argv, NULL, 0, 3);

		fprintf(stderr, "usb: the phone's network (%s, %s) renamed %s (%d)\n", name, drivers[k],
		        PHONELINK_IF, r);
		if (r == 0) snprintf(name, sizeof name, "%s", PHONELINK_IF);
	}
	if (act && !netdev_up(name)) {
		char *argv[] = { "/sbin/ip", "link", "set", name, "up", NULL };

		fprintf(stderr, "usb: the phone's network up (%d)\n", spawn_capture(argv, NULL, 0, 3));
	}
	if (act && !phone_dhcp_running()) phone_dhcp_start(name);
	if (ifname && in) snprintf(ifname, in, "%s", name);
	if (ip && n && (fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0)) >= 0) {
		struct ifreq ifr;

		memset(&ifr, 0, sizeof ifr);
		snprintf(ifr.ifr_name, sizeof ifr.ifr_name, "%s", name);
		ifr.ifr_addr.sa_family = AF_INET;
		if (ioctl(fd, SIOCGIFADDR, &ifr) == 0)
			inet_ntop(AF_INET, &((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr, ip, (socklen_t)n);
		close(fd);
	}
	return true;
}

/* The device on the port's bus is an iPhone (or an iPad): the bus's first
 * device, while the port is a host */
bool plat_usb_iphone(void)
{
	char v[16], prod[64];

	return read_small("/sys/bus/usb/devices/1-1/idVendor", v, sizeof v) &&
	       read_small("/sys/bus/usb/devices/1-1/product", prod, sizeof prod) &&
	       phonelink_is_iphone(v, prod);
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

int plat_brightness_raw(int level)
{
	return level >= 0 && level <= BRIGHT_MAX ? bright_ladder[level] : -1;
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
/* Bluetooth, the GPU's shaders, one speaker, brightness on FUNCTION with a
 * volume key (key_event), the side lights and Player 2: its profile's
 * (src/device/pixel2_profile.c). */
bool plat_menu_held(void) { return fn_down; }
bool plat_tap_held(in_button b) { return b == IN_L2 ? l2_down : b == IN_R2 ? r2_down : false; }

/* MENU > Side Lights: one line on the card, in the file plastron's gauge reads
 * at boot and again on SIGHUP (package/gauge/src/gauge.c; the line's words,
 * src/sidelights.h). Replaced whole (src/atomic.h), so the gauge never reads
 * half a line; a missing file is every part on both sides. */
#define SIDE_LIGHTS_FILE "/mnt/SDCARD/.userdata/" RK_ID "/side-lights"

side_lights plat_side_lights(void)
{
	char text[128] = "";
	int fd = open(SIDE_LIGHTS_FILE, O_RDONLY | O_CLOEXEC);
	ssize_t n;

	if (fd < 0) return LIGHTS_DEFAULT;
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
 * and a switch takes about 1.5 ms (2026-11-04).
 *
 * Never slowed on a card with the file steady-memory in its .userdata/pixel2,
 * or with steady-memory.txt on its boot partition, PX2BOOT, which a player
 * can make from any computer before the first start (plastron's zyperos.init
 * turns it into /run/zyperos-steady-memory at each start). Diatom's
 * port/pixel2.c reads the same two for a game's ceilings. Each
 * switch stops the DDR while the display reads it, at any moment of its
 * scan: plastron's DMC driver asks BL31 for no wait for the vertical blank,
 * as Rockchip's own kernel does. One Pixel 2 froze in the menus, where the
 * idle shelf above switched every few seconds (1,429 switches in 45 min of
 * use on another, 2026-11-15), and not in a game held at one rate. Off by
 * default: a still shelf drew 17 mA more from the battery at 1056 MHz
 * (2026-11-15), as 69 mW did in 2026-11-04's measure. */
/* The write on a thread of its own. plastron's DMC driver switches the DDR
 * in the display's vertical blank (9-px2-px30-dmc-vblank.patch), so a write
 * waits for it, up to a frame (16.6 ms each, measured 2026-11-15, where a
 * switch took 1.5 ms), and the press that wakes a still shelf would wait
 * with it. The latest wish wins; one waiting is all there is. At exit the
 * last one is waited for (ram_settle, up to 200 ms): the launcher's own
 * last word is the full rate (main.c), and a port that runs after it must
 * not start at 328 MHz. */
#define DMC_MAX "/sys/class/devfreq/dmc/max_freq"
static pthread_mutex_t ram_mx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  ram_cv = PTHREAD_COND_INITIALIZER;
static const char     *ram_want, *ram_done;
static bool            ram_thread_up;

static void *ram_writer(void *unused)
{
	(void)unused;
	pthread_mutex_lock(&ram_mx);
	for (;;) {
		const char *v;

		while (ram_want == ram_done) pthread_cond_wait(&ram_cv, &ram_mx);
		v = ram_want;
		pthread_mutex_unlock(&ram_mx);
		write_file(DMC_MAX, v);
		pthread_mutex_lock(&ram_mx);
		ram_done = v;
		pthread_cond_broadcast(&ram_cv);
	}
	return NULL;
}

static void ram_settle(void)
{
	struct timespec until;

	clock_gettime(CLOCK_REALTIME, &until);
	until.tv_nsec += 200 * 1000000L;
	if (until.tv_nsec >= 1000000000L) { until.tv_sec++; until.tv_nsec -= 1000000000L; }
	pthread_mutex_lock(&ram_mx);
	while (ram_want != ram_done)
		if (pthread_cond_timedwait(&ram_cv, &ram_mx, &until)) break;
	pthread_mutex_unlock(&ram_mx);
}

void plat_ram_idle(bool idle)
{
	char steady[512];
	const char *v;
	pthread_t t;

	snprintf(steady, sizeof steady, "%s/steady-memory", P_USERDATA);
	v = idle && access(steady, F_OK) != 0 &&
	    access("/run/zyperos-steady-memory", F_OK) != 0 ? "328000000" : "0";
	pthread_mutex_lock(&ram_mx);
	if (!ram_thread_up && pthread_create(&t, NULL, ram_writer, NULL) == 0) {
		pthread_detach(t);
		ram_thread_up = true;
		atexit(ram_settle);
	}
	if (ram_thread_up) {
		ram_want = v;
		pthread_cond_broadcast(&ram_cv);
	}
	pthread_mutex_unlock(&ram_mx);
	/* No thread: the write here, as before */
	if (!ram_thread_up) write_file(DMC_MAX, v);
}

/* ---- the chip's temperature ---- */

/* The RK3326's two sensors, the zones its profile names (px30.dtsi's
 * soc-thermal, the CPU's, and gpu-thermal). Both slow their part at 70 and
 * 85 C and power the handheld off at 115 C (critical, an orderly power-off:
 * see src/heat.h). Found by type, so a kernel that numbers its zones
 * differently is still read right; a zone not there is left out
 * (src/thermal.h). */
#define FAKE_TEMP "/tmp/zyperos_fake_temp"

bool plat_chip_temp(int *mc)
{
	int v = read_int(FAKE_TEMP);

	if (v >= 0) {
		*mc = v < 1000 ? v * 1000 : v;
		return true;
	}
	if ((v = thermal_max_c(plat_profile()->thermal_zones, NULL)) < 0) return false;
	*mc = v * 1000;
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
	/* ZYPEROS_BATTERY_DIR stands in for the gauge's folder: a host shot's
	 * About with a battery in it (tools/i18n-shots.py's BATTERY_DIRS) */
	const char *dir = getenv("ZYPEROS_BATTERY_DIR");

	return dir && *dir ? dir : "/sys/class/power_supply/battery";
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
		/* The cable, not the battery's status (src/charging_rule.h):
		 * rk817_charger says Discharging on the cable with a full battery,
		 * and Auto Off powered a plugged-in device down, twice on
		 * 2026-11-04. */
		v = read_int("/sys/class/power_supply/rk817-charger/online");
		if (v == 0 || v == 1) {
			*charging = charging_from(v, NULL);
			return true;
		}
		/* The status is read only where online gives no answer: an
		 * explicit zero wins over one that still says Charging or Full. */
		if ((s = fopen("/sys/class/power_supply/battery/status", "r"))) {
			bool read = fgets(st, sizeof st, s) != NULL;
			if (read) *charging = charging_from(v, st);
			fclose(s);
			if (read) return true;
		}
	}
	return pct != NULL;
}
