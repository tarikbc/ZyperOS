/* SPDX-License-Identifier: GPL-3.0-only */
/* The TrimUI Brick (TG3040): the display, the buttons that arrive on three
 * different devices, the LEDs, the codec, the panel backlight, the battery,
 * and where things live on its card. Everything here is what makes a Brick a
 * Brick; the rest of ZyperOS reaches it through platform.h and device.h. */
#include "gadget.h"
#include "charging.h"
#include "charging_rule.h"
#include "atomic.h"
#include "audioout.h"
#include "clamp.h"
#include "db.h"
#include "device.h"
#include "fileio.h"
#include "linksteps.h"
#include "nightkeep.h"
#include "levels.h"
#include "menuvol.h"
#include "panelgamma.h"
#include "panelgamma_sunxi.h"
#include "platform.h"
#include "screenfx_draw.h"
#include "stickpad.h"
#include "thermal.h"
#include "wdog.h"
#include "keymap.h"
#ifdef ZYPEROS_DESK
#include "desk.h"
#include "deskcable.h"
#endif

#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#ifdef __linux__
#include <linux/input.h>
#include <sys/ioctl.h>
#endif
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static SDL_Window *win;
static SDL_Renderer *ren;
/* A panel of another size than the canvas (src/canvas.h): the Smart Pro's
 * 1280x720 under its 1368x768 canvas. The canvas is drawn into screen_tex,
 * at its own size, and plat_present scales it onto the whole panel
 * (screen_dst). NULL where the panel is the canvas (the Brick's 1024x768,
 * the host's desktop): the window is the screen, as it always was. */
static SDL_Texture *screen_tex;
static SDL_Rect     screen_dst;
/* A video picture under the screen for the next present (plat_video_under),
 * where the screen is screen_tex */
static struct { bool on, has_src; SDL_Texture *tex; SDL_Rect src, dst; SDL_Color bg; } under;

#define BITS_PER_LONG   (8 * (int)sizeof(long))
#define NLONGS(max)     (((max) + BITS_PER_LONG) / BITS_PER_LONG)
#define BIT_IS_SET(a,b) (((a)[(b) / BITS_PER_LONG] >> ((b) % BITS_PER_LONG)) & 1UL)

static int fd_pad = -1;     /* "Brick Gamepad": every button, the volume keys too */
static int fd_keys = -1;    /* "sunxi-keyboard": declares the volume keys, never sends them */
static int fd_power = -1;   /* "axp2202-pek" */
static int jack_fd = -1;    /* "audiocodec sunxi Audio Jack": the jack switch, for the settings */

/* ZYPEROS_INPUT_DEBUG=1 logs raw evdev codes, so one press tells you exactly
 * which device a control arrives on. */
static int dbg_input;

#ifdef __linux__
/* The left stick as the d-pad on the launcher's screens (src/stickpad.h),
 * where the pad has one: the Smart Pro's, from tools/sppad.c, as ABS_X and
 * ABS_Y. A direction is held while its key or the stick holds it. The Brick's
 * pad has no axis, so stick_max stays 0 and nothing changes there. */
_Static_assert((int)IN_LEFT == (int)STICK_LEFT && (int)IN_RIGHT == (int)STICK_RIGHT &&
               (int)IN_UP == (int)STICK_UP && (int)IN_DOWN == (int)STICK_DOWN,
               "stickpad's directions are the d-pad's buttons");
static stickpad stick;
static int      stick_x, stick_y, stick_max;
static bool     dpad_key[STICK_DIRS];
#endif

#ifdef __linux__
/* Every button by its key code, as the Pixel 2 reads its own. brick-pad sends
 * the Pixel's codes: A, B, X and Y as SOUTH, EAST, NORTH and WEST, named for
 * their labels, and the d-pad as BTN_DPAD_*. Then the Brick's own: MENU, and
 * the two front keys, which are brightness. L2 and R2 are the menu music's
 * tracks on the shelf; the Brick has no lights for the Pixel 2's L2+R2. */
static const struct { int code; in_button b; } keymap[] = {
	{ BTN_DPAD_UP, IN_UP },       { BTN_DPAD_DOWN, IN_DOWN },
	{ BTN_DPAD_LEFT, IN_LEFT },   { BTN_DPAD_RIGHT, IN_RIGHT },
	{ BTN_SOUTH, IN_ACCEPT },     { BTN_EAST, IN_BACK },
	{ BTN_NORTH, IN_X },          { BTN_WEST, IN_Y },
	{ BTN_TL, IN_L1 },            { BTN_TR, IN_R1 },
	{ BTN_TL2, IN_L2 },           { BTN_TR2, IN_R2 },
	{ BTN_SELECT, IN_SELECT },    { BTN_START, IN_START },
	{ BTN_MODE, IN_MENU },
	{ BTN_THUMBL, IN_BRIGHTDN },  { BTN_THUMBR, IN_BRIGHTUP },
	{ KEY_VOLUMEDOWN, IN_VOLDN }, { KEY_VOLUMEUP, IN_VOLUP },
	{ KEY_POWER, IN_POWER },
};
#endif

const char *P_ROOT = "/mnt/SDCARD/ZyperOS";
const char *P_CARD = "/mnt/SDCARD";
const char *P_ROMS = "/mnt/SDCARD/Roms";
/* The card folder of the device built for: the Brick's own, or the Smart
 * Pro's, which runs this platform layer too (mk/cross.mk) */
#ifndef ZYPEROS_DEVICE_ID
#define ZYPEROS_DEVICE_ID "brick"
#endif
const char *P_USERDATA = "/mnt/SDCARD/.userdata/" ZYPEROS_DEVICE_ID;

/* The Brick's boot screen is drawn before anything could say a theme to it */
int plat_boot_theme(const char *text) { (void)text; return 0; }
const char *P_SHARED = "/mnt/SDCARD/.userdata/shared";
/* Over The Hare's page. On the card rather than in the binary so it can be
 * restyled with a text editor and a reload, which is the whole argument for
 * a file-transfer feature existing at all. */
const char *P_WEB = "/mnt/SDCARD/ZyperOS/res/web";

const char *P_FONT = "/mnt/SDCARD/ZyperOS/menu.ttf";

SDL_Renderer *plat_renderer(void) { return ren; }

/* The boot animation is plastron's splash, drawing into the framebuffer's
 * last page until ZyperOS draws. EGL's setup moves the screen to an empty page
 * before ZyperOS has drawn anything, which the splash puts back; so it is told
 * when the next move is ZyperOS's first frame, with a SIGUSR1, as on the
 * Pixel 2. By name, as nothing here knows its pid; none running (the splash
 * already gone) is nothing to tell. */
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

/* The screens' shader is on this frame already (plat_screen_finish) */
static bool fx_done;

/* CRT Curved's frame: while the screens' shader bends, ZyperOS draws into
 * this texture of the screen's size instead of the window, and the bend
 * draws it onto the window (plat_screen_finish), as the Pixel 2 bends its
 * screen_tex. Reading the window back instead (sfx_bend_target) stalls a
 * GPU that draws in tiles, the Brick's PowerVR, for a whole frame's pixels
 * every frame. Made the first time the curve is on; while it is off ZyperOS
 * draws straight into the window as before, so a frame without the curve
 * costs what it did. */
static SDL_Texture *bend_tex;

static bool bending(void) { return sfx_on() && sfx_curved(); }

/* The window, as a render target: the window itself, screen_tex on a panel
 * of another shape (show_scaled), or in the host's desk mode the texture its
 * screen is drawn into, which goes to the window at present
 * (src/device/desk.h) */
static SDL_Texture *window_target(void)
{
#ifdef ZYPEROS_DESK
	if (desk_on()) return desk_screen();
#endif
	return screen_tex;
}

/* Where ZyperOS's frame goes: bend_tex while the curve is on, else the
 * window (also when bend_tex cannot be made: the bend reads back then) */
static SDL_Texture *screen_target(void)
{
	if (!bending()) return window_target();
	if (!bend_tex) {
		bend_tex = SDL_CreateTexture(ren, PLAT_TARGET_FORMAT, SDL_TEXTUREACCESS_TARGET,
		                             ZYPEROS_SCREEN_W, ZYPEROS_SCREEN_H);
		if (bend_tex) SDL_SetTextureBlendMode(bend_tex, SDL_BLENDMODE_NONE);
		else fprintf(stderr, "screens' shader: no texture for the curve (%s); "
		             "it reads the screen back\n", SDL_GetError());
	}
	return bend_tex;
}

/* The shader on the frame where it is: CRT Curved's bend draws bend_tex
 * onto the window (or, the frame it came on, which went to the window,
 * reads it back and draws it again), then the mask multiplies it. The
 * Brick's own rows offer none of the old screens (plat_gpu_shaders), so on
 * the Brick this is Off and nothing until they do; the host, which shows
 * the Pixel 2's screens, has them. */
void plat_screen_finish(void)
{
	SDL_Rect all = { 0, 0, ZYPEROS_SCREEN_W, ZYPEROS_SCREEN_H };
	bool in_tex = bend_tex && SDL_GetRenderTarget(ren) == bend_tex;

	if (in_tex && !bending()) {
		/* The curve went off with this frame in bend_tex: the frame as it is */
		SDL_SetRenderTarget(ren, window_target());
		SDL_RenderCopy(ren, bend_tex, NULL, &all);
		in_tex = false;
	}
	if (fx_done || !sfx_on()) return;
	fx_done = true;
	if (sfx_curved()) {
		if (in_tex) {
			Uint8 cr, cg, cb, ca;

			SDL_SetRenderTarget(ren, window_target());
			SDL_GetRenderDrawColor(ren, &cr, &cg, &cb, &ca);
			SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
			SDL_RenderClear(ren);
			SDL_SetRenderDrawColor(ren, cr, cg, cb, ca);
			sfx_draw_curved(ren, bend_tex, &all, 0.0);
		} else {
			sfx_bend_target(ren, ZYPEROS_SCREEN_W, ZYPEROS_SCREEN_H);
		}
	}
	sfx_draw_mask(ren, ZYPEROS_SCREEN_W, ZYPEROS_SCREEN_H, &all, 0.0);
}

