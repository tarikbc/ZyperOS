/* SPDX-License-Identifier: GPL-3.0-only */
/* Desk mode (desk.h): the host build as a handheld in a window. */
#include "desk.h"

#include "../device.h"
#include "deskface.h"
#include "keymap.h"
#include "profile.h"

#include <SDL_image.h>
#include <SDL_ttf.h>
#ifdef __APPLE__
#include <CoreGraphics/CoreGraphics.h>
#endif

#include "../../diatom/include/diatom_deskfb.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

static SDL_Window   *g_win;
static SDL_Renderer *g_ren;
static SDL_Texture  *g_screen;      /* ZyperOS's 1024x768 */
static SDL_Texture  *g_panel;       /* the panel's pixels, when not the screen's */
static int           g_pw, g_ph;    /* the panel, landscape */
static float         g_zoom;        /* window points a panel pixel (0 until set) */
static bool          g_face = true; /* the handheld drawn around the screen */
static bool          g_help;
static double        g_hz = 60;

/* The buttons held by desk mode's own ways in (keys, mouse, controller,
 * socket), for the bar to light */
static Uint32 g_held;

/* A game: Diatom's frames in the window, the buttons to Diatom
 * (diatom_deskfb.h). From the launcher's device_display_release to its
 * device_display_take, as a handheld's panel goes to Diatom and back. */
static bool           g_game;
static diatom_deskfb *g_fb;
static size_t         g_fb_bytes;
static SDL_Texture   *g_game_tex;
static uint32_t       g_fb_seq;          /* the last frame shown */
static uint32_t       g_power_edges;     /* POWER's downs (1) and ups (2), queued */
static int            g_npower;

/* ---- what this run is ---------------------------------------------------- */

bool desk_on(void)
{
	static int on = -1;

	if (on < 0) {
		const char *drv = getenv("SDL_VIDEODRIVER");

		on = !getenv("ZYPEROS_HEADLESS") &&
		     !(drv && (!strcmp(drv, "dummy") || !strcmp(drv, "offscreen")));
		/* tools/desk-check.py: desk mode in SDL's dummy window */
		if (getenv("ZYPEROS_DESK_FORCE")) on = 1;
	}
	return on;
}

SDL_Texture *desk_screen(void) { return desk_on() ? g_screen : NULL; }

in_button desk_key_button(SDL_Scancode sc) { return keymap_button(sc); }

/* ---- the window: the handheld around its screen (deskface.h) ------------- */

/* Desk mode's own events (SDL_RegisterEvents): the socket's lines, a tap's
 * let-go, a redraw (enum below) */
static Uint32 g_ctl_event = (Uint32)-1;

static bool      g_has_face;          /* this handheld has a drawing */
static float     g_s = 1;             /* the face's points a millimetre */
static float     g_ox, g_oy;          /* its top left in the window, points */
static SDL_FRect g_scr;               /* the screen's picture in the window, points */
static in_button g_hover = IN_NONE;   /* the key under the pointer */
static bool      g_hints;             /* every key's name: Option held */
static Uint32    g_note_until;        /* the first note's end, SDL ticks */

static bool face_on(void) { return g_face && g_has_face; }

/* The display's pixels a point, as of the window's start or its last move
 * to another display: what the zoom counts in (a resize's renderer can
 * report its old size for a moment) */
static float g_ratio = 1;

static float pixel_ratio(void)
{
	int ww, wh, pw, ph;

	SDL_GetWindowSize(g_win, &ww, &wh);
	SDL_GetRendererOutputSize(g_ren, &pw, &ph);
	return ww > 0 ? (float)pw / ww : 1;
}

/* Where everything is for the window's size now: the face as large as fits,
 * in the middle; or the screen alone */
static void layout(void)
{
	int ww, wh;

	SDL_GetWindowSize(g_win, &ww, &wh);
	if (face_on()) {
		float w1, h1, fw, fh;
		SDL_FRect sc;

		face_size(1, &w1, &h1, &sc);
		g_s = fminf(ww / w1, wh / h1);
		face_size(g_s, &fw, &fh, &g_scr);
		g_ox = (ww - fw) / 2;
		g_oy = (wh - fh) / 2;
		g_scr.x += g_ox;
		g_scr.y += g_oy;
	} else {
		float k = fminf((float)ww / g_pw, (float)wh / g_ph);

		g_ox = g_oy = 0;
		g_scr = (SDL_FRect){ (ww - g_pw * k) / 2, (wh - g_ph * k) / 2, g_pw * k, g_ph * k };
	}
}

/* The window for the zoom: the panel g_zoom points a pixel, and the face
 * around it at the same scale */
static void window_fit(void)
{
	float w = g_pw * g_zoom, h = g_ph * g_zoom;

	if (face_on()) {
		SDL_FRect sc;
		face_size(g_pw * g_zoom / face_screen_mm(), &w, &h, &sc);
	}
	SDL_SetWindowSize(g_win, (int)(w + 0.5f), (int)(h + 0.5f));
	layout();
}

/* Points a millimetre on the display the window is on */
static float display_ppmm(void)
{
#ifdef __APPLE__
	int x, y, w, h;
	CGDirectDisplayID id;
	uint32_t n = 0;

	SDL_GetWindowPosition(g_win, &x, &y);
	SDL_GetWindowSize(g_win, &w, &h);
	if (CGGetDisplaysWithPoint(CGPointMake(x + w / 2.0, y + h / 2.0), 1, &id, &n) == kCGErrorSuccess && n) {
		CGSize mm = CGDisplayScreenSize(id);
		CGRect pts = CGDisplayBounds(id);
		if (mm.width > 0) return (float)(pts.size.width / mm.width);
	}
#endif
	{
		float hdpi = 0;
		int d = SDL_GetWindowDisplayIndex(g_win);

		if (d >= 0 && SDL_GetDisplayDPI(d, NULL, &hdpi, NULL) == 0 && hdpi > 0)
			return hdpi / pixel_ratio() / 25.4f;
	}
	return 72 / 25.4f;
}

/* The panel at its own size on the display: its lit width (the profile's)
 * in the display's points */
static float actual_zoom(void)
{
	const device_profile *p = plat_profile();

	return p ? p->screen.active_w_mm * display_ppmm() / g_pw : 1;
}

/* ---- the help, and the first note ------------------------------------------ */

typedef struct { const char *keys, *what; } help_row;
static const help_row HELP_KEYS[] = {
	{ "Arrows", "the d-pad" }, { "X   Z", "A   B" }, { "S   A", "X   Y" },
	{ "Q   W", "L1   R1" }, { "E   R", "L2   R2" }, { "Return", "START" },
	{ "Right Shift, Delete", "SELECT" }, { "Esc, M", "MENU" },
	{ "-   =", "volume down, up" }, { "[   ]", "brightness keys" }, { "P", "POWER" },
};
static const help_row HELP_WINDOW[] = {
	{ "Option", "the keys on the buttons" },
	{ "Cmd 1, 2, 3", "pixel for pixel, twice, three times" },
	{ "Cmd 0", "the panel's real size" },
	{ "Cmd B", "the screen alone, or the handheld" },
	{ "Cmd S", "a screenshot, to the Desktop" },
	{ "F1", "this card" },
};

static void draw_help(SDL_Renderer *r, float ratio)
{
	const SDL_Color title = { 245, 245, 247, 255 }, key = { 236, 236, 240, 255 },
	                what = { 168, 170, 178, 255 }, head = { 130, 132, 140, 255 };
	const int nk = (int)(sizeof HELP_KEYS / sizeof HELP_KEYS[0]), nw = (int)(sizeof HELP_WINDOW / sizeof HELP_WINDOW[0]);
	float row = 19, cw = 440, ch = 56 + (nk + nw) * row + 2 * 30 + 44, k;
	int ww, wh;

	SDL_GetWindowSize(g_win, &ww, &wh);
	k = fminf(1, fminf((ww - 24) / cw, (wh - 24) / ch));
	SDL_FRect card = { (ww - cw * k) / 2, (wh - ch * k) / 2, cw * k, ch * k };

	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
	face_rrect(r, (SDL_FRect){ 0, 0, (float)ww, (float)wh }, 0, (SDL_Color){ 0, 0, 0, 110 },
	           (SDL_Color){ 0, 0, 0, 150 }, ratio);
	face_shadow(r, card, 14 * k, 26 * k, 8 * k, 120, ratio);
	face_rrect(r, card, 14 * k, (SDL_Color){ 44, 45, 51, 250 }, (SDL_Color){ 30, 31, 35, 250 }, ratio);
	float y = card.y + 30 * k, left = card.x + 30 * k, mid = card.x + 200 * k;
	face_text(r, "Desk mode", 17 * k, title, card.x + card.w / 2, y, ratio, true);
	y += 34 * k;
	face_text(r, "THE BUTTONS", 10 * k, head, left + face_text_w("THE BUTTONS", 10 * k, ratio, true) / 2, y, ratio, true);
	y += row * k;
	for (int i = 0; i < nk; i++, y += row * k) {
		face_text(r, HELP_KEYS[i].keys, 12 * k, key, left + face_text_w(HELP_KEYS[i].keys, 12 * k, ratio, true) / 2, y, ratio, true);
		face_text(r, HELP_KEYS[i].what, 12 * k, what, mid + face_text_w(HELP_KEYS[i].what, 12 * k, ratio, false) / 2, y, ratio, false);
	}
	y += 10 * k;
	face_text(r, "THE WINDOW", 10 * k, head, left + face_text_w("THE WINDOW", 10 * k, ratio, true) / 2, y, ratio, true);
	y += row * k;
	for (int i = 0; i < nw; i++, y += row * k) {
		face_text(r, HELP_WINDOW[i].keys, 12 * k, key, left + face_text_w(HELP_WINDOW[i].keys, 12 * k, ratio, true) / 2, y, ratio, true);
		face_text(r, HELP_WINDOW[i].what, 12 * k, what, mid + face_text_w(HELP_WINDOW[i].what, 12 * k, ratio, false) / 2, y, ratio, false);
	}
	y += 12 * k;
	face_text(r, "The mouse holds a key. The wheel is Up and Down.", 11 * k, what, card.x + card.w / 2, y, ratio, false);
	face_text(r, "A controller's buttons go by place: A right, B bottom.", 11 * k, what, card.x + card.w / 2,
	          y + 16 * k, ratio, false);
}

/* A note at the window's foot, the first seconds: where the keys are */
static void draw_note(SDL_Renderer *r, float ratio)
{
	const char *text = "Hold Option to see the keys   \xC2\xB7   F1 for help";
	int ww, wh;
	float pt = 11, w = face_text_w(text, pt, ratio, true) + 28, h = 26;

	SDL_GetWindowSize(g_win, &ww, &wh);
	SDL_FRect chip = { (ww - w) / 2, wh - h - 14, w, h };
	face_shadow(r, chip, h / 2, 10, 3, 90, ratio);
	face_rrect(r, chip, h / 2, (SDL_Color){ 40, 41, 47, 238 }, (SDL_Color){ 26, 27, 31, 238 }, ratio);
	face_text(r, text, pt, (SDL_Color){ 240, 240, 244, 255 }, chip.x + chip.w / 2, chip.y + chip.h / 2, ratio, true);
}

/* ---- presenting ----------------------------------------------------------- */

static Uint64 g_last_present;
static double g_present_ms[128];   /* a ring of the presents' times */
static unsigned g_presents;

static double now_ms(void)
{
	return SDL_GetPerformanceCounter() * 1000.0 / SDL_GetPerformanceFrequency();
}

/* The panel's 60 a second, on a display that may do 120: wait until a
 * frame's time has gone by since the last, less a little, and let vsync put
 * the present on the display's next refresh */
static void pace(void)
{
	Uint64 f = SDL_GetPerformanceFrequency(), now = SDL_GetPerformanceCounter();
	double period, early;

	if (g_hz <= 0 || !g_last_present) return;
	period = f / g_hz;
	early = f * 0.002;
	if ((double)(now - g_last_present) < period - early) {
		Uint32 ms = (Uint32)(((double)g_last_present + period - early - (double)now) * 1000.0 / f);
		if (ms) SDL_Delay(ms);
	}
}

/* Press to photon, as desk mode can see it: from a press (its event's time,
 * or the socket's line) to the end of the first present after the launcher
 * read it */
static double g_press_t0 = -1;     /* a press waiting for its frame */
static bool   g_press_read;        /* ...which the launcher has read */
static double g_lat[16];
static unsigned g_nlat;

static void deferred_frame(void);

static bool g_dirty;    /* desk mode's own picture changed: help, lights, zoom */

/* The window's picture: the screen, the bar, the help, presented. From the
 * launcher's present, or from desk mode itself when only its own parts
 * changed while the launcher rests (the screen texture holds the last whole
 * frame then) */
/* A shot of the whole window asked for over the socket (`shot FILE window`):
 * taken as the next frame is drawn, then answered */
static char g_wshot[512];
static int  g_wshot_fd = -1;
static void reply(int fd, const char *fmt, ...);

static void window_shot(SDL_Renderer *r)
{
	int w = 0, h = 0;
	SDL_Surface *s;
	bool ok = false;

	SDL_GetRendererOutputSize(r, &w, &h);
	if ((s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32))) {
		if (SDL_RenderReadPixels(r, NULL, SDL_PIXELFORMAT_RGBA32, s->pixels, s->pitch) == 0) {
			for (int i = 0; i < w * h; i++) ((Uint8 *)s->pixels)[i * 4 + 3] = 255;
			ok = IMG_SavePNG(s, g_wshot) == 0;
		}
		SDL_FreeSurface(s);
	}
	reply(g_wshot_fd, "{\"ok\":%s,\"file\":\"%s\",\"w\":%d,\"h\":%d}", ok ? "true" : "false", g_wshot, w, h);
	g_wshot[0] = 0;
	g_wshot_fd = -1;
}