bool plat_screen_shader_in_frame(void) { return sfx_on(); }

/* The canvas onto a panel of another size: the video under it if there is
 * one (at the panel's own resolution, its rectangle scaled as the canvas
 * is), then the canvas, the screens' shader on it already. Over a video the
 * canvas went into a clear texture, which SDL's blend premultiplied, so it
 * goes over with ONE; with none it is copied as it is, alpha ignored, as the
 * Brick's window shows it. */
static void show_scaled(void)
{
	SDL_SetRenderTarget(ren, NULL);
	SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
	SDL_RenderClear(ren);
	if (under.on) {
		static SDL_BlendMode premul;
		float sx = (float)screen_dst.w / ZYPEROS_SCREEN_W, sy = (float)screen_dst.h / ZYPEROS_SCREEN_H;
		SDL_FRect r = { screen_dst.x + under.dst.x * sx, screen_dst.y + under.dst.y * sy,
		                under.dst.w * sx, under.dst.h * sy };
		SDL_Rect bg = screen_dst;

		if (!premul)
			premul = SDL_ComposeCustomBlendMode(
				SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
				SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
		SDL_SetRenderDrawColor(ren, under.bg.r, under.bg.g, under.bg.b, 255);
		SDL_RenderFillRect(ren, &bg);
		if (under.tex) SDL_RenderCopyF(ren, under.tex, under.has_src ? &under.src : NULL, &r);
		SDL_SetTextureBlendMode(screen_tex, premul);
		under.on = false;
	} else {
		SDL_SetTextureBlendMode(screen_tex, SDL_BLENDMODE_NONE);
	}
	SDL_RenderCopy(ren, screen_tex, NULL, &screen_dst);
	SDL_RenderPresent(ren);
	SDL_SetRenderTarget(ren, screen_tex);
}

/* Kept only in the host's desk mode, which draws on its screen texture
 * (window_target). The Brick draws on the window, whose back buffer after
 * SDL_RenderPresent is one of fb0's pages as Imagination's EGL left it,
 * not the frame just shown. bend_tex would keep it, but while it is in use
 * the shader is in the frame, and no frame is drawn over the last then. */
bool plat_screen_kept(void)
{
#ifdef ZYPEROS_DESK
	return desk_on();
#else
	return false;
#endif
}

/* The Brick's panel is the screen, so drawing to the screen is drawing to the
 * window (on a panel of another size than the canvas, show_scaled). The first present tells
 * the splash first, and goes on the kernel's clock beside the splash's own
 * lines, so the boot from power to shelf reads off one log, as on the
 * Pixel 2. The draw color is put back, as ZyperOS expects it to stay. */
void plat_present(void)
{
	static bool shown;
	Uint8 r, g, b, a;

	plat_grab_frame(ren);
	wdog_feed();                    /* the loop came round (wdog.h) */
	plat_overlay_present(ren);
	plat_screen_finish();
	if (!shown) {
		signal_splash(SIGUSR1);
#ifdef __linux__
		struct timespec now;
		char line[64];
		int fd;

		clock_gettime(CLOCK_BOOTTIME, &now);
		snprintf(line, sizeof line, "zyperos: first frame at %.3f s\n",
		         now.tv_sec + now.tv_nsec / 1e9);
		if ((fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC)) >= 0) {
			if (write(fd, line, strlen(line)) < 0) { /* not fatal */ }
			close(fd);
		}
#endif
		shown = true;
	}
#ifdef ZYPEROS_DESK
	if (desk_on()) desk_present(ren);
	else
#endif
	if (screen_tex) {
		SDL_GetRenderDrawColor(ren, &r, &g, &b, &a);
		show_scaled();
		SDL_SetRenderDrawColor(ren, r, g, b, a);
	} else {
		SDL_RenderPresent(ren);
	}
	fx_done = false;
	/* The next frame into bend_tex while the curve is on */
	if (bending()) SDL_SetRenderTarget(ren, screen_target());
}
/* The window, or screen_tex on a panel of another size, or bend_tex */
void plat_draw_to_screen(void) { SDL_SetRenderTarget(ren, screen_target()); }

/* The Brick's screen is its panel: the picture goes straight into the frame,
 * and what is drawn after it lands on top of it. On a panel of another size
 * the picture waits for the present (show_scaled), at the panel's own
 * pixels, and the screen is cleared to nothing, so it shows through wherever
 * this frame draws nothing; under CRT Curved it goes into the frame as on the
 * Brick, so the curve bends it with the rest. */
void plat_video_under(SDL_Texture *tex, const SDL_Rect *src, const SDL_Rect *dst, SDL_Color bg)
{
	SDL_BlendMode mode;

	if (!screen_tex || bending()) {
		SDL_SetRenderDrawColor(ren, bg.r, bg.g, bg.b, 255);
		SDL_RenderClear(ren);
		if (tex) SDL_RenderCopy(ren, tex, src, dst);
		return;
	}
	under.on = true;
	under.tex = tex;
	under.has_src = src != NULL;
	under.src = src ? *src : (SDL_Rect){ 0, 0, 0, 0 };
	under.dst = dst ? *dst : (SDL_Rect){ 0, 0, 0, 0 };
	under.bg = bg;
	SDL_SetRenderTarget(ren, screen_tex);
	SDL_GetRenderDrawBlendMode(ren, &mode);
	SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
	SDL_SetRenderDrawColor(ren, 0, 0, 0, 0);
	SDL_RenderClear(ren);
	SDL_SetRenderDrawBlendMode(ren, mode);
}

/* No ports on the Brick, and no splash --still to hold a picture */
bool plat_save_screen(const char *path) { (void)path; return false; }

#ifdef __linux__
/* Flash Drive's pictures for usbdrive (src/usbdrive_screen.c): the frame
 * just drawn, never shown, read back and made 0.625 of the canvas, 640x480
 * on a 4:3 one and 855x480 on the Smart Pro's 1368x768, the size usbdrive's
 * atlas is laid out in, landscape (the Brick's panel is not turned), as raw
 * XRGB8888. usbdrive's page on the Brick (plastron's
 * package/usbdrive/src/panel_fb.c) scales it up to the panel again. Each
 * picture is the average of the 1.6 x 1.6 frame pixels under it, two taps a
 * way, so text keeps its weight. A few frames at Flash Drive's start: a
 * read back here costs nothing a game would see. */
/* usbdrive's pictures' size: src/usbdrive_screen.c's UD_SAVE_W and
 * UD_SAVE_H, the units its atlas is written in (a file's format, not a
 * place on this screen) */
#define SAVE_W (ZYPEROS_SCREEN_W * 5 / 8)
#define SAVE_H 480
bool plat_save_screen_hidden(const char *path)
{
	const int w = ZYPEROS_SCREEN_W, h = ZYPEROS_SCREEN_H;
	uint32_t *in = malloc((size_t)w * h * 4), *out = malloc((size_t)SAVE_W * SAVE_H * 4);
	bool ok = false;
	FILE *f;
	int x, y;

	if (in && out && SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_XRGB8888, in, w * 4) == 0) {
		for (y = 0; y < SAVE_H; y++) {
			/* The source rows the picture's row covers: its two edges */
			int y0 = y * h / SAVE_H, y1 = ((y + 1) * h - 1) / SAVE_H;

			for (x = 0; x < SAVE_W; x++) {
				int x0 = x * w / SAVE_W, x1 = ((x + 1) * w - 1) / SAVE_W;
				uint32_t a = in[(size_t)y0 * w + x0], b = in[(size_t)y0 * w + x1];
				uint32_t c = in[(size_t)y1 * w + x0], d = in[(size_t)y1 * w + x1];
				uint32_t v = 0;
				int k;

				for (k = 0; k < 24; k += 8)
					v |= (((a >> k & 255) + (b >> k & 255) + (c >> k & 255) + (d >> k & 255) + 2) / 4) << k;
				out[(size_t)y * SAVE_W + x] = v;
			}
		}
		if ((f = fopen(path, "wb"))) {
			ok = fwrite(out, 4, (size_t)SAVE_W * SAVE_H, f) == (size_t)SAVE_W * SAVE_H;
			if (fclose(f) != 0) ok = false;
		}
	}
	free(in);
	free(out);
	return ok;
}
#else
/* The host has no usbdrive to leave pictures for */
bool plat_save_screen_hidden(const char *path) { (void)path; return false; }
#endif
SDL_Texture *plat_load_screen(const char *path) { (void)path; return NULL; }

bool plat_video_init(void)
{
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
#ifdef __linux__
	SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
	/* plastron loads the GPU's display driver while ZyperOS starts rather than
	 * before it, as nothing needs it until here: so this waits for it, which
	 * it normally doesn't have to. Only where it is plastron's module, which
	 * shows in /sys/module once loaded; 2 s at most. */
	if (access("/usr/lib/brick/dc_sunxi.ko", F_OK) == 0) {
		unsigned t0 = plat_now_ms(), waited;

		while (access("/sys/module/dc_sunxi", F_OK) != 0 && plat_now_ms() - t0 < 2000)
			usleep(2000);
		waited = plat_now_ms() - t0;
		if (waited >= 2)
			fprintf(stderr, "video: waited %u ms for the GPU's display driver\n", waited);
	}
#endif
	if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
		fprintf(stderr, "video init: %s\n", SDL_GetError());
		return false;
	}
	SDL_ShowCursor(SDL_DISABLE);
	/* A host build run by a check or for a shot (ZYPEROS_HEADLESS set, with
	 * SDL_VIDEODRIVER=dummy) puts no window on the desktop of whoever is using
	 * the computer: the window is hidden and the software renderer draws it,
	 * which needs no OpenGL. SDL's offscreen driver would do as well but gives
	 * every window OpenGL by default and fails without an EGL library; dummy
	 * has no GL at all. The checks draw in software anyway. */
	bool headless = getenv("ZYPEROS_HEADLESS") != NULL;