static void compose(SDL_Renderer *r, bool launcher)
{
	Uint8 cr, cg, cb, ca;
	SDL_BlendMode bm;
	SDL_Texture *src = g_game && g_game_tex ? g_game_tex : g_screen;
	float ratio = pixel_ratio();
	SDL_Rect d = { (int)(g_scr.x * ratio + 0.5f), (int)(g_scr.y * ratio + 0.5f),
	               (int)(g_scr.w * ratio + 0.5f), (int)(g_scr.h * ratio + 0.5f) };
	int ww, wh;
	SDL_Rect face_vp;

	SDL_GetWindowSize(g_win, &ww, &wh);
	face_vp = (SDL_Rect){ (int)(g_ox * ratio + 0.5f), (int)(g_oy * ratio + 0.5f),
	                      (int)((ww - 2 * g_ox) * ratio + 0.5f), (int)((wh - 2 * g_oy) * ratio + 0.5f) };

	SDL_GetRenderDrawColor(r, &cr, &cg, &cb, &ca);
	SDL_GetRenderDrawBlendMode(r, &bm);
	/* Down to the panel's own pixels, as the Pixel 2 scales its screen (a
	 * game's frame is the panel's size already) */
	if (g_panel && src == g_screen) {
		SDL_SetRenderTarget(r, g_panel);
		SDL_RenderCopy(r, g_screen, NULL, NULL);
		src = g_panel;
	}
	SDL_SetRenderTarget(r, NULL);
	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
	SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
	SDL_RenderClear(r);
	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
	/* The handheld on a dark desk, lit from above */
	if (face_on()) {
		face_rrect(r, (SDL_FRect){ 0, 0, (float)ww, (float)wh }, 0, (SDL_Color){ 46, 47, 52, 255 },
		           (SDL_Color){ 26, 27, 30, 255 }, ratio);
		SDL_RenderSetViewport(r, &face_vp);
		face_draw(r, g_s, ratio, g_held);
		SDL_RenderSetViewport(r, NULL);
	}
	/* Each panel pixel a block of the window's when the zoom is whole */
	{
		float k = (float)d.w / g_pw;
		SDL_SetTextureScaleMode(src, k - (int)(k + 0.01f) < 0.01f ? SDL_ScaleModeNearest : SDL_ScaleModeLinear);
	}
	SDL_RenderCopy(r, src, NULL, &d);
	if (face_on()) {
		SDL_RenderSetViewport(r, &face_vp);
		face_draw_over(r, g_s, ratio, g_hover, g_hints);
		SDL_RenderSetViewport(r, NULL);
	}
	if (SDL_GetTicks() < g_note_until && !g_help) draw_note(r, ratio);
	if (g_help) draw_help(r, ratio);
	/* A game's frames come at the core's own rate, Diatom's pacing: each
	 * goes up as it comes */
	if (!g_game) pace();
	if (g_wshot[0] && g_wshot_fd >= 0) window_shot(r);
	SDL_RenderPresent(r);
	g_dirty = false;
	g_last_present = SDL_GetPerformanceCounter();
	if (launcher) {
		double t = now_ms();

		g_present_ms[g_presents++ % 128] = t;
		if (g_press_t0 >= 0 && g_press_read) {
			g_lat[g_nlat++ % 16] = t - g_press_t0;
			g_press_t0 = -1;
			g_press_read = false;
		}
	}
	SDL_SetRenderTarget(r, g_screen);
	SDL_SetRenderDrawBlendMode(r, bm);
	SDL_SetRenderDrawColor(r, cr, cg, cb, ca);
	if (launcher) deferred_frame();
}

void desk_present(SDL_Renderer *r) { compose(r, true); }

/* ---- the window ----------------------------------------------------------- */

static void ctl_start(void);

/* The first note's end: a redraw, the launcher at rest or not */
static Uint32 note_timer(Uint32 ms, void *arg)
{
	SDL_Event e;

	(void)ms; (void)arg;
	SDL_zero(e);
	e.type = g_ctl_event;
	e.user.code = 2;            /* CTL_REDRAW */
	SDL_PushEvent(&e);
	return 0;
}

bool desk_video_init(SDL_Window **win, SDL_Renderer **ren)
{
	const device_profile *p = plat_profile();
	const char *e;
	char title[96];

	float want = 0;

	g_pw = p->screen.turn % 180 ? p->screen.panel_h : p->screen.panel_w;
	g_ph = p->screen.turn % 180 ? p->screen.panel_w : p->screen.panel_h;
	/* The zoom asked for: the display's pixels a panel pixel */
	if ((e = getenv("ZYPEROS_DESK_ZOOM")) && atof(e) >= 0.25 && atof(e) <= 6) want = (float)atof(e);
	if ((e = getenv("ZYPEROS_DESK_FRAME"))) g_face = atoi(e) != 0;
	if ((e = getenv("ZYPEROS_DESK_HZ"))) g_hz = atof(e);
	g_has_face = face_init(p->model_borrowed ? NULL : p->model);
	snprintf(title, sizeof title, "ZyperOS - %s", p->name);

	/* A controller's buttons by their place, never their printed letters:
	 * A on the right, B at the bottom, as the handheld has them */
	SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");
	if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0)
		fprintf(stderr, "desk: no game controllers (%s)\n", SDL_GetError());
	if (!TTF_WasInit()) TTF_Init();

	g_win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, g_pw / 2, g_ph / 2,
	                         SDL_WINDOW_HIDDEN | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);
	if (!g_win) {
		fprintf(stderr, "window: %s\n", SDL_GetError());
		return false;
	}
	g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC |
	                                      SDL_RENDERER_TARGETTEXTURE);
	/* No GPU (SDL's dummy window, a check): drawn in software */
	if (!g_ren) g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_SOFTWARE | SDL_RENDERER_TARGETTEXTURE);
	if (!g_ren) {
		fprintf(stderr, "renderer: %s\n", SDL_GetError());
		return false;
	}
	g_screen = SDL_CreateTexture(g_ren, PLAT_TARGET_FORMAT, SDL_TEXTUREACCESS_TARGET,
	                             ZYPEROS_SCREEN_W, ZYPEROS_SCREEN_H);
	if (!g_screen) {
		fprintf(stderr, "desk: no screen texture (%s)\n", SDL_GetError());
		return false;
	}
	SDL_SetTextureBlendMode(g_screen, SDL_BLENDMODE_NONE);
	SDL_SetTextureScaleMode(g_screen, SDL_ScaleModeLinear);
	if (g_pw != ZYPEROS_SCREEN_W || g_ph != ZYPEROS_SCREEN_H) {
		g_panel = SDL_CreateTexture(g_ren, PLAT_TARGET_FORMAT, SDL_TEXTUREACCESS_TARGET, g_pw, g_ph);
		if (g_panel) SDL_SetTextureBlendMode(g_panel, SDL_BLENDMODE_NONE);
	}
	SDL_SetWindowMinimumSize(g_win, g_pw / 4, g_ph / 4);
	/* One panel pixel a display pixel unless asked: on a Retina display that
	 * is about the panel's real size, and its pixels exact. Smaller when the
	 * handheld would not fit the display. */
	{
		SDL_Rect usable;
		int d = SDL_GetWindowDisplayIndex(g_win);

		g_ratio = pixel_ratio();
		g_zoom = (want > 0 ? want : 1) / g_ratio;
		window_fit();
		if (want <= 0 && d >= 0 && SDL_GetDisplayUsableBounds(d, &usable) == 0) {
			int ww, wh;
			SDL_GetWindowSize(g_win, &ww, &wh);
			float k = fminf((usable.w - 40) / (float)ww, (usable.h - 60) / (float)wh);
			if (k < 1) { g_zoom *= k; window_fit(); }
		}
		SDL_SetWindowPosition(g_win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
		SDL_ShowWindow(g_win);
		layout();
	}
	g_note_until = SDL_GetTicks() + 7000;
	SDL_AddTimer(7100, note_timer, NULL);
	SDL_SetRenderTarget(g_ren, g_screen);
	SDL_SetRenderDrawColor(g_ren, 0, 0, 0, 255);
	SDL_RenderClear(g_ren);
	*win = g_win;
	*ren = g_ren;
	fprintf(stderr,
	        "desk: %s, panel %dx%d, %.2f display pixels a panel pixel, %s a second. Keys: arrows d-pad,\n"
	        "desk: X Z S A = A B X Y, Q W E R = L1 R1 L2 R2, Return START, Right Shift or Delete SELECT,\n"
	        "desk: Esc or M MENU, - = volume, P power. Option shows them on the keys, F1 lists the rest.\n",
	        p->name, g_pw, g_ph, g_zoom * g_ratio, g_hz > 0 ? "60 frames" : "the display's frames");
	ctl_start();
	return true;
}

void desk_video_quit(void)
{
	face_forget();
	if (g_panel) { SDL_DestroyTexture(g_panel); g_panel = NULL; }
	if (g_screen) { SDL_DestroyTexture(g_screen); g_screen = NULL; }
	g_ren = NULL;
	g_win = NULL;
}

/* The panel as a PNG: what the window shows, at the panel's own pixels */
static bool save_panel(const char *path)
{
	if (g_game && g_fb) {
		/* The game's frame, as the window shows it */
		SDL_Surface *f = SDL_CreateRGBSurfaceWithFormatFrom(g_fb + 1, (int)g_fb->w, (int)g_fb->h, 32,
		                                                    (int)g_fb->w * 4, SDL_PIXELFORMAT_ARGB8888);
		SDL_Surface *o = f ? SDL_ConvertSurfaceFormat(f, SDL_PIXELFORMAT_RGB24, 0) : NULL;
		bool ok = o && IMG_SavePNG(o, path) == 0;

		SDL_FreeSurface(o);
		SDL_FreeSurface(f);
		return ok;
	}
	SDL_Texture *src = g_panel ? g_panel : g_screen, *was = SDL_GetRenderTarget(g_ren);
	SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, g_pw, g_ph, 32, SDL_PIXELFORMAT_RGBA32);
	bool ok = false;

	if (!s) return false;
	if (g_panel) {
		SDL_SetRenderTarget(g_ren, g_panel);
		SDL_RenderCopy(g_ren, g_screen, NULL, NULL);
	}
	SDL_SetRenderTarget(g_ren, src);
	if (SDL_RenderReadPixels(g_ren, NULL, SDL_PIXELFORMAT_RGBA32, s->pixels, s->pitch) == 0) {
		/* The screen keeps no alpha of its own: opaque */
		for (int i = 0; i < g_pw * g_ph; i++) ((Uint8 *)s->pixels)[i * 4 + 3] = 255;
		ok = IMG_SavePNG(s, path) == 0;
	}
	SDL_SetRenderTarget(g_ren, was);
	SDL_FreeSurface(s);
	return ok;
}

static void shot_to_desktop(void)
{
	char path[512], stamp[32];
	time_t t = time(NULL);
	const char *home = getenv("HOME");

	strftime(stamp, sizeof stamp, "%Y-%m-%d at %H.%M.%S", localtime(&t));
	snprintf(path, sizeof path, "%s/Desktop/ZyperOS %s.png", home ? home : ".", stamp);
	fprintf(stderr, "desk: %s %s\n", save_panel(path) ? "saved" : "could not save", path);
}

/* ---- input ------------------------------------------------------------------ */

static void fb_buttons(void);
static uint32_t diatom_buttons(Uint32 held);
static uint32_t g_game_press;    /* a press in a game: Diatom's bit, until a frame used it */

static void set_btn(in_state *st, in_button b, bool down)
{
	if (b == IN_NONE) return;
	if ((g_held >> b & 1) != (Uint32)down) g_dirty = true;
	if (down) {
		if (!(g_held >> b & 1) && g_press_t0 < 0) {
			g_press_t0 = now_ms();
			g_press_read = !g_game;  /* the launcher reads st after this poll */
			/* in a game, the first frame Diatom made with it */
			g_game_press = g_game ? diatom_buttons(1u << b) : 0;
		}
		g_held |= 1u << b;
	} else {
		g_held &= ~(1u << b);
	}
	if (!g_game) {
		plat_set_btn(st, b, down);
	} else if (b != IN_POWER) {
		fb_buttons();
	} else if (g_npower < 16) {
		/* POWER is the launcher's in a game too (device_power_pressed) */
		g_power_edges |= (down ? 1u : 2u) << (2 * g_npower++);
	}
}

static uint32_t diatom_buttons(Uint32 held)
{
	static const struct { in_button b; int bit; } map[] = {
		{ IN_UP, 0 }, { IN_DOWN, 1 }, { IN_LEFT, 2 }, { IN_RIGHT, 3 },
		{ IN_ACCEPT, 4 }, { IN_BACK, 5 }, { IN_X, 6 }, { IN_Y, 7 },
		{ IN_L1, 8 }, { IN_R1, 9 }, { IN_L2, 10 }, { IN_R2, 11 },
		{ IN_SELECT, 12 }, { IN_START, 13 }, { IN_MENU, 14 },
	};
	uint32_t m = 0;

	for (size_t i = 0; i < sizeof map / sizeof map[0]; i++)
		if (held >> map[i].b & 1) m |= 1u << map[i].bit;
	return m;
}

static void fb_buttons(void)
{
	if (g_fb) __atomic_store_n(&g_fb->buttons, g_game ? diatom_buttons(g_held) : 0, __ATOMIC_RELEASE);
}

/* The wheel's taps: down now, up at the next poll */
static Uint32 g_tap_release;
static float  g_wheel;