#ifdef ZYPEROS_DESK
	/* A window on a computer: desk mode, the handheld's panel at a zoom */
	if (!headless && desk_on()) {
		if (!desk_video_init(&win, &ren)) return false;
		SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
		SDL_RendererInfo info;
		if (SDL_GetRendererInfo(ren, &info) == 0)
			fprintf(stderr, "renderer: %s, driver: %s, vsync %s\n", info.name, SDL_GetCurrentVideoDriver(),
			        (info.flags & SDL_RENDERER_PRESENTVSYNC) ? "granted" : "DECLINED (requested)");
		return true;
	}
#endif
	/* The panel as it is seen, landscape (the Smart Pro's kernel turns its
	 * 720x1280 panel into a 1280x720 framebuffer): the canvas itself where
	 * it is the panel's size, else the canvas scaled onto it (show_scaled).
	 * A shot's hidden window is the canvas: the shot is the canvas's pixels. */
	const device_profile *p = plat_profile();
	int pw = p->screen.turn % 180 ? p->screen.panel_h : p->screen.panel_w;
	int ph = p->screen.turn % 180 ? p->screen.panel_w : p->screen.panel_h;
	bool scaled = !headless && (pw != ZYPEROS_SCREEN_W || ph != ZYPEROS_SCREEN_H);
	int ww = scaled ? pw : ZYPEROS_SCREEN_W;
	int wh = scaled ? ph : ZYPEROS_SCREEN_H;

	win = SDL_CreateWindow("ZyperOS", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, ww, wh,
	                       headless ? SDL_WINDOW_HIDDEN : SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
	if (!win) {
		fprintf(stderr, "window: %s\n", SDL_GetError());
		return false;
	}
	ren = SDL_CreateRenderer(win, -1, headless ? SDL_RENDERER_SOFTWARE
	                                           : SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (!ren) {
		fprintf(stderr, "renderer: %s\n", SDL_GetError());
		return false;
	}
	SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
	if (scaled) {
		screen_tex = SDL_CreateTexture(ren, PLAT_TARGET_FORMAT, SDL_TEXTUREACCESS_TARGET,
		                               ZYPEROS_SCREEN_W, ZYPEROS_SCREEN_H);
		if (!screen_tex) {
			fprintf(stderr, "screen texture: %s\n", SDL_GetError());
			return false;
		}
		screen_dst = (SDL_Rect){ 0, 0, ww, wh };
		SDL_SetRenderTarget(ren, screen_tex);
		fprintf(stderr, "video: the %dx%d canvas scaled onto a %dx%d panel\n",
		        ZYPEROS_SCREEN_W, ZYPEROS_SCREEN_H, ww, wh);
	}
	/* Whether vsync was GRANTED, not whether it was asked for. PRESENTVSYNC
	 * is a request the driver may decline in silence, and the shelf loop has
	 * no delay in it - so a declined request is not a slower animation, it is
	 * an uncapped loop presenting mid-scanout. This line existed and read
	 * info.name only, which is how the request came to be talked about as if
	 * it were the result. */
	SDL_RendererInfo info;
	if (SDL_GetRendererInfo(ren, &info) == 0)
		fprintf(stderr, "renderer: %s, driver: %s, vsync %s\n",
		        info.name, SDL_GetCurrentVideoDriver(),
		        (info.flags & SDL_RENDERER_PRESENTVSYNC) ? "granted"
		                                                 : "DECLINED (requested)");
	/* The cube turns two offscreen faces, so this is load-bearing rather than
	 * informational: without it the vertical shelves have nothing to draw. */
	if (SDL_GetRendererInfo(ren, &info) == 0)
		fprintf(stderr, "renderer: render-to-texture %s\n",
		        (info.flags & SDL_RENDERER_TARGETTEXTURE) ? "available"
		                                                  : "MISSING");
	return true;
}

void plat_video_quit(void)
{
	plat_osd_forget();
	sfx_forget();
	if (bend_tex) { SDL_DestroyTexture(bend_tex); bend_tex = NULL; }
if (screen_tex) { SDL_DestroyTexture(screen_tex); screen_tex = NULL; }
	under.on = false;
#ifdef ZYPEROS_DESK
	desk_video_quit();
#endif
	if (ren) { SDL_DestroyRenderer(ren); ren = NULL; }
	if (win) { SDL_DestroyWindow(win); win = NULL; }
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

#ifdef __linux__
/* Every input device this file wants, found by what each reports, not by
 * number, in one pass, as pixel2.c finds the Pixel 2's: there is no udev, and
 * enumeration order is not a promise. Every node is kept but the ones not
 * wanted, as closing one waits out an RCU grace period in the kernel. */
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
		if (fd_pad < 0 && BIT_IS_SET(keys, BTN_SOUTH)) {
			struct input_absinfo ax;

			fd_pad = fd;
			if (ioctl(fd, EVIOCGABS(ABS_X), &ax) == 0 && ax.maximum > 0)
				stick_max = ax.maximum;
		} else if (fd_keys < 0  && BIT_IS_SET(keys, KEY_VOLUMEUP))        fd_keys = fd;
		else if (fd_power < 0 && BIT_IS_SET(keys, KEY_POWER))           fd_power = fd;
		else if (jack_fd < 0  && BIT_IS_SET(sw, SW_HEADPHONE_INSERT))   jack_fd = fd;
		else kept = false;
		if (!kept) close(fd);
	}
}
#endif

bool plat_input_init(void)
{
	char marker[512];
	snprintf(marker, sizeof marker, "%s/.input_debug", P_ROOT);
	dbg_input = getenv("ZYPEROS_INPUT_DEBUG") != NULL || access(marker, F_OK) == 0;
#ifdef __linux__
	if (fd_pad < 0 || fd_keys < 0 || fd_power < 0) input_scan();
	if (fd_pad < 0)   fprintf(stderr, "input: no gamepad found\n");
	if (fd_keys < 0)  fprintf(stderr, "input: no volume keys found\n");
	if (fd_power < 0) fprintf(stderr, "input: no power key found\n");
#endif
	return true;
}

/* Throw away everything that queued up while a game owned the screen. */
void plat_input_flush(void)
{
	SDL_Event e;
#ifdef ZYPEROS_DESK
	/* Desk mode keeps what is not a stale press: its socket's commands, the
	 * window closed, a controller plugged in */
	if (desk_on()) desk_flush();
	else
#endif
	{
		SDL_PumpEvents();
		while (SDL_PollEvent(&e)) { }
	}
#ifdef __linux__
	{
		struct input_event ev;
		int fds[3], i;
		fds[0] = fd_power; fds[1] = fd_keys; fds[2] = fd_pad;
		for (i = 0; i < 3; i++)
			while (fds[i] >= 0 &&
			       read(fds[i], &ev, sizeof ev) == (ssize_t)sizeof ev) { }
	}
#endif
	plat_menu_forget();
}

/* No wait, as on the Pixel 2: the keys are read straight from the kernel, and
 * a boot on plastron makes no input noise (three recorded 2026-10-12: a plain
 * one, power held, buttons pressed, had nothing but the presses themselves),
 * so emptying the queues is enough. The 350 ms this replaces waited out
 * TrimUI's input daemon. */
void plat_input_settle(in_state *st)
{
	plat_input_flush();
	memset(st, 0, sizeof *st);
}

/* The descriptors stay open: they are display-independent. */
void plat_input_quit(void) { }

/* No motor yet: GPIO 227 on the bldo3 rail waits for the tester's buzz (the
 * Brick parity plan's item 14), and the profile says none (rumble false) */
bool plat_rumble_buzz(float scale, int ms)
{
	(void)scale;
	(void)ms;
	return false;
}

#ifdef __linux__
/* The stick where the last report left it: each direction it holds or lets
 * go, with the d-pad's key for that direction */
static void stick_apply(in_state *st)
{
	bool was[STICK_DIRS];
	int d;

	memcpy(was, stick.held, sizeof was);
	stickpad_update(&stick, stick_x, stick_y, stick_max);
	for (d = 0; d < STICK_DIRS; d++)
		if (stick.held[d] != was[d])
			plat_set_btn(st, (in_button)d, stick.held[d] || dpad_key[d]);
}
#endif

/* MENU and volume: brightness together where the profile says so, the Smart
 * Pro (DEV_BRIGHT_MENU_VOL), and plain keys on the Brick, with F1 and F2 */
static menuvol menu_chord = MENUVOL_INIT;

static void poll_fd(int fd, in_state *st)
{
#ifdef __linux__
	struct input_event ev;
	size_t i;
	bool moved = false;

	while (fd >= 0 && read(fd, &ev, sizeof ev) == (ssize_t)sizeof ev) {
		/* The stick, taken as a whole at each report */
		if (ev.type == EV_ABS && fd == fd_pad && (ev.code == ABS_X || ev.code == ABS_Y)) {
			if (ev.code == ABS_X) stick_x = ev.value;
			else stick_y = ev.value;
			moved = true;
			continue;
		}
		if (ev.type == EV_SYN && ev.code == SYN_REPORT && moved) {
			stick_apply(st);
			moved = false;
			continue;
		}
		if (ev.type != EV_KEY || ev.value == 2) continue;
		if (dbg_input) fprintf(stderr, "[in] fd=%d code=%d val=%d\n", fd, ev.code, ev.value);
		for (i = 0; i < sizeof keymap / sizeof keymap[0]; i++)
			if (keymap[i].code == ev.code) {
				in_button b = keymap[i].b;

				/* MENU and the volume keys, which are brightness
				 * together on the Smart Pro (src/device/menuvol.h) */
				if (b == IN_MENU) {
					menuvol_menu(&menu_chord, st, ev.value == 1, plat_brightness_on_volume());
				} else if (b == IN_VOLUP || b == IN_VOLDN) {
					menuvol_volume(&menu_chord, st, b == IN_VOLUP, ev.value == 1,
					               plat_brightness_on_volume());
				/* A d-pad key: held while it or the stick holds it */
				} else if (b <= IN_DOWN) {
					dpad_key[b] = ev.value == 1;
					plat_set_btn(st, b, dpad_key[b] || stick.held[b]);
				/* A repeat is no let-go of POWER (src/powerhold.h) */
				} else if (b != IN_POWER || ev.value != 2) {
					plat_key(st, b, ev.value == 1);
				}
				break;
			}
	}
	if (moved) stick_apply(st);
#else
	(void)fd; (void)st;
#endif
}

/* A keyboard is the buttons by src/device/keymap.h's one map, the host's desk
 * mode's and the Simulator's: every button the launcher reads has a key, so a
 * keyboard can go wherever the handheld can, out of the tutorial too
 * (docs/building.md lists them) */
static in_button map_key(SDL_Scancode sc) { return keymap_button(sc); }

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
	wdog_feed();
	ms = plat_input_wait_hook(plat_power_wait(ms));
	if (!n) {
#ifdef ZYPEROS_DESK
		/* No input file on a computer: woken by an event (a key, the
		 * mouse, desk mode's socket), not after the whole wait */
		desk_wait(ms);
#else
		SDL_Delay(ms);
#endif
		return;
	}
	/* A signal ends it early too, which is what a stop should do */
	poll(p, n, (int)ms);
}

void plat_input_poll(in_state *st)
{
	wdog_feed();                    /* the loop came round (wdog.h) */
	memset(st->pressed, 0, sizeof st->pressed);
	st->power_tap = false;
	if (plat_terminating()) st->quit_requested = true;
	/* The jack, checked here because this is the one thing every screen does
	 * once a frame - and, more to the point, the one thing the launcher stops
	 * doing while a game runs, since it is blocked in plat_resident_wait. That
	 * is exactly the ownership boundary the level needs: whoever is pumping
	 * input owns the volume, so the launcher must not re-apply a level over
	 * Diatom's while Diatom holds it. Diatom polls the same switch on its own
	 * side for the same reason. */
	menuvol_poll(&menu_chord, st);
	plat_audio_jack_poll();
	/* And the mute switch, for the same reason and on the same frame. */
	plat_mute_poll(true);
	SDL_Event e;
#ifdef ZYPEROS_DESK
	if (desk_on()) desk_poll_begin(st);
#endif
	while (SDL_PollEvent(&e)) {
#ifdef ZYPEROS_DESK
		if (e.type != SDL_QUIT && desk_on() && desk_event(&e, st)) continue;
#endif
		switch (e.type) {
		case SDL_QUIT:
			st->quit_requested = true;
			break;
		case SDL_KEYDOWN:
		case SDL_KEYUP:
			if (e.key.repeat) break;
			plat_key(st, map_key(e.key.keysym.scancode), e.type == SDL_KEYDOWN);
			break;
		}
	}
	poll_fd(fd_pad, st);
	poll_fd(fd_keys, st);
	poll_fd(fd_power, st);
	plat_power_poll(st);
	plat_menu_after(st);
}

/* The launcher and Diatom take turns on one framebuffer here, so there is
 * nothing to hand over. The display's gamma table is the kernel's for both,
 * set again on the way back all the same: a game's Diatom sets it at a
 * night's start or end, and a game that ended mid-change may have left
 * another one. */
#ifdef __linux__
static void display_gamma(const char *when);
static void screen_colors_init(void);
#endif
/* In the host's desk mode the window shows Diatom's frames while a game
 * runs (src/device/desk.h); on the Brick, nothing. */
void device_display_release(void)
{
#ifdef ZYPEROS_DESK
	if (desk_on()) desk_game_begin();
#endif
}
unsigned device_game_wait(unsigned ms)
{
#ifdef ZYPEROS_DESK
	if (desk_on()) return desk_game_wait(ms);
#endif
	return ms;
}
void device_display_take(void)
{
#ifdef ZYPEROS_DESK
	if (desk_on()) desk_game_end();
#endif
#ifdef __linux__
	display_gamma("taking the display back");
#endif
}

/* The power key's queue, drained: true if a press was in it. The launcher
 * watches power this way while a child or a game owns the screen, which is
 * the one control that always has to work. */
bool device_power_pressed(void)
{
	bool pressed = false;
#ifdef ZYPEROS_DESK
	/* The keyboard's P, the bar's POWER, the window closed: desk mode's */
	if (desk_on()) {
		int edge;

		while ((edge = desk_power_edge()) >= 0)
			if (plat_power_event(edge == 1)) pressed = true;
	}
#endif
#ifdef __linux__
	struct input_event ev;

	while (fd_power >= 0 && read(fd_power, &ev, sizeof ev) == (ssize_t)sizeof ev)
		if (ev.type == EV_KEY && ev.code == KEY_POWER && ev.value != 2 &&
		    plat_power_event(ev.value == 1))
			pressed = true;
#endif
	/* A hold done, as MENU > Power Button on Hold asks (src/powerhold.h) */
	return plat_power_due() || pressed;
}

/* ---- volume and brightness, straight at the hardware ----------------------
 *
 * ZyperOS's own code, against the same two device interfaces the emulator uses.
 * It replaced a third-party settings library, which bought three things beyond
 * independence:
 *
 *   - The scales MATCH. Diatom drives volume as 21 positions and brightness
 *     as a 12-rung ladder; libmsettings used 0-20 and 0-10. Every level
 *     crossing the socket had to be rescaled, and a rescale is where an
 *     off-by-one hides. Both sides now speak the same ladder and the
 *     conversion is the identity.
 *   - Settings persist in ZyperOS's own file rather than the stock firmware's
 *     /mnt/UDISK/system.json, which is TrimUI's state and not ours to own.
 *   - One fewer binary on the card.
 *
 * The device facts here were measured, not guessed, and two of them are traps:
 *
 *   `digital volume` (0-63) is the speaker level and is INVERTED - 0 is
 *   loudest, 63 is quietest - while the driver's own metadata advertises the
 *   opposite. It also declares mute=0, so its minimum is maximum attenuation
 *   (about -74 dB) rather than silence; the speaker switch carries the last
 *   step.
 *
 *   `Headphone Volume` is NOT a speaker level. Raising it routes audio to the
 *   jack and mutes the speakers. It stays at zero.
 *
 * The backlight has no /sys/class/backlight on this device; it is the
 * Allwinner disp2 engine, addressed with plain command numbers (not _IOWR) and
 * an unsigned long[4] argument block. */

#ifdef __linux__
#include <sys/ioctl.h>

/* From the kernel UAPI (sound/asound.h), vendored rather than depended on.
 * Only the integer case is needed; the union is declared at full size because
 * its size is what _IOWR bakes into the request number, and a wrong layout
 * produces a wrong request number rather than a failed call. */
struct pl_ctl_elem_id {
	unsigned int  numid;
	int           iface;
	unsigned int  device;
	unsigned int  subdevice;
	unsigned char name[44];
	unsigned int  index;
};
struct pl_aes_iec958 {
	unsigned char status[24], subcode[147], pad, dig_subframe[4];
};
struct pl_ctl_elem_value {
	struct pl_ctl_elem_id id;
	unsigned int indirect: 1;
	union {
		union { long value[128]; long *value_ptr; } integer;
		union { long long value[64]; long long *value_ptr; } integer64;
		union { unsigned int item[128]; unsigned int *item_ptr; } enumerated;
		union { unsigned char data[512]; unsigned char *data_ptr; } bytes;
		struct pl_aes_iec958 iec958;
	} value;
	unsigned char reserved[128];
};
#define PL_CTL_ELEM_READ   _IOWR('U', 0x12, struct pl_ctl_elem_value)
#define PL_CTL_ELEM_WRITE  _IOWR('U', 0x13, struct pl_ctl_elem_value)

/* The codec's controls, the speakers' and the jack's windows and the
 * display engine's rungs - and why each is what it is: src/device/
 * brick_levels.h, as the Pixel 2's are in pixel2_levels.h. */
#include "brick_levels.h"
_Static_assert(VOL_MAX == PLAT_VOL_MAX, "brick_levels.h vs PLAT_VOL_MAX");

#define DISP_LCD_SET_BRIGHTNESS 0x102
#define DISP_LCD_GET_BRIGHTNESS 0x103

/* The table (brick_levels.h) is the definition; platform.h only publishes its
 * extent. If they ever disagree this stops the build rather than shipping a
 * rescale that is quietly off by a rung, which is the bug that put the
 * maximum in a header. */
_Static_assert(BRIGHT_MAX == PLAT_BRIGHT_MAX, "bright_ladder vs PLAT_BRIGHT_MAX");

static int mixer_fd = -1, disp_fd = -1;
static int cur_vol = -1, cur_bright = -1;

static int ctl_io(const char *name, long *val, int write)
{
	struct pl_ctl_elem_value v;

	if (mixer_fd < 0) return -1;
	memset(&v, 0, sizeof v);
	v.id.iface = 2;                                 /* SNDRV_CTL_ELEM_IFACE_MIXER */
	snprintf((char *)v.id.name, sizeof v.id.name, "%s", name);
	if (write) {
		v.value.integer.value[0] = *val;
		return ioctl(mixer_fd, PL_CTL_ELEM_WRITE, &v);
	}
	if (ioctl(mixer_fd, PL_CTL_ELEM_READ, &v) < 0) return -1;
	*val = v.value.integer.value[0];
	return 0;
}

/* Every nudge of the rocker lands here, including while a game is running, so
 * what this costs is a frame-budget question rather than a tidiness one.
 * Measured on the card 2026-09-05: the atomic_open/atomic_commit this replaces
 * was 3.03 ms median with a 9.27 ms tail, against a 16.7 ms frame. The two
 * writes below are 0.48 ms each with a 0.59 ms tail. */
static void levels_save(void)
{
	db_set_int(db_dev(), "volume", cur_vol);
	db_set_int(db_dev(), "brightness", cur_bright);
}

/* Write a control and complain if it does not land. Discarding this return is
 * what cost the project 18 dB for its whole life; see mixer_defaults(). */
static void ctl_set(const char *name, long val)
{
	if (ctl_io(name, &val, 1) < 0)
		fprintf(stderr, "settings: mixer rejected '%s' = %ld\n", name, val);
}