void desk_poll_begin(in_state *st)
{
	for (int b = 0; b < IN_COUNT; b++)
		if (g_tap_release >> b & 1) set_btn(st, (in_button)b, false);
	g_tap_release = 0;
}

static void tap(in_state *st, in_button b)
{
	set_btn(st, b, true);
	g_tap_release |= 1u << b;
}

/* The mouse on the handheld's keys */
static in_button g_mouse = IN_NONE;

static in_button key_at_point(int x, int y)
{
	return face_on() ? face_hit(g_s, (float)x - g_ox, (float)y - g_oy) : IN_NONE;
}

static bool is_dpad(in_button b) { return b == IN_UP || b == IN_DOWN || b == IN_LEFT || b == IN_RIGHT; }

/* A controller: its buttons by place, its left stick as the d-pad, its
 * triggers as L2 and R2 */
static in_button pad_button(Uint8 b)
{
	switch (b) {
	case SDL_CONTROLLER_BUTTON_B:             return IN_ACCEPT;   /* east */
	case SDL_CONTROLLER_BUTTON_A:             return IN_BACK;     /* south */
	case SDL_CONTROLLER_BUTTON_Y:             return IN_X;        /* north */
	case SDL_CONTROLLER_BUTTON_X:             return IN_Y;        /* west */
	case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  return IN_L1;
	case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return IN_R1;
	case SDL_CONTROLLER_BUTTON_BACK:          return IN_SELECT;
	case SDL_CONTROLLER_BUTTON_START:         return IN_START;
	case SDL_CONTROLLER_BUTTON_GUIDE:         return IN_MENU;
	case SDL_CONTROLLER_BUTTON_MISC1:         return IN_MENU;
	case SDL_CONTROLLER_BUTTON_DPAD_UP:       return IN_UP;
	case SDL_CONTROLLER_BUTTON_DPAD_DOWN:     return IN_DOWN;
	case SDL_CONTROLLER_BUTTON_DPAD_LEFT:     return IN_LEFT;
	case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:    return IN_RIGHT;
	default:                                  return IN_NONE;
	}
}

static void pad_axis(in_state *st, Uint8 axis, Sint16 v)
{
	static bool on[6][2];
	const int press = 16000, let_go = 11000;
	in_button lo = IN_NONE, hi = IN_NONE;

	switch (axis) {
	case SDL_CONTROLLER_AXIS_LEFTX:        lo = IN_LEFT; hi = IN_RIGHT; break;
	case SDL_CONTROLLER_AXIS_LEFTY:        lo = IN_UP;   hi = IN_DOWN;  break;
	case SDL_CONTROLLER_AXIS_TRIGGERLEFT:  hi = IN_L2; break;
	case SDL_CONTROLLER_AXIS_TRIGGERRIGHT: hi = IN_R2; break;
	default: return;
	}
	if (axis >= 6) return;
	if (lo != IN_NONE) {
		bool want = on[axis][0] ? v < -let_go : v < -press;
		if (want != on[axis][0]) { on[axis][0] = want; set_btn(st, lo, want); }
	}
	{
		bool want = on[axis][1] ? v > let_go : v > press;
		if (want != on[axis][1]) { on[axis][1] = want; set_btn(st, hi, want); }
	}
}

/* z: the display's pixels a panel pixel */
static void zoom_to(float z)
{
	if (z < 0.25f) z = 0.25f;
	if (z > 6) z = 6;
	g_zoom = z / g_ratio;
	window_fit();
	g_dirty = true;
	fprintf(stderr, "desk: %.2f display pixels a panel pixel\n", z);
}

static void ctl_event(const SDL_Event *e, in_state *st);
static void ctl_drop(const SDL_Event *e);

bool desk_event(const SDL_Event *e, in_state *st)
{
	switch (e->type) {
	case SDL_KEYDOWN:
	case SDL_KEYUP: {
		SDL_Keymod mod = e->key.keysym.mod;
		bool down = e->type == SDL_KEYDOWN;

		if (mod & (KMOD_GUI | KMOD_CTRL)) {
			/* The window's own keys, Command (Control elsewhere) and a key */
			if (!down || e->key.repeat) return true;
			switch (e->key.keysym.scancode) {
			case SDL_SCANCODE_1: zoom_to(1); break;
			case SDL_SCANCODE_2: zoom_to(2); break;
			case SDL_SCANCODE_3: zoom_to(3); break;
			case SDL_SCANCODE_0: zoom_to(actual_zoom() * g_ratio); break;
			case SDL_SCANCODE_B: g_face = !g_face; window_fit(); g_dirty = true; break;
			case SDL_SCANCODE_S: shot_to_desktop(); break;
			case SDL_SCANCODE_SLASH: g_help = !g_help; g_dirty = true; break;
			default: return false;   /* Cmd Q and the rest are the system's */
			}
			return true;
		}
		if (e->key.keysym.scancode == SDL_SCANCODE_F1) {
			if (down && !e->key.repeat) { g_help = !g_help; g_dirty = true; }
			return true;
		}
		/* Option: every key's name on its button, while held */
		if (e->key.keysym.scancode == SDL_SCANCODE_LALT || e->key.keysym.scancode == SDL_SCANCODE_RALT) {
			if (g_hints != down) { g_hints = down; g_dirty = true; }
			return true;
		}
		if (e->key.repeat) return true;
		set_btn(st, keymap_button(e->key.keysym.scancode), down);
		return true;
	}
	case SDL_MOUSEBUTTONDOWN:
		if (e->button.button != SDL_BUTTON_LEFT) return true;
		g_mouse = key_at_point(e->button.x, e->button.y);
		if (g_mouse != IN_NONE) set_btn(st, g_mouse, true);
		return true;
	case SDL_MOUSEBUTTONUP:
		if (e->button.button == SDL_BUTTON_LEFT && g_mouse != IN_NONE) {
			set_btn(st, g_mouse, false);
			g_mouse = IN_NONE;
		}
		return true;
	case SDL_MOUSEMOTION:
		/* The key under the pointer names its key */
		{
			in_button over = key_at_point(e->motion.x, e->motion.y);
			if (over != g_hover) { g_hover = over; g_dirty = true; }
		}
		/* The d-pad rolls from one way to the next under a held press */
		if (g_mouse != IN_NONE && is_dpad(g_mouse)) {
			in_button now = key_at_point(e->motion.x, e->motion.y);
			if (is_dpad(now) && now != g_mouse) {
				set_btn(st, g_mouse, false);
				g_mouse = now;
				set_btn(st, g_mouse, true);
			}
		}
		return true;
	case SDL_MOUSEWHEEL: {
		float v = e->wheel.preciseY != 0 ? e->wheel.preciseY : (float)e->wheel.y;
		float h = e->wheel.preciseX != 0 ? e->wheel.preciseX : (float)e->wheel.x;

		if (e->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) { v = -v; h = -h; }
		/* A notch is a press; a trackpad's flick is many small steps */
		g_wheel += v;
		while (g_wheel >= 1)  { tap(st, IN_UP);   g_wheel -= 1; }
		while (g_wheel <= -1) { tap(st, IN_DOWN); g_wheel += 1; }
		if (h >= 1) tap(st, IN_RIGHT);
		else if (h <= -1) tap(st, IN_LEFT);
		return true;
	}
	case SDL_CONTROLLERDEVICEADDED: {
		SDL_GameController *c = SDL_GameControllerOpen(e->cdevice.which);
		if (c) fprintf(stderr, "desk: controller \"%s\"\n", SDL_GameControllerName(c));
		return true;
	}
	case SDL_CONTROLLERDEVICEREMOVED: {
		SDL_GameController *c = SDL_GameControllerFromInstanceID(e->cdevice.which);
		if (c) SDL_GameControllerClose(c);
		return true;
	}
	case SDL_CONTROLLERBUTTONDOWN:
	case SDL_CONTROLLERBUTTONUP:
		set_btn(st, pad_button(e->cbutton.button), e->type == SDL_CONTROLLERBUTTONDOWN);
		return true;
	case SDL_CONTROLLERAXISMOTION:
		pad_axis(st, e->caxis.axis, e->caxis.value);
		return true;
	case SDL_WINDOWEVENT:
		if (e->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) layout();
		if (e->window.event == SDL_WINDOWEVENT_DISPLAY_CHANGED) g_ratio = pixel_ratio();
		if (e->window.event == SDL_WINDOWEVENT_LEAVE && g_hover != IN_NONE) g_hover = IN_NONE;
		if (e->window.event == SDL_WINDOWEVENT_SIZE_CHANGED || e->window.event == SDL_WINDOWEVENT_EXPOSED)
			g_dirty = true;
		return true;
	default:
		if (e->type == g_ctl_event) {
			ctl_event(e, st);
			return true;
		}
		return false;
	}
}