/* Codec-wide state that the volume keys do not own, set once at init.
 *
 * HP_CTL is 0-7 at 6 dB a step and is INVERTED, exactly like GAIN_CTL above:
 * 0 is loudest, 7 is near silence. The driver's TLV claims the opposite
 * (dBscale-min=-42 dB, step +6 dB) and is wrong in the same way it is wrong
 * about GAIN_CTL, so the metadata cannot be trusted on this codec at all.
 *
 * Despite its name it is not the jack. SPEAKER_CTL drives the speaker off the
 * headphone stage, so this control gates everything the device plays. Diatom's
 * port carried a comment insisting that raising it rerouted output to the jack
 * and muted the speakers - what actually happens is that raising it attenuates
 * the speaker, which sounds identical and is not the same thing. Settled by
 * ear on 2026-08-31: at 7 the speaker is barely audible, at 0 it is loud.
 *
 * This write already existed, in apply_volume, spelled "Headphone" - a control
 * this codec does not have. PL_CTL_ELEM_WRITE matches names exactly, the
 * return was thrown away, and so every sound the device ever made came out
 * 18 dB down while the source read as though it were setting this to zero.
 *
 * SWAP_CTL at 1 crosses the channels. The stock hook clears it
 * (runtrimui-original.sh: `tinymix set 1 0`) and so does NextUI; we never did,
 * so left and right have been backwards the entire time. It is an enumerated
 * control rather than an integer, but the value union overlaps and we only
 * ever write item 0, so the integer path reaches it.
 *
 * HP_CTL stays at 0 for both outputs. The jack was calibrated on 2026-09-02 and
 * the answer was not this control: 9.3 dB was wanted at the ceiling and this
 * steps in sixes, and it attenuates the speaker as well, so it cannot be moved
 * for the jack alone. The headphone case is handled by its own window on
 * GAIN_CTL instead - see HP_RAW_TOP. The one exception is the cut, in
 * apply_volume.
 *
 * HPSW_CTL on, always. With it and SPEAKER_CTL both off the codec stops taking
 * samples (see apply_volume), so the cut, which turns SPEAKER_CTL off, is only
 * safe while this one is on. TrimUI's system left it on; plastron's comes up
 * with it off, and there every mute and every level 0 stopped the sound
 * device instead of silencing it: MP4's clock stood still until the cut came
 * off (found 2026-10-16). The speaker runs off this stage too, and with it
 * off after being on the speaker was weak. */
static void mixer_defaults(void)
{
	if (mixer_fd < 0) return;
	ctl_set(HPSW_CTL, 1);
	ctl_set(HP_CTL, 0);
	ctl_set(SWAP_CTL, 0);
}

/* Is there a plug in the headphone jack?
 *
 * SW_HEADPHONE_INSERT on the codec's own input node. This device exposes the
 * state nowhere else: there is no ALSA jack kcontrol among its seventeen
 * controls and nothing under /sys for it, so the only way to ask is EVIOCGSW,
 * which is why this opens an input device rather than reading a file.
 *
 * Found by capability rather than by number, by input_scan with the rest: the
 * cost of the wrong node is a volume ladder silently calibrated for the wrong
 * output. */
static int jack_present(void)
{
	unsigned long bits[NLONGS(SW_MAX)];

	if (jack_fd < 0) return 0;
	memset(bits, 0, sizeof bits);
	if (ioctl(jack_fd, EVIOCGSW(sizeof bits), bits) < 0) return 0;
	return BIT_IS_SET(bits, SW_HEADPHONE_INSERT) ? 1 : 0;
}

static int jack_was = -1;         /* last state acted on; -1 = never asked */

/* THE MUTE SWITCH, and why it is read here rather than as an input event.
 *
 * Measured 2026-09-16 by watching every input device and every exported GPIO
 * while the switch was flipped. It reports two ways: as EV_SW code 1 on
 * /dev/input/event3, and as this pin. The pin is the one worth reading,
 * because an event only arrives on a CHANGE - it says nothing about which way
 * the switch is pointing at boot, after a resume, or after the launcher
 * restarts, which is exactly when a physical control and the software can
 * disagree. A file that always holds the truth cannot drift.
 *
 * DOWN, which reads 1, is muted. Eric's call: "off" is what the position says,
 * and on a mute switch that labels the sound.
 *
 * Opened once and pread, not opened per poll: this runs from plat_input_poll,
 * which every screen calls once a frame. */
#define MUTE_GPIO "/sys/class/gpio/gpio243/value"
static int mute_fd = -1;
/* Tri-state: -1 means "not known", which is not the same as "not muted".
 *
 * The switch can be flipped DURING a game, when Diatom owns the codec and this
 * side is blocked in plat_resident_wait seeing nothing. Coming back with a
 * remembered position, the poll below compares the hardware against it, finds
 * them equal and returns without re-applying - so the launcher would play at
 * full volume with the switch down. Exactly the fault jack_forget exists to
 * prevent, and found by reading its comment. */
static int muted = -1;

static bool mute_switch_down(void)
{
	char c = 0;

	if (mute_fd < 0) mute_fd = open(MUTE_GPIO, O_RDONLY | O_CLOEXEC);
	if (mute_fd < 0) return false;
	if (pread(mute_fd, &c, 1, 0) != 1) return false;
	return c == '1';
}

static void apply_volume(int v)
{
	int hp = jack_present();
	bool cut = v == 0 || muted == 1;
	long raw = cut ? GAIN_RAW_MAX
	         : aout_level_to_raw(v, VOL_MAX,
	                             hp ? HP_RAW_TOP : SPK_RAW_TOP,
	                             hp ? HP_RAW_BOTTOM : SPK_RAW_BOTTOM);
	long quiet = cut ? HP_CTL_QUIET : 0;
	long on = !cut;

	jack_was = hp;
	cur_vol = v;
	/* Zero has to cut the path, not merely attenuate it - and so does the
	 * switch, which is why it lands here rather than beside the callers: every
	 * route that re-applies a level (a nudge, a jack coming out) passes
	 * through this line and honors the switch for free.
	 *
	 * SPEAKER_CTL cuts the speaker only, so with headphones in the switch and
	 * level 0 both left them playing until 2026-09-30. They are cut by level
	 * instead: GAIN_CTL and HP_CTL both at their quiet ends, about -116 dB and
	 * silent by ear that day. Not "Headphone Switch", tried the same day: with
	 * it and SPEAKER_CTL both off the codec stops taking samples (hw_ptr stood
	 * still), and whatever waits on its stream waits until one comes back on -
	 * a game froze switching to a headset until the mute came off. */
	ctl_io(GAIN_CTL, &raw, 1);
	ctl_io(HP_CTL, &quiet, 1);
	ctl_io(SPEAKER_CTL, &on, 1);
}

/* Re-apply if the plug went in or came out since the last time.
 *
 * Without this a level only moves to the right ladder at the next volume press,
 * so plugging in mid-game leaves the old register in place - which is precisely
 * the moment the difference is 9 dB and being worn on your head. Cheap enough
 * to call from a periodic path: one ioctl on an already-open fd, and it writes
 * nothing unless the state actually changed. */
bool plat_headphones_present(void) { return jack_present() != 0; }

/* The Brick's port can play through a USB-C DAC once it is a host, but nothing
 * on the board tells it a device is plugged in, and switching it to host by
 * hand risks supplying power into a charger: not worth a board. 2026-10-05. */
bool plat_usb_audio_present(void) { return false; }

/* Files over the USB cable while a computer has the Brick on it, Down To The
 * Wire as on the Pixel 2, and over Wi-Fi by Over The Hare otherwise. A
 * computer is one that has brought up the cable's network link (its carrier):
 * a charger powers the cable but never does. The controller's own state is no
 * use here, as it says "configured" from the first computer on, through
 * unplugs and chargers; plastron's usbgadget drops the link whenever the
 * cable loses power. plastron gives usb0 this address. Two small reads,
 * asked as menus are built. */
#define CABLE_ADDR "10.55.0.2"
#define CABLE_PEER "10.55.0.1"      /* the computer, from udhcpd (usbgadget) */
#define CABLE_UDC  "/sys/class/udc/5100000.udc-controller"

static bool file_starts(const char *path, const char *word)
{
	char buf[32] = "";
	int fd = open(path, O_RDONLY | O_CLOEXEC);

	if (fd < 0) return false;
	if (read(fd, buf, sizeof buf - 1) < 0) buf[0] = '\0';
	close(fd);
	return strncmp(buf, word, strlen(word)) == 0;
}

bool plat_cable_link(char *addr, size_t n)
{
#ifdef ZYPEROS_DESK
	if (deskcable_on()) return deskcable_link(addr, n);
#endif
	if (!file_starts("/sys/class/net/usb0/carrier", "1") ||
	    !file_starts("/sys/class/power_supply/axp2202-usb/online", "1"))
		return false;
	if (addr && n) snprintf(addr, n, "%s", CABLE_ADDR);
	return true;
}

/* The cable as on the Pixel 2 (src/usblink.h): charging only until asked.
 * plastron's usbgadget script makes the network gadget at boot and binds it
 * only in Developer Mode (a development image's); ZyperOS asks once a plug
 * when a computer is on the cable, and binds it for File Transfer or
 * Developer, or hands the card over for Flash Drive.
 *
 * Telling a computer from a charger is the Brick's own problem: the
 * AXP2202's BC1.2 reads a computer's port as a dedicated charger (plastron's
 * kernel patch 0029, measured on a Mac's USB-A port), and its Type-C CC
 * says nothing either through a USB-A cable. The one tell is the computer:
 * it resets the bus and configures a device it sees, which a charger never
 * does. So each plug is probed: the gadget bound for up to USB_PROBE_MS,
 * and a reset or a configuration in that time (the controller's state off
 * "not attached") is a computer, left bound until the player answers
 * (Charge Only lets it go, as on the Pixel 2); none is a charger, and the
 * probe let go. The probe is this file's, not the link's: plat_usb_link
 * answers for what usblink set, and the probe's binding never shows there.
 * A computer sees the Brick's network device for the probe's moment, or
 * until the question is answered: no way to ask the computer without it. */
#define CABLE_GADGET "/sys/kernel/config/usb_gadget/brick/UDC"
#define CABLE_ONLINE "/sys/class/power_supply/axp2202-usb/online"
#define USB_PROBE_MS 3000u

enum { PLUG_OUT, PLUG_PROBING, PLUG_COMPUTER, PLUG_CHARGER };
static int plug_state = PLUG_OUT;
static unsigned plug_t0;
static bool link_on;          /* what usblink set: plat_usb_link's answer */

/* The link's work in the background (gadget.h): the gadget's binds and
 * unbinds in hwput's child, never waited on */
static void link_setup(void)
{
	static bool done;

	if (done) return;
	done = true;
	gadget_init(&(gadget_cfg){ CABLE_UDC, NULL, CABLE_GADGET, NULL, "usb0", CABLE_PEER, false,
	                           NULL, plat_now_ms });
}

/* The gadget on the controller, or asked to be: true when it is bound or
 * will be once the work asked is done */
static bool gadget_bound(void)
{
	link_setup();
	return gadget_link(-1);
}

static bool gadget_bind(bool on)
{
	link_setup();
	return gadget_link(on ? 1 : 0);
}

/* A host on the bus: the controller past "not attached" (reset, addressed,
 * configured), which only a computer's reset moves it to */
static bool host_seen(void)
{
	return file_starts(CABLE_UDC "/state", "default") ||
	       file_starts(CABLE_UDC "/state", "addressed") ||
	       file_starts(CABLE_UDC "/state", "configured");
}

usb_peer plat_usb_peer(void)
{
#ifdef ZYPEROS_DESK
	if (deskcable_on()) return deskcable_peer();
#endif
	/* The link's work, at every look at the port (gadget.h) */
	link_setup();
	gadget_pump();
	if (!file_starts(CABLE_ONLINE, "1")) {
		/* Out: the probe's binding goes with it (the link's is usblink's
		 * to let go, which it does when it sees this) */
		if (plug_state != PLUG_OUT && !link_on) gadget_bind(false);
		if (plug_state == PLUG_COMPUTER) fprintf(stderr, "usb: the cable came out\n");
		plug_state = PLUG_OUT;
		return USB_PEER_NONE;
	}
	switch (plug_state) {
	case PLUG_OUT:
		/* A new plug, or the cable already in at the start: probed */
		plug_t0 = plat_now_ms();
		plug_state = PLUG_PROBING;
		if (!gadget_bind(true)) fprintf(stderr, "usb: the gadget would not bind to probe the cable\n");
		return USB_PEER_NONE;
	case PLUG_PROBING:
		if (host_seen()) {
			plug_state = PLUG_COMPUTER;
			fprintf(stderr, "usb: a computer on the cable (it reset the bus in %u ms)\n",
			        plat_now_ms() - plug_t0);
			return USB_PEER_COMPUTER;
		}
		if (plat_now_ms() - plug_t0 < USB_PROBE_MS) return USB_PEER_NONE;
		plug_state = PLUG_CHARGER;
		if (!link_on) gadget_bind(false);
		fprintf(stderr, "usb: a charger on the cable (no reset in %u ms)\n", USB_PROBE_MS);
		return USB_PEER_CHARGER;
	case PLUG_COMPUTER:
		return USB_PEER_COMPUTER;
	default:
		return USB_PEER_CHARGER;
	}
}

/* usblink's link: -1 asks, 0 lets it go (charging only), 1 binds it, 2 a
 * fresh one (off and on). The answer is the link as it will be once the
 * work asked is done (gadget.h), which says its end in the log. */
bool plat_usb_link(int op)
{
#ifdef ZYPEROS_DESK
	if (deskcable_on()) return deskcable_usb_link(op);
#endif
	link_setup();
	if (op < 0) return link_on;
	link_on = gadget_link(op);
	return link_on;
}

/* The controller's state says nothing here (CABLE_UDC's comment) */
int plat_usb_configured(void)
{
#ifdef ZYPEROS_DESK
	if (deskcable_on()) return deskcable_configured();
#endif
	return -1;
}

bool plat_cable_sense(link_sense *out)
{
#ifdef ZYPEROS_DESK
	if (deskcable_on()) return deskcable_sense(out);
#endif
	(void)out;
	return false;
}
bool plat_usb_power(usb_power *out) { (void)out; return false; }
bool plat_usb_power_grant(unsigned plug) { (void)plug; return false; }
bool plat_usb_power_mode(bool automatic) { (void)automatic; return false; }
bool plat_phone_link(bool act, char *ifname, size_t in, char *ip, size_t n)
{
	(void)act; (void)ifname; (void)in; (void)ip; (void)n;
	return false;
}
bool plat_usb_iphone(void) { return false; }

/* The controller lets go of the cable and takes it again, so the computer
 * sees the Brick plugged in afresh, when the link needs it (cablefresh.h):
 * the Pixel 2's computer once dropped it while the controller went on saying
 * "configured" (2026-10-08). Its state says nothing here ("configured" from
 * the first computer on), so only the carrier and the computer's answer
 * count. */
void plat_cable_reconnect(void)
{
#ifdef ZYPEROS_DESK
	if (deskcable_on()) { deskcable_reconnect(); return; }
#endif
	link_setup();
	gadget_fresh();
}

/* The switch, checked wherever the jack is and for the same reason: this is
 * the one thing every screen does once a frame.
 *
 * It mutes the LAUNCHER's audio only. While a game runs the launcher is
 * blocked in plat_resident_wait and Diatom owns the codec - it must, or the
 * two would fight over one control - so the switch reaches a game by being
 * told to it, not by both of them reading the same pin. Deciding what a
 * physical control MEANS is the launcher's job; being quiet when asked is the
 * emulator's. */
bool plat_mute_poll(bool own_volume)
{
	int now = mute_switch_down() ? 1 : 0;

	if (now == muted) return false;
	muted = now;
	/* Out of a game this side is the only writer. Diatom used to be told here
	 * too, and it answered by applying its own level - which out of a game is
	 * -1, unknown - so every unmute switched the speaker back off a moment
	 * after this side turned it on, and with headphones in wrote a gain past
	 * the register's end that wrapped around to full volume. Found
	 * 2026-09-30. It needs no telling here: every RUN sends the switch's
	 * state before the game starts. */
	if (own_volume) {
		if (cur_vol >= 0) apply_volume(cur_vol);
		return true;
	}
	/* DURING A GAME, ONLY THE CUT - never this side's level.
	 *
	 * apply_volume would write GAIN_CTL from cur_vol, which is this side's
	 * idea of the volume and is stale the moment Diatom takes over: the
	 * player can change it in-game and only Diatom knows where it ended up.
	 * Re-applying it here is the exact fault the comment above plat_input_poll
	 * warns about, arriving by a new road.
	 *
	 * Turning the speaker back ON is safe even if Diatom sits at level 0,
	 * because it attenuates as well as switching - its own comment puts the
	 * control's minimum near -74 dB - so the worst case is a path opened onto
	 * something already inaudible, and Diatom's own next write settles it.
	 *
	 * This side cuts because it is immediate: 100 ms, the resident loop's
	 * period. The SETMUTE is what makes the cut STICK, since Diatom writes
	 * the same controls whenever it applies a level. Neither alone is
	 * enough - one is fast and one is durable. */
	plat_resident_tell("SETMUTE\ton=%d", muted == 1 ? 1 : 0);
	{
		long on = (muted != 1);

		ctl_io(SPEAKER_CTL, &on, 1);
		/* And the headphones, by level as in apply_volume - the cut only.
		 * Coming back, the level is Diatom's, and the SETMUTE above has it
		 * put its own back. */
		if (!on) {
			long raw = GAIN_RAW_MAX, quiet = HP_CTL_QUIET;

			ctl_io(GAIN_CTL, &raw, 1);
			ctl_io(HP_CTL, &quiet, 1);
		}
	}
	return true;
}

bool plat_muted(void) { return muted == 1; }

/* Whatever this side remembers about the switch was formed while it was
 * driving; a game just was. Called where jack_forget is, and for its reason. */
static void mute_forget(void) { muted = -1; }

void plat_audio_jack_poll(void)
{
	if (!aout_should_reapply(jack_was, jack_present() != 0, cur_vol >= 0))
		return;
	/* A plug in or out closes the volume boost: the new output starts at
	 * the top without it (src/levels.c), the first poll aside */
	if (jack_was >= 0) levels_output_changed();
	apply_volume(cur_vol);
}

/* Forget which way the jack was, so the next poll re-applies whatever the
 * hardware says now instead of trusting a memory formed before a handover. */
static void jack_forget(void) { jack_was = -1; }

static void apply_brightness(int b)
{
	unsigned long a[4] = { 0, 0, 0, 0 };

	cur_bright = b;
	if (disp_fd < 0) return;
	a[1] = bright_ladder[b];
	ioctl(disp_fd, DISP_LCD_SET_BRIGHTNESS, a);
}