void desk_wait(unsigned ms)
{
	/* The launcher rests with a whole frame drawn: show what desk mode
	 * changed meanwhile, then wait. The event stays queued for the poll
	 * that follows. */
	if (g_dirty && g_ren) compose(g_ren, false);
	SDL_WaitEventTimeout(NULL, (int)ms);
}

/* ---- the control socket (tools/desk) ---------------------------------------
 *
 * One command a connection, one JSON line back, as simulator/simctl's: a
 * thread takes the line and hands it to the main thread as an event, which
 * wakes the launcher's wait; the main thread does it between frames and
 * answers. A tap's let-go and a wait for frames come back the same way. */

enum { CTL_LINE, CTL_RELEASE, CTL_REDRAW };

typedef struct { int fd; int frames_left; bool latency; } ctl_wait;
static ctl_wait g_waits[8];
static int g_nwaits;
static int g_listen = -1;

static void reply(int fd, const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(buf, sizeof buf - 1, fmt, ap);
	va_end(ap);
	if (n < 0) n = 0;
	if (n > (int)sizeof buf - 2) n = (int)sizeof buf - 2;
	buf[n++] = '\n';
	if (write(fd, buf, (size_t)n) < 0) { /* the client went */ }
	close(fd);
}

static void *ctl_thread(void *arg)
{
	(void)arg;
	for (;;) {
		int fd = accept(g_listen, NULL, NULL);
		char line[1024];
		size_t used = 0;
		ssize_t n;

		if (fd < 0) {
			if (errno == EINTR) continue;
			return NULL;
		}
		while (used < sizeof line - 1 && (n = read(fd, line + used, sizeof line - 1 - used)) > 0) {
			used += (size_t)n;
			if (memchr(line, '\n', used)) break;
		}
		line[used] = '\0';
		line[strcspn(line, "\r\n")] = '\0';
		SDL_Event e;
		SDL_zero(e);
		e.type = g_ctl_event;
		e.user.code = CTL_LINE;
		e.user.data1 = strdup(line);
		e.user.data2 = (void *)(intptr_t)fd;
		if (!e.user.data1 || SDL_PushEvent(&e) != 1) {
			free(e.user.data1);
			close(fd);
		}
	}
}

static void ctl_start(void)
{
	const char *path = getenv("ZYPEROS_DESK_SOCK");
	struct sockaddr_un a;
	pthread_t t;

	/* Once a run: the window can be made again (a port's menu) */
	if (g_ctl_event != (Uint32)-1) return;
	g_ctl_event = SDL_RegisterEvents(1);
	if (!path || !*path || strlen(path) >= sizeof a.sun_path) return;
	unlink(path);
	g_listen = socket(AF_UNIX, SOCK_STREAM, 0);
	if (g_listen < 0) return;
	memset(&a, 0, sizeof a);
	a.sun_family = AF_UNIX;
	snprintf(a.sun_path, sizeof a.sun_path, "%s", path);
	mode_t old = umask(077);
	int ok = bind(g_listen, (struct sockaddr *)&a, sizeof a) == 0 && listen(g_listen, 8) == 0;
	umask(old);
	if (!ok || pthread_create(&t, NULL, ctl_thread, NULL) != 0) {
		fprintf(stderr, "desk: no control socket at %s (%s)\n", path, strerror(errno));
		close(g_listen);
		g_listen = -1;
		return;
	}
	pthread_detach(t);
	fprintf(stderr, "desk: control socket %s (tools/desk)\n", path);
}

static const char *const BTN_NAMES[IN_COUNT] = {
	[IN_LEFT] = "left", [IN_RIGHT] = "right", [IN_UP] = "up", [IN_DOWN] = "down",
	[IN_ACCEPT] = "a", [IN_BACK] = "b", [IN_X] = "x", [IN_Y] = "y",
	[IN_L1] = "l1", [IN_R1] = "r1", [IN_START] = "start", [IN_SELECT] = "select",
	[IN_MENU] = "menu", [IN_VOLUP] = "volup", [IN_VOLDN] = "voldown",
	[IN_BRIGHTUP] = "brightup", [IN_BRIGHTDN] = "brightdown", [IN_POWER] = "power",
	[IN_L2] = "l2", [IN_R2] = "r2", [IN_L3] = "l3", [IN_R3] = "r3",
};

/* "a+right" as a mask of buttons; 0 when one is no button */
static Uint32 chord(const char *s)
{
	char buf[128], *tok, *save = NULL;
	Uint32 m = 0;

	snprintf(buf, sizeof buf, "%s", s);
	for (tok = strtok_r(buf, "+", &save); tok; tok = strtok_r(NULL, "+", &save)) {
		int b;
		for (b = 0; b < IN_COUNT; b++)
			if (BTN_NAMES[b] && !strcmp(tok, BTN_NAMES[b])) break;
		if (b == IN_COUNT) return 0;
		m |= 1u << b;
	}
	return m;
}