void plat_settings_init(void)
{
	int v = -1, b = -1;

	mixer_fd = open("/dev/snd/controlC0", O_RDWR);
	disp_fd  = open("/dev/disp", O_RDWR);
	if (mixer_fd < 0) fprintf(stderr, "settings: no /dev/snd/controlC0\n");
	if (disp_fd  < 0) fprintf(stderr, "settings: no /dev/disp\n");
	mixer_defaults();
	if (jack_fd < 0) input_scan();
	if (jack_fd < 0) fprintf(stderr, "settings: no headphone jack input node; "
	                                 "volume will use the speaker ladder\n");

	/* The player's last choice, and it wins. The two-tier lookup this
	 * replaces - a saved level, then a shipped default - is one key each,
	 * because seeding writes the default once and a nudge overwrites it. Same
	 * outcome, and it can no longer get the precedence wrong: the config used
	 * to be reapplied over the saved level at every boot, which put it ahead
	 * of the player. */
	v = db_get_int(db_dev(), "volume", -1);
	b = db_get_int(db_dev(), "brightness", -1);

	/* Both are stored in the units this code uses - a rung each - so there is
	 * no conversion here and no way for one key to mean two things. */

	/* Last, whatever the panel is already at, so the launcher's first OSD
	 * tells the truth even with nothing configured anywhere - launch.sh set a
	 * brightness before this process existed. Nearest rung, because the panel
	 * reports raw values and only the ladder has rungs. */
	if (b < 0 && disp_fd >= 0) {
		int raw = ioctl(disp_fd, DISP_LCD_GET_BRIGHTNESS, (unsigned long[4]){ 0, 0, 0, 0 });
		int i;
		if (raw >= 0) {
			b = 0;
			for (i = 1; i <= BRIGHT_MAX; i++)
				if (abs(bright_ladder[i] - raw) < abs(bright_ladder[b] - raw)) b = i;
		}
	}
	cur_vol    = v >= 0 ? clampi(v, 0, VOL_MAX)    : -1;
	cur_bright = b >= 0 ? clampi(b, 0, BRIGHT_MAX) : BRIGHT_MAX / 2;
	if (cur_vol >= 0) apply_volume(cur_vol);
	apply_brightness(cur_bright);
	screen_colors_init();
}

/* The disp2 engine's LCD commands, beside SET/GET_BRIGHTNESS in the vendor's
 * sunxi_display2.h: 0x104 lights the backlight, 0x105 puts it out. Screen 0
 * in the first word, as for brightness. Watched on the device both ways. */
#define DISP_LCD_BACKLIGHT_ENABLE  0x104
#define DISP_LCD_BACKLIGHT_DISABLE 0x105

void plat_backlight(bool on)
{
	unsigned long a[4] = { 0, 0, 0, 0 };

	if (disp_fd < 0) return;
	ioctl(disp_fd, on ? DISP_LCD_BACKLIGHT_ENABLE : DISP_LCD_BACKLIGHT_DISABLE, a);
}

void plat_backlight_dim(bool dim)
{
	unsigned long a[4] = { 0, 0, 0, 0 };

	if (disp_fd < 0 || cur_bright < 0) return;
	a[1] = bright_ladder[dim ? 0 : cur_bright];
	ioctl(disp_fd, DISP_LCD_SET_BRIGHTNESS, a);
}

/* ---- Screen Colors and Night Mode ---- */

/* MENU > Display > Screen Colors and Night Mode on the display's own gamma
 * table (src/panelgamma_sunxi.h), which the panel's every frame goes
 * through: the launcher's, a game's and anything after, at no cost to any
 * of them. The same keeping as the Pixel 2's (src/device/pixel2.c), with
 * nothing under a floor: the Brick's rungs are its backlight's own. */

/* What is on the display now, Standard until plat_settings_init reads the
 * saved ones */
static panelgamma_color screen_color;

static void display_gamma(const char *when)
{
	if (disp_fd < 0) return;
	if (panelgamma_sunxi_apply(disp_fd, &screen_color) == 0)
		fprintf(stderr, "video: the display's gamma table %s, %s\n",
		        panelgamma_color_standard(&screen_color) ? "off" : "set", when);
	else
		fprintf(stderr, "video: could not set the display's gamma table, %s\n", when);
}

/* The kept colors (night 0) and Night Mode as kept, with its nights about
 * now (src/nightmode.h): what the file says. Night Warmth's list shows its
 * cursor's warmth meanwhile (night_preview), which nothing else sees. */
static panelgamma_color screen_kept;
static night_cfg        night_kept;
static panelgamma_night night_sched;
static int              night_preview;
static time_t           night_looked, night_planned;

/* The colors and Night Mode for Diatom, which reads the file each time it
 * takes the display (panelgamma.h) and looks at the nights in it once a
 * second over a game: the kept ones only, never a preview */
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

/* Saturation: the display engine's own color block, in sysfs, at the Pixel
 * 2's steps (src/panelgamma_sunxi.h) */
#define SCREEN_SATURATION_FILE PANELGAMMA_SUNXI_SATURATION
static int screen_saturation_on = 0;   /* the step on the knob now */

bool plat_screen_saturation_here(void)
{
	return access(SCREEN_SATURATION_FILE, W_OK) == 0;
}

static void screen_saturation_apply(int step)
{
	char text[8];
	FILE *f;

	step = clampi(step, -SCREEN_COLORS_RANGE, SCREEN_COLORS_RANGE);
	if (step == screen_saturation_on || !(f = fopen(SCREEN_SATURATION_FILE, "w"))) return;
	snprintf(text, sizeof text, "%d\n", panelgamma_sunxi_saturation(step));
	fputs(text, f);
	if (fclose(f) == 0) screen_saturation_on = step;
	else fprintf(stderr, "settings: could not write %s\n", SCREEN_SATURATION_FILE);
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

#ifdef ZYPEROS_DESK
	desk_note_colors(c, keep);   /* a Linux host's desk mode, which has no /dev/disp */
#endif

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
	 * time set by hand, and the next night written for Diatom */
	if (now - night_planned >= 60 || now < night_planned ||
	    (night_sched.n > 0 && (long long)now >= night_sched.until[0]))
		night_plan(now, false);
	w = night_now(now);
	if (w == screen_want.night) return false;
	night_want(true);
	fprintf(stderr, "night: Night Mode %s\n", w ? "on" : "off");
	return true;
}

/* Screen Colors and Night Mode as kept, before the launcher's first frame:
 * the file written even when Standard, so a launcher started again in the
 * same boot leaves no older line for Diatom to read, and the table set
 * whatever the last launcher left on the display */
static void screen_colors_init(void)
{
	screen_color.temp = db_get_int(db_dev(), "screen_temp", 0);
	screen_color.tint = db_get_int(db_dev(), "screen_tint", 0);
	screen_color.contrast = db_get_int(db_dev(), "screen_contrast", 0);
	screen_color.gamma = db_get_int(db_dev(), "screen_gamma", 0);
	panelgamma_color_clamp(&screen_color);
	screen_kept = screen_color;
	night_kept = night_load();
	night_plan(time(NULL), true);
	screen_color.night = night_now(time(NULL));
	screen_want = screen_color;
	/* Written whatever the knob holds: a launcher started again keeps
	 * nothing of the last one's (screen_saturation_on starts at 0) */
	screen_saturation_on = SCREEN_COLORS_RANGE + 1;
	screen_saturation_apply(db_get_int(db_dev(), "screen_saturation", 0));
	display_gamma("at the start");
}

/* Before a program of its own that draws (Flash Drive): the panel plain,
 * saturation too. The next display_gamma puts the player's back. */
void plat_display_plain(void)
{
	screen_saturation_apply(0);
	if (disp_fd >= 0 && panelgamma_sunxi_apply(disp_fd, NULL) == 0)
		fprintf(stderr, "video: the display's gamma table off for the program after this one\n");
}

#else   /* not __linux__ */

/* The host build exists to look at the shelf while changing how it looks
 * (mk/native.mk). There is no codec and no display engine here, so the
 * settings are state and nothing more - enough that the OSD draws and the
 * levels the launcher reports are consistent. */
#define VOL_MAX    PLAT_VOL_MAX
#define BRIGHT_MAX PLAT_BRIGHT_MAX
static int cur_vol = 8, cur_bright = 7;
static int mixer_fd = 0, disp_fd = 0;    /* "present", so the nudges run */
static void levels_save(void) { }
static void apply_volume(int v) { cur_vol = v; }
static void apply_brightness(int b) { cur_bright = b; }
/* No jack on the host, so nothing can be plugged into it. */
void plat_audio_jack_poll(void) { }
/* And no switch to flip. */
bool plat_mute_poll(bool own_volume) { (void)own_volume; return false; }
bool plat_muted(void) { return false; }
bool plat_headphones_present(void) { return false; }
bool plat_usb_audio_present(void) { return false; }
bool plat_cable_link(char *addr, size_t n)
{
#ifdef ZYPEROS_DESK
	if (deskcable_on()) return deskcable_link(addr, n);
#endif
	(void)addr; (void)n;
	return false;
}
void plat_cable_reconnect(void)
{
#ifdef ZYPEROS_DESK
	if (deskcable_on()) deskcable_reconnect();
#endif
}
usb_peer plat_usb_peer(void)
{
#ifdef ZYPEROS_DESK
	if (deskcable_on()) return deskcable_peer();
#endif
	return USB_PEER_UNKNOWN;
}
bool plat_usb_link(int op)
{
#ifdef ZYPEROS_DESK
	if (deskcable_on()) return deskcable_usb_link(op);
#endif
	(void)op;
	return true;
}
int plat_usb_configured(void)
{
#ifdef ZYPEROS_DESK
	if (deskcable_on()) return deskcable_configured();
#endif
	return -1;
}
bool plat_cable_sense(link_sense *out)
{
#ifdef ZYPEROS_DESK
	if (deskcable_on()) return deskcable_sense(out);
#endif
	(void)out;
	return false;
}
bool plat_usb_power(usb_power *out) { (void)out; return false; }
bool plat_usb_power_grant(unsigned plug) { (void)plug; return false; }
bool plat_usb_power_mode(bool automatic) { (void)automatic; return false; }
bool plat_phone_link(bool act, char *ifname, size_t in, char *ip, size_t n)
{
	(void)act; (void)ifname; (void)in; (void)ip; (void)n;
	return false;
}
bool plat_usb_iphone(void) { return false; }
static void jack_forget(void) { }
static void mute_forget(void) { }
void plat_backlight(bool on) { (void)on; }
void plat_backlight_dim(bool dim) { (void)dim; }
void plat_display_plain(void) { }
/* No table on a computer: desk mode keeps what the panel's would hold, for
 * `tools/desk state` (src/device/desk.h) */
void plat_screen_colors(const screen_colors *c, bool keep)
{
#ifdef ZYPEROS_DESK
	desk_note_colors(c, keep);
#else
	(void)c; (void)keep;
#endif
}
void plat_brightness_lift(int lift) { (void)lift; }
void plat_screen_colors_tick(bool now) { (void)now; }
void plat_night(const struct night_cfg *n) { (void)n; }
void plat_night_preview(int warmth) { (void)warmth; }
bool plat_night_tick(void) { return false; }
/* The host draws the Pixel 2's page whole, Saturation too, for the shots */
bool plat_screen_saturation_here(void) { return true; }