static Uint32 release_timer(Uint32 ms, void *mask)
{
	SDL_Event e;

	(void)ms;
	SDL_zero(e);
	e.type = g_ctl_event;
	e.user.code = CTL_RELEASE;
	e.user.data1 = mask;
	SDL_PushEvent(&e);
	return 0;
}

/* Screen Colors as the panel's table would have them: none asked yet */
static screen_colors g_colors;
static bool g_colors_set, g_colors_kept;

void desk_note_colors(const screen_colors *c, bool keep)
{
	if (!c) return;
	g_colors = *c;
	g_colors_set = true;
	g_colors_kept = keep;
}

/* `state`'s colors: null until the launcher has put any on the panel */
static void colors_json(char *out, size_t cap)
{
	if (!g_colors_set) {
		snprintf(out, cap, "null");
		return;
	}
	snprintf(out, cap, "{\"temp\":%d,\"tint\":%d,\"contrast\":%d,\"saturation\":%d,\"gamma\":%d,\"kept\":%s}",
	         g_colors.temp, g_colors.tint, g_colors.contrast, g_colors.saturation, g_colors.gamma,
	         g_colors_kept ? "true" : "false");
}

static double fps_last_second(void)
{
	double now = now_ms();
	int n = 0;

	for (unsigned i = 0; i < 128 && i < g_presents; i++)
		if (now - g_present_ms[(g_presents - 1 - i) % 128] <= 1000.0) n++;
	return n;
}

static void held_json(char *out, size_t cap)
{
	size_t used = 0;

	out[0] = '\0';
	for (int b = 0; b < IN_COUNT; b++)
		if ((g_held >> b & 1) && BTN_NAMES[b])
			used += (size_t)snprintf(out + used, used < cap ? cap - used : 0, "%s\"%s\"", used ? "," : "", BTN_NAMES[b]);
}

static char g_focus_screen[96] = "", g_focus_name[96] = "";
static int g_focus_cursor = -1;

/* JSON's string: a quote or a backslash escaped, a control byte left out */
static void json_copy(char *out, size_t cap, const char *in)
{
	size_t n = 0;

	for (const char *c = in ? in : ""; *c && n + 2 < cap; c++) {
		if ((unsigned char)*c < 0x20) continue;
		if (*c == '"' || *c == '\\') out[n++] = '\\';
		out[n++] = *c;
	}
	out[n] = '\0';
}

void desk_note_focus(const char *screen, int cursor, const char *name)
{
	json_copy(g_focus_screen, sizeof g_focus_screen, screen);
	g_focus_cursor = cursor;
	json_copy(g_focus_name, sizeof g_focus_name, name);
}

static void ctl_event(const SDL_Event *e, in_state *st)
{
	if (e->user.code == CTL_REDRAW) {
		g_dirty = true;
		return;
	}
	if (e->user.code == CTL_RELEASE) {
		Uint32 m = (Uint32)(uintptr_t)e->user.data1;
		for (int b = 0; b < IN_COUNT; b++) if (m >> b & 1) set_btn(st, (in_button)b, false);
		return;
	}
	char *line = e->user.data1, verb[32] = "", a1[512] = "", a2[64] = "";
	int fd = (int)(intptr_t)e->user.data2;
	sscanf(line, "%31s %511s %63s", verb, a1, a2);

	if (!strcmp(verb, "state")) {
		char held[256], colors[160];
		held_json(held, sizeof held);
		colors_json(colors, sizeof colors);
		reply(fd, "{\"ok\":true,\"device\":\"%s\",\"panel\":[%d,%d],\"zoom\":%.2f,\"frame\":%s,"
		          "\"hz\":%.0f,\"fps\":%.0f,\"held\":[%s],\"screen\":\"%s\",\"cursor\":%d,\"focus\":\"%s\","
		          "\"colors\":%s}",
		      plat_profile()->id, g_pw, g_ph, g_zoom * g_ratio, face_on() ? "true" : "false", g_hz,
		      fps_last_second(), held, g_focus_screen, g_focus_cursor, g_focus_name, colors);
	} else if (!strcmp(verb, "press") || !strcmp(verb, "release") || !strcmp(verb, "tap") ||
	           !strcmp(verb, "latency")) {
		Uint32 m = chord(a1);
		int ms = *a2 ? atoi(a2) : 80;
		if (!m || ms < 1 || ms > 5000) {
			reply(fd, "{\"ok\":false,\"error\":\"buttons: up down left right a b x y l1 r1 l2 r2 select start menu volup voldown brightup brightdown power, joined with +\"}");
		} else {
			bool down = strcmp(verb, "release") != 0;
			if (!strcmp(verb, "latency")) g_press_t0 = -1;    /* this press is the one timed */
			for (int b = 0; b < IN_COUNT; b++) if (m >> b & 1) set_btn(st, (in_button)b, down);
			if (!strcmp(verb, "tap") || !strcmp(verb, "latency"))
				SDL_AddTimer((Uint32)ms, release_timer, (void *)(uintptr_t)m);
			if (!strcmp(verb, "latency") && g_nwaits < 8)
				g_waits[g_nwaits++] = (ctl_wait){ fd, 0, true };
			else
				reply(fd, "{\"ok\":true}");
		}
	} else if (!strcmp(verb, "shot") && *a1 && !strcmp(a2, "window") && g_wshot_fd < 0) {
		/* The whole window, the drawn handheld round the panel: at the next
		 * frame drawn */
		snprintf(g_wshot, sizeof g_wshot, "%s", a1);
		g_wshot_fd = fd;
		g_dirty = true;
	} else if (!strcmp(verb, "shot") && *a1) {
		bool ok = save_panel(a1);
		reply(fd, "{\"ok\":%s,\"file\":\"%s\"}", ok ? "true" : "false", a1);
	} else if (!strcmp(verb, "wait-frames") && atoi(a1) > 0 && g_nwaits < 8) {
		g_waits[g_nwaits++] = (ctl_wait){ fd, atoi(a1), false };
	} else if (!strcmp(verb, "stats")) {
		char lat[256] = "";
		size_t used = 0;
		unsigned n = g_nlat < 16 ? g_nlat : 16;
		for (unsigned i = 0; i < n; i++)
			used += (size_t)snprintf(lat + used, sizeof lat - used, "%s%.1f", i ? "," : "",
			                         g_lat[(g_nlat - n + i) % 16]);
		reply(fd, "{\"ok\":true,\"fps\":%.0f,\"frames\":%u,\"press_to_present_ms\":[%s]}",
		      fps_last_second(), g_presents, lat);
	} else if (!strcmp(verb, "zoom") && atof(a1) > 0) {
		zoom_to((float)atof(a1));
		reply(fd, "{\"ok\":true,\"zoom\":%.2f}", g_zoom * g_ratio);
	} else if ((!strcmp(verb, "frame") || !strcmp(verb, "bar")) && (*a1 == '0' || *a1 == '1')) {
		g_face = *a1 == '1';
		window_fit();
		g_dirty = true;
		reply(fd, "{\"ok\":true}");
	} else if (!strcmp(verb, "hints")) {
		g_hints = *a1 ? *a1 == '1' : !g_hints;
		g_dirty = true;
		reply(fd, "{\"ok\":true,\"hints\":%s}", g_hints ? "true" : "false");
	} else if (!strcmp(verb, "help")) {
		g_help = *a1 ? *a1 == '1' : !g_help;
		g_dirty = true;
		reply(fd, "{\"ok\":true,\"help\":%s}", g_help ? "true" : "false");
	} else if (!strcmp(verb, "quit")) {
		SDL_Event q;
		SDL_zero(q);
		q.type = SDL_QUIT;
		SDL_PushEvent(&q);
		reply(fd, "{\"ok\":true}");
	} else {
		reply(fd, "{\"ok\":false,\"error\":\"state | press|release|tap BUTTON[+BUTTON] [MS] | latency BUTTON | "
		          "shot FILE.png [window] | wait-frames N | stats | zoom N | frame 0|1 | hints [0|1] | help [0|1] | quit\"}");
	}
	free(line);
}

/* A command that could not be kept: answered, never left hanging */
static void ctl_drop(const SDL_Event *e)
{
	if (e->user.code != CTL_LINE) return;
	reply((int)(intptr_t)e->user.data2, "{\"ok\":false,\"error\":\"busy: try again\"}");
	free(e->user.data1);
}

/* After each present: the waits for frames count down, a timed press says
 * how long its frame took */
static void deferred_frame(void)
{
	for (int i = 0; i < g_nwaits; i++) {
		ctl_wait *w = &g_waits[i];
		bool done = false;

		if (w->latency) {
			if (g_press_t0 < 0 && g_nlat) {
				reply(w->fd, "{\"ok\":true,\"press_to_present_ms\":%.1f}", g_lat[(g_nlat - 1) % 16]);
				done = true;
			}
		} else if (--w->frames_left <= 0) {
			reply(w->fd, "{\"ok\":true}");
			done = true;
		}
		if (done) {
			g_waits[i--] = g_waits[--g_nwaits];
		}
	}
}

/* ---- a game ----------------------------------------------------------------- */

static bool fb_map(void)
{
	const char *path = getenv("ZYPEROS_DESK_FB");
	struct stat sb;
	void *m;
	int fd;

	if (g_fb) return true;
	if (!path || !*path || (fd = open(path, O_RDWR | O_CLOEXEC)) < 0) return false;
	if (fstat(fd, &sb) != 0 || (size_t)sb.st_size < sizeof(diatom_deskfb)) { close(fd); return false; }
	m = mmap(NULL, (size_t)sb.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);
	if (m == MAP_FAILED) return false;
	g_fb = m;
	g_fb_bytes = (size_t)sb.st_size;
	if (__atomic_load_n(&g_fb->magic, __ATOMIC_ACQUIRE) != DIATOM_DESKFB_MAGIC ||
	    g_fb->version != DIATOM_DESKFB_VERSION || !g_fb->w || !g_fb->h ||
	    DIATOM_DESKFB_BYTES(g_fb->w, g_fb->h) > g_fb_bytes) {
		fprintf(stderr, "desk: %s is no desk frame file of Diatom's\n", path);
		munmap(m, g_fb_bytes);
		g_fb = NULL;
		return false;
	}
	return true;
}

void desk_game_begin(void)
{
	if (!g_ren || !fb_map()) {
		/* No Diatom of desk mode's (tools/desk runs one): the window keeps
		 * the launcher's last frame, as it always did */
		return;
	}
	if (!g_game_tex) {
		g_game_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
		                               (int)g_fb->w, (int)g_fb->h);
		if (!g_game_tex) return;
		SDL_SetTextureBlendMode(g_game_tex, SDL_BLENDMODE_NONE);
	}
	g_game = true;
	/* The frame already there is the last game's: wait for a new one */
	g_fb_seq = __atomic_load_n(&g_fb->seq, __ATOMIC_ACQUIRE);
	fb_buttons();
}

void desk_game_end(void)
{
	if (!g_game) return;
	g_game = false;
	g_game_press = 0;
	fb_buttons();
	g_dirty = true;
}

int desk_power_edge(void)
{
	int v;

	if (!g_npower) return -1;
	v = (int)(g_power_edges & 3u);
	g_power_edges >>= 2;
	g_npower--;
	return v == 1;
}

unsigned desk_game_wait(unsigned ms)
{
	static in_state none;    /* the launcher reads no buttons in a game */
	SDL_Event e;
	uint32_t seq;

	if (!g_game) return ms;
	while (SDL_PollEvent(&e)) {
		if (e.type == SDL_QUIT) {
			/* The window closed: as POWER, the game saved on its way out */
			if (g_npower < 15) {
				g_power_edges |= (1u | 2u << 2) << (2 * g_npower);
				g_npower += 2;
			}
			continue;
		}
		desk_event(&e, &none);
	}
	seq = __atomic_load_n(&g_fb->seq, __ATOMIC_ACQUIRE);
	if (!(seq & 1u) && seq != g_fb_seq) {
		SDL_UpdateTexture(g_game_tex, NULL, g_fb + 1, (int)g_fb->w * 4);
		/* A frame half copied over meanwhile is shown as it is, once:
		 * the next is whole */
		if (g_game_press && (g_fb->polled & g_game_press)) {
			g_press_read = true;
			g_game_press = 0;
		}
		g_fb_seq = seq;
		compose(g_ren, true);
		return 0;
	}
	if (g_dirty) compose(g_ren, false);
	return ms < 4 ? ms : 4;
}

void desk_flush(void)
{
	SDL_Event e, keep[32];
	int n = 0;

	SDL_PumpEvents();
	while (SDL_PollEvent(&e)) {
		switch (e.type) {
		case SDL_KEYUP:
			if (!(e.key.keysym.mod & (KMOD_GUI | KMOD_CTRL))) {
				in_button b = keymap_button(e.key.keysym.scancode);
				if (b != IN_NONE) g_held &= ~(1u << b);
			}
			break;
		case SDL_MOUSEBUTTONUP:
			if (g_mouse != IN_NONE) g_held &= ~(1u << g_mouse);
			g_mouse = IN_NONE;
			break;
		case SDL_CONTROLLERBUTTONUP: {
			in_button b = pad_button(e.cbutton.button);
			if (b != IN_NONE) g_held &= ~(1u << b);
			break;
		}
		case SDL_QUIT:
		case SDL_CONTROLLERDEVICEADDED:
		case SDL_CONTROLLERDEVICEREMOVED:
		case SDL_WINDOWEVENT:
			if (n < 32) keep[n++] = e;
			break;
		default:
			if (e.type == g_ctl_event && n < 32) keep[n++] = e;
			else if (e.type == g_ctl_event) ctl_drop(&e);
			break;
		}
	}
	g_dirty = true;
	for (int i = 0; i < n; i++) SDL_PushEvent(&keep[i]);
}