/* No settings database on the host, so the config defaults are all there is.
 * Taken anyway rather than ignored: a shelf rendered by --shot should show the
 * OSD at the levels the card is configured for. */
void plat_settings_init(void)
{
	/* The host has no codec and no display engine, so the levels above are
	 * all there is and nothing needs applying to hardware. */
}

#endif  /* __linux__ */

/* Called by the transport the moment a game hands input back: whatever this
 * side remembers about the jack and the switch was formed while something
 * else was driving. */
void device_levels_forget(void)
{
	jack_forget();
	mute_forget();
}

int plat_volume_get(void)     { return cur_vol; }
int plat_brightness_get(void) { return cur_bright; }
#ifdef __linux__
/* The display engine's own value at a rung (DISP_LCD_SET_BRIGHTNESS), for the
 * boot screens (src/boottheme.h); the host has no ladder to give */
int plat_brightness_raw(int level) { return level >= 0 && level <= BRIGHT_MAX ? bright_ladder[level] : -1; }
#else
int plat_brightness_raw(int level) { (void)level; return -1; }
#endif

void plat_volume_nudge(int delta)
{
	if (mixer_fd < 0) return;
	apply_volume(clampi((cur_vol < 0 ? 0 : cur_vol) + delta, 0, VOL_MAX));
	levels_save();
	plat_osd_show(2, cur_vol, VOL_MAX);
}

void plat_brightness_nudge(int delta)
{
	if (disp_fd < 0) return;
	apply_brightness(clampi(cur_bright + delta, 0, BRIGHT_MAX));
	levels_save();
	plat_osd_show(1, cur_bright, BRIGHT_MAX);
}

void plat_volume_set_pct(int pct)
{
	if (mixer_fd < 0) return;
	apply_volume(clampi((pct * VOL_MAX + 50) / 100, 0, VOL_MAX));
	levels_save();
}

void plat_brightness_set(int level)
{
	if (disp_fd < 0) return;
	apply_brightness(clampi(level, 0, BRIGHT_MAX));
	levels_save();
}

/* Both radios are on the Brick's board: the xradio chip carries Wi-Fi and
 * Bluetooth. Not asked of wlan0, as the Pixel 2's adapter is: Wi-Fi Off
 * takes the chip's driver out (plastron's init.d/wpa_supplicant stop), and
 * wlan0 goes with it, so here no wlan0 is only the radio off - and MENU >
 * Wi-Fi, where the player turns it on, shows only where there is Wi-Fi. */
bool plat_has_wifi(void)      { return true; }
/* Bluetooth, CPU shaders (Off and Sharp-Shimmerless, port/brick.c), two
 * speakers, F1 and F2 for brightness, no battery lights and no Player 2:
 * its profile's (src/device/brick_profile.c), or on the host desktop's. */
/* MENU held, for the tutorial's hands, where it is the brightness chord's
 * (the Smart Pro); on the Brick a button of its own, and in_state has it. */
bool plat_menu_held(void) { return menu_chord.down && plat_brightness_on_volume(); }
/* L2 and R2 are buttons of their own too. */
bool plat_tap_held(in_button b) { (void)b; return false; }

side_lights plat_side_lights(void) { return LIGHTS_DEFAULT; }
void plat_side_lights_set(side_lights on) { (void)on; }
void plat_side_lights_say(const char *line) { (void)line; }

/* The A133's DRAM clock is its kernel's, with nothing here to lower. */
void plat_ram_idle(bool idle) { (void)idle; }

/* ---- the chip's temperature ---- */

/* The A133's CPU and GPU sensors, the zones its profile names. Its device
 * tree's passive trips, where the kernel slows the CPU, are at 58 and 63 C,
 * and its critical trip, a power-off, at 90 C, so "Getting hot" (src/heat.h)
 * at 85 C comes a little before that. A number in the test file comes
 * first, as on the Pixel 2 (src/device/pixel2.c), so a host shot or a check
 * can ask for the warning; the host has no zones. */
bool plat_chip_temp(int *mc)
{
	FILE *f = fopen("/tmp/zyperos_fake_temp", "r");
	int v = -1;

	if (f) {
		if (fscanf(f, "%d", &v) != 1) v = -1;
		fclose(f);
		if (v >= 0) {
			*mc = v < 1000 ? v * 1000 : v;
			return true;
		}
	}
#ifdef __linux__
	if ((v = thermal_max_c(plat_profile()->thermal_zones, NULL)) < 0) return false;
	*mc = v * 1000;
	return true;
#else
	return false;
#endif
}

/* ---- charging ---- */

/* The AXP2202's power supplies, as plastron's kernel names them (its patch
 * 0016): the battery's gauge and the USB input */
#define AXP_BATTERY    "/sys/class/power_supply/axp2202-battery"
#define AXP_USB_ONLINE "/sys/class/power_supply/axp2202-usb/online"

#ifdef __linux__
/* A sysfs number, or -1 for no file or nothing in it */
static int read_int(const char *path)
{
	char buf[16];

	return file_head(path, buf, sizeof buf) > 0 ? atoi(buf) : -1;
}

/* The averaged reading where the driver gives one, as rk817's does, else the
 * one of the moment: which of the two the AXP2202's driver has is not
 * measured yet */
static int read_avg_or_now(const char *avg, const char *now)
{
	int v = read_int(avg);

	return v != -1 ? v : read_int(now);
}
#endif

/* No charge limit on the Brick's charger, but the fake gauge has one, for a
 * host shot of MENU > Charge Limit */
bool plat_charge_limit_supported(void)
{
	return access("/tmp/zyperos_fake_charge", F_OK) == 0;
}

void plat_charge_limit_set(int pct)
{
	FILE *f;

	if (access("/tmp/zyperos_fake_charge", F_OK) != 0) return;
	if ((f = fopen("/tmp/zyperos_fake_charge_limit", "w"))) {
		fprintf(f, "%d\n", pct);
		fclose(f);
	}
}

/* The gauge for the charging screen and the corner's time left
 * (src/charging.h), read as the Pixel 2 reads rk817's: the charge, the
 * battery's current and voltage, its charge in mAh where the driver says,
 * the cable, and Full. A charger plugged in leaves the Brick off (its
 * U-Boot's patch 0009), so the screen comes only with POWER or Auto Off on
 * the cable (charge_instead, src/powerscreens.c). The test file comes
 * first, as on the Pixel 2, so a host shot or a check can draw the screen;
 * the host has no gauge. */
bool plat_charge_read(ch_reading *r)
{
	FILE *f = fopen("/tmp/zyperos_fake_charge", "r");
	char line[64] = "";

	if (f) {
		bool ok = fgets(line, sizeof line, f) != NULL && ch_fake_parse(line, r);

		fclose(f);
		if (ok) return true;
	}
#ifdef __linux__
	{
		char st[16] = "";
		int v;

		memset(r, 0, sizeof *r);
		if ((v = read_int(AXP_BATTERY "/capacity")) < 0) return false;
		r->pct = v;
		/* In uA and uV, into the battery positive, as the power_supply
		 * class has them; -1 uA is read_int's no answer, and none either */
		v = read_avg_or_now(AXP_BATTERY "/current_avg", AXP_BATTERY "/current_now");
		r->ma_known = v != -1;
		r->ma = r->ma_known ? v / 1000 : 0;
		r->mv = read_avg_or_now(AXP_BATTERY "/voltage_avg", AXP_BATTERY "/voltage_now") / 1000;
		v = read_int(AXP_BATTERY "/charge_now");
		r->now_mah = v >= 0 ? v / 1000 : -1;
		v = read_int(AXP_BATTERY "/charge_full");
		r->full_mah = v > 0 ? v / 1000 : -1;
		r->online = read_int(AXP_USB_ONLINE) == 1;
		if (file_head(AXP_BATTERY "/status", st, sizeof st) > 0) r->full = !strncmp(st, "Full", 4);
		/* Never held: the Brick has no charge limit, so a "Not charging"
		 * on the cable is not one holding it */
		r->held = false;
		/* Not told: the port's BC1.2 answer is "dedicated charger" for a
		 * computer's USB-A port too (kernel patch 0029), so the screen
		 * says "charger" */
		r->kind = CH_UNKNOWN;
		return true;
	}
#else
	return false;
#endif
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

	if (dir && *dir)
		return dir;
#ifdef __linux__
	return AXP_BATTERY;
#else
	return NULL;
#endif
}

bool plat_battery(int *pct, bool *charging)
{
	FILE *fake = fopen("/tmp/zyperos_fake_batt", "r");
	if (charging) *charging = false;
	if (fake) {
		int v = -1;
		if (fscanf(fake, "%d", &v) != 1) v = -1;
		fclose(fake);
		if (v >= 0) {
			if (pct) *pct = v;
			return true;
		}
	}
#ifdef __linux__
	if (pct) {
		FILE *f = fopen(AXP_BATTERY "/capacity", "r");
		if (!f) return false;
		int v = -1;
		if (fscanf(f, "%d", &v) != 1) v = -1;
		fclose(f);
		if (v < 0) return false;
		*pct = v;
	}
	if (charging) {
		/* The cable, not the battery's status, as on the Pixel 2
		 * (src/charging_rule.h): the USB input's online, which
		 * plat_cable_link reads too. The status is read only where online
		 * gives no answer. */
		int v = read_int(AXP_USB_ONLINE);
		FILE *s;

		if (v == 0 || v == 1) {
			*charging = charging_from(v, NULL);
			return true;
		}
		if ((s = fopen(AXP_BATTERY "/status", "r"))) {
			char st[32] = { 0 };
			bool read = fgets(st, sizeof st, s) != NULL;
			if (read) *charging = charging_from(v, st);
			fclose(s);
			if (read) return true;
		}
	}
	return pct != NULL;
#else
	(void)pct;
	return false;
#endif
}
