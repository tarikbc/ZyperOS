/* SPDX-License-Identifier: GPL-3.0-only */
/* The portable half of the platform layer: what every device shares. The
 * settings the library database holds for games, the input helpers, running
 * and spawning children, the power-off request and the volume and brightness
 * line. The device itself is in src/device/<name>.c, chosen at build time,
 * and the Diatom transport is in resident.c. */
#include "coreopts.h"   /* the defaults under the player's own */
#include "db.h"
#include "device.h"
#include "dslang.h"
#include "i18n.h"
#include "keyrepeat.h" /* hold-to-repeat's 300 ms and 90 ms, which portkeys repeats on too */
#include "platform.h"
#include "spawn.h"

#include "levels.h"
#include "osdbar.h" /* the volume and brightness bar, drawn here and elsewhere */
#include "powerhold.h"
#include "theme.h"  /* which theme the bar is drawn in */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/ioctl.h>
#include <linux/rtc.h>
#endif

/* ---- core options, read once from the library database ------------------- */
/* Lines before any [SECTION] apply to every game; a [TAG] section applies only
 * to that system, keyed on the same tag systems.cfg uses for saves and states.
 *
 * The per-system half exists because mgba_gb_model cannot be set globally.
 * Autodetect is right for Game Boy Color and GBA and wrong only for Game Boy,
 * where it reads the SGB flag and boots a Super Game Boy - a SNES accessory -
 * so a DMG cartridge comes out colorized and framed in a border, on a shelf
 * that says Game Boy. Pinning the model globally would force GBC titles into
 * DMG mode too, and a 0xC0 cartridge would refuse to boot. */
#define COREOPT_MAX 96
static coreopt coreopts[COREOPT_MAX];
static int  ncoreopts = -1;   /* -1 = not read yet */

void plat_coreopts_invalidate(void)
{
	ncoreopts = -1;
}

/* A row of the player's. Refused whole, not stored short: half a key=value
 * pair is still a valid-looking one, and it would be sent to a core as
 * though someone meant it. */
static bool coreopt_row(const char *key, const char *value, void *ctx)
{
	(void)ctx;
	if (ncoreopts >= COREOPT_MAX) return false;
	if (!coreopt_add(coreopts, &ncoreopts, COREOPT_MAX, key, value) && strchr(key + strlen("coreopt."), '.'))
		fprintf(stderr, "coreopts: entry too long, ignoring: %.40s\n", key);
	return true;
}

/* Keys are "coreopt.<tag>.<option>", with an empty tag for a global - so
 * "coreopt..mgba_sgb_borders" is global and "coreopt.GB.mgba_gb_model" is not.
 * The empty segment looks odd and is the point: one prefix scan brings back
 * both kinds, and coreopt_nth still decides precedence. The player's rows
 * first, then the defaults under them (src/coreopts.h): the device's, then
 * every device's. */
static void coreopts_load(void)
{
	const device_profile *p = plat_profile();

	ncoreopts = 0;
	db_each_prefix(db_lib(), "coreopt.", coreopt_row, NULL);
	coreopt_defaults(coreopts, &ncoreopts, COREOPT_MAX, p->coreopts);
}

/* The one opinion that is not a stored setting: a Nintendo DS game's language
 * is the shelf's Game Language, Auto being MENU > Language (src/dslang.h),
 * which can change between two games, so it is made when asked and not read
 * once with the rest. It goes first,
 * so an entry of the same key in the database, sent after it, wins. */
static const char *derived_coreopt(const char *tag)
{
	static char kv[48];
	char word[16];

	if (!tag || strcmp(tag, DSLANG_TAG)) return NULL;
	db_get_str(db_dev(), DSLANG_SETTING, word, sizeof word, "auto");
	snprintf(kv, sizeof kv, "%s=%s", DSLANG_KEY, dslang_option(dslang_parse(word), i18n_lang()));
	return kv;
}

int plat_coreopt_count(const char *tag)
{
	int n = 0;
	if (ncoreopts < 0) coreopts_load();
	while (coreopt_nth(coreopts, ncoreopts, tag, n, NULL)) n++;
	return n + (derived_coreopt(tag) ? 1 : 0);
}

const char *plat_coreopt(const char *tag, int i)
{
	const char *kv = "", *own = derived_coreopt(tag);
	if (ncoreopts < 0) coreopts_load();
	if (own) {
		if (i == 0) return own;
		i--;
	}
	coreopt_nth(coreopts, ncoreopts, tag, i, &kv);
	return kv;
}

/* ---- turbo, read once from the library database -------------------------- */
/* One canonical map per system tag, handed to Diatom after RUN. Diatom makes a
 * pulse a property of a BINDING, so `x:a~3,y:b~3` is the whole feature: X
 * becomes a turbo A and Y a turbo B, three frames pressed and three released.
 *
 * Per system because it is only safe where those two buttons are SPARE. Seven of
 * the eleven consoles here have two face buttons; a Genesis 6-button pad and a
 * SNES pad use X and Y for real, and turbo would take them away.
 *
 * A file rather than a table in the binary because the rate is exactly the sort
 * of thing a player wants to change, and because a remap screen would one day
 * write this same field over the same protocol. */
#define TURBO_MAX 16
static struct { char tag[8]; char map[96]; } turbos[TURBO_MAX];
static int nturbos = -1;                     /* -1 = not read yet */

static bool turbo_row(const char *key, const char *value, void *ctx)
{
	const char *tag = key + strlen("turbo.");

	(void)ctx;
	if (nturbos >= TURBO_MAX) return false;
	/* Refused, not stored short. Half a map is a map nobody wrote, and Diatom
	 * rejects a bad one whole - after the game is already up, where the
	 * refusal is invisible. */
	if (strlen(tag) >= sizeof turbos[0].tag ||
	    strlen(value) >= sizeof turbos[0].map) {
		fprintf(stderr, "turbo: entry too long, ignoring: %.32s\n", tag);
		return true;
	}
	snprintf(turbos[nturbos].tag, sizeof turbos[0].tag, "%s", tag);
	snprintf(turbos[nturbos].map, sizeof turbos[0].map, "%s", value);
	nturbos++;
	return true;
}

static void turbos_load(void)
{
	nturbos = 0;
	db_each_prefix(db_lib(), "turbo.", turbo_row, NULL);
}

/* The map for this system, or NULL for one that wants none. */
const char *plat_turbo_map(const char *tag)
{
	int i;

	if (nturbos < 0) turbos_load();
	if (!tag) return NULL;
	for (i = 0; i < nturbos; i++)
		if (!strcmp(turbos[i].tag, tag)) return turbos[i].map;
	return NULL;
}

void paths_init(void)
{
	const char *v;
	if ((v = getenv("ZYPEROS_ROOT"))) P_ROOT = v;
	if ((v = getenv("ZYPEROS_CARD"))) P_CARD = v;
	if ((v = getenv("ZYPEROS_ROMS"))) P_ROMS = v;
	if ((v = getenv("ZYPEROS_USERDATA"))) P_USERDATA = v;
	if ((v = getenv("ZYPEROS_SHARED"))) P_SHARED = v;
	if ((v = getenv("ZYPEROS_WEB")))    P_WEB = v;
	if ((v = getenv("ZYPEROS_FONT"))) P_FONT = v;
}

unsigned plat_now_ms(void) { return SDL_GetTicks(); }

/* ---- POWER: a press or a hold turns the handheld off (src/powerhold.h) ----
 *
 * One state for the one key, whichever loop reads its events: a screen's
 * plat_input_poll (plat_set_btn below) or the watch over a game or a program
 * (device_power_pressed). in.pressed[IN_POWER] is the turn-off - the press in
 * Press mode, the hold done in Hold mode - which is what every screen asks
 * it for; in.power_tap is the key going down, whatever the mode, for the
 * charging screen, where a press starts the handheld as from off. The hold
 * shows as the settings pill's power kind for as long as it is under way. */
static powerhold g_power;
static bool      g_power_pill;      /* the pill shows this hold */

static void power_pill(void)
{
	int pm = powerhold_permille(&g_power, SDL_GetTicks());

	if (pm >= 0) {
		plat_osd_show(OSDBAR_POWER, pm * OSDBAR_POWER_STEPS / 1000, OSDBAR_POWER_STEPS);
		g_power_pill = true;
	} else if (g_power_pill) {
		g_power_pill = false;
		/* Done, it stays full for its hold and fades as the volume's does;
		 * let go early, it goes at once */
		if (g_power.fired) plat_osd_show(OSDBAR_POWER, OSDBAR_POWER_STEPS, OSDBAR_POWER_STEPS);
		else plat_osd_fade();
	}
}

bool plat_power_event(bool down)
{
	bool off = powerhold_key(&g_power, down, SDL_GetTicks());

	power_pill();
	return off;
}

bool plat_power_due(void)
{
	bool due = powerhold_due(&g_power, SDL_GetTicks());

	power_pill();
	return due;
}

int plat_power_hold(void) { return powerhold_permille(&g_power, SDL_GetTicks()); }
void plat_power_consume(void) { powerhold_consume(&g_power); power_pill(); }

void plat_power_poll(in_state *st)
{
	if (plat_power_due()) st->pressed[IN_POWER] = true;
}

unsigned plat_power_wait(unsigned ms)
{
	/* The pill's next segment, or the end of the hold */
	int left = powerhold_next_ms(&g_power, SDL_GetTicks(), OSDBAR_POWER_STEPS);

	return left >= 0 && (unsigned)left < ms ? (unsigned)left : ms;
}

static unsigned (*g_wait_hook)(unsigned ms);

void plat_on_input_wait(unsigned (*hook)(unsigned ms)) { g_wait_hook = hook; }

unsigned plat_input_wait_hook(unsigned ms)
{
	return g_wait_hook ? g_wait_hook(ms) : ms;
}

void plat_set_btn(in_state *st, in_button b, bool down)
{
	if (b == IN_NONE) return;
	if (b == IN_POWER) {
		if (down && !st->down[b]) {
			st->power_tap = true;
			st->down_since[b] = SDL_GetTicks();
			st->last_repeat[b] = 0;
		}
		st->down[b] = down;
		if (plat_power_event(down)) st->pressed[b] = true;
		return;
	}
	if (down && !st->down[b]) {
		st->pressed[b] = true;
		st->down_since[b] = SDL_GetTicks();
		st->last_repeat[b] = 0;
	}
	st->down[b] = down;
}

/* Set from main's SIGTERM handler. Surfaced here rather than checked at each
 * call site because `quit_requested` is already polled by every loop in the
 * program, so one assignment reaches all of them - including modal loops that
 * were written later and would otherwise have to remember. The keyboard was
 * exactly that case: it checked quit_requested and not main's want_quit, so
 * SIGTERM was ignored while it was open, and a restart from the shell did
 * nothing until the process was killed with -9. */
static volatile sig_atomic_t g_terminating;

void plat_terminate(void) { g_terminating = 1; }
bool plat_terminating(void) { return g_terminating != 0; }

bool in_repeat(in_state *st, in_button b)
{
	if (st->pressed[b]) return true;
	if (!st->down[b]) return false;
	Uint32 now = SDL_GetTicks();
	if (now - st->down_since[b] < REPEAT_DELAY_MS) return false;
	if (now - st->last_repeat[b] < REPEAT_RATE_MS) return false;
	st->last_repeat[b] = now;
	return true;
}

int in_repeat_axis(in_state *st, in_button neg, in_button pos)
{
	bool n = in_repeat(st, neg), p = in_repeat(st, pos);

	return p ? 1 : n ? -1 : 0;
}

/* Whether the last game ended because the player hit power rather than because
 * they quit. The launcher owns the shutdown sequence, so it has to be able to
 * tell the two apart when control comes back. */
static bool run_power_pressed;

bool plat_run_power_pressed(void) { return run_power_pressed; }
void plat_note_power_pressed(void) { run_power_pressed = true; }
void plat_set_power_pressed(bool on) { run_power_pressed = on; }

extern char **environ;

/* The environment a child starts with: this process's, with `envkv` laid over
 * it - each "KEY=VALUE" replacing any KEY already there. Built BEFORE the fork
 * and handed to execve, so between fork and exec the child calls nothing but
 * chdir, setsid, execve, write and _exit.
 *
 * Both forks below used to putenv each pair in the child, and one reported a
 * failed exec with fprintf. Neither is async-signal-safe, which is all POSIX
 * promises a child of a threaded process may call before it execs - and this
 * process has threads, the art resizer among them writing to stderr. Whether
 * glibc's own fork makes those two safe anyway was never checked; building the
 * environment first removes the question.
 *
 * The strings are borrowed, not copied: environ's and the caller's both outlive
 * the exec that uses them. Only the array is allocated, and the parent frees it
 * once the fork is done. */
static char **child_environ(const char *const envkv[])
{
	size_t have = 0, add = 0, i, k, n = 0;
	char **out;

	while (environ && environ[have]) have++;
	while (envkv && envkv[add]) add++;
	if (!(out = malloc((have + add + 1) * sizeof *out))) return NULL;
	for (i = 0; i < have; i++) {
		const char *eq = strchr(environ[i], '=');
		size_t klen = eq ? (size_t)(eq - environ[i]) : strlen(environ[i]);
		bool replaced = false;

		for (k = 0; k < add && !replaced; k++)
			replaced = !strncmp(envkv[k], environ[i], klen) && envkv[k][klen] == '=';
		if (!replaced) out[n++] = environ[i];
	}
	for (k = 0; k < add; k++)
		if (strchr(envkv[k], '=')) out[n++] = (char *)envkv[k];
	out[n] = NULL;
	return out;
}

/* The launcher's slice of a game run the slow way (plat_run_on_tick), and
 * a stop asked of it (plat_run_stop) */
static void (*run_tick)(void);
static volatile bool run_stop;

void plat_run_on_tick(void (*fn)(void)) { run_tick = fn; }
void plat_run_stop(void) { run_stop = true; }

int plat_run(char *const argv[], const char *const envkv[], const char *workdir)
{
	pid_t pid;
	char **env = child_environ(envkv);
	size_t alen = strlen(argv[0]);

	if (!env) return -1;
	run_power_pressed = false;
	run_stop = false;
	pid = fork();
	if (pid < 0) { free(env); return -1; }
	if (pid == 0) {
		ssize_t w;

		if (workdir) {
			if (chdir(workdir) != 0) { /* still try to run */ }
		}
		execve(argv[0], argv, env);
		/* write, not fprintf - see child_environ. */
		w = write(2, "execve ", 7);
		w = write(2, argv[0], alen);
		w = write(2, ": failed\n", 9);
		(void)w;
		_exit(127);
	}
	free(env);
	int status = 0;
#ifdef __linux__
	/* Escape hatch: a core that dead-ends (a bad ROM, a missing BIOS) leaves
	 * the child showing an error screen that eats no input. Watch the power
	 * button while waiting and end the child on a press. */
	device_power_pressed();          /* drain stale events */
	int ms_since_term = -1;
	for (;;) {
		pid_t r = waitpid(pid, &status, WNOHANG);
		if (r == pid) break;
		if (r < 0) return -1;
		usleep(100 * 1000);
		if (run_tick) run_tick();
		if (device_power_pressed() && ms_since_term < 0) {
			run_power_pressed = true;
			kill(pid, SIGTERM);
			ms_since_term = 0;
		}
		/* A quit asked from the launcher's own channel (px2-open): ended as
		 * a power press ends it, TERM and its state written, but not taken
		 * for one */
		if (run_stop && ms_since_term < 0) {
			kill(pid, SIGTERM);
			ms_since_term = 0;
		}
		if (ms_since_term >= 0) {
			ms_since_term += 100;
			if (ms_since_term > 3000) {
				kill(pid, SIGKILL);
				ms_since_term = -1;
			}
		}
	}
#else
	if (waitpid(pid, &status, 0) < 0) return -1;
#endif
	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/* Start a child that outlives this process, and leaves no zombie: double-
 * forked, so the middle child is reaped here and the program is inherited by
 * init (src/spawn.h, spawn_detached). */
long plat_spawn_detached_pid(char *const argv[], const char *const envkv[],
                             const char *workdir)
{
	char **env = child_environ(envkv);
	long pid;

	if (!env) return -1;
	pid = spawn_detached(argv, env, workdir);
	free(env);
	return pid;
}

bool plat_spawn_detached(char *const argv[], const char *const envkv[],
                         const char *workdir)
{
	return plat_spawn_detached_pid(argv, envkv, workdir) > 0;
}

const char *plat_flag(const char *path)
{
	static char buf[4][512];
	static unsigned n;
	const char *dir = getenv("ZYPEROS_FLAG_DIR"), *base = strrchr(path, '/');
	char *b;

	if (!dir || !*dir) return path;
	b = buf[n++ % 4];
	snprintf(b, sizeof buf[0], "%s/%s", dir, base ? base + 1 : path);
	return b;
}

void plat_request_poweroff(void)
{
	FILE *f = fopen(plat_flag(ZYPEROS_POWEROFF_FLAG), "w");
	if (f) fclose(f);
}

void plat_request_reboot(void)
{
	FILE *f = fopen(plat_flag(ZYPEROS_REBOOT_FLAG), "w");
	if (f) fclose(f);
}

/* The settings indicator: the pill src/osdbar.c draws, at the top center on
 * any volume or brightness change - the setting's mark and one segment per
 * step, in the design tokens' colors for volume and for brightness. Diatom
 * shows the same pixels over a game and portkeys over a port, so the
 * feedback is one thing everywhere; this file only places it, fades it, and
 * redraws it when the level moves or the theme changes. It draws in the
 * theme that is on, theme_index(), which src/theme.c also writes to
 * THEME_FILE (src/themefile.h) for Diatom and portkeys, which draw the bar
 * where this process does not, so the three agree.
 *
 * It was a 6 px line across the top, with no mark: the color and the button
 * just pressed were meant to agree. MENU held turns the volume keys to
 * brightness, though, and a warm line in place of what was a cyan one is a
 * hard thing to notice at arm's length; a sun is not. */

static int    osd_kind = 0; /* 0 none, 1 brightness, 2 volume */
static int    osd_val = 0, osd_max = 1;
static Uint32 osd_shown_at = 0, osd_changed_at = 0;
static bool   osd_dirty = true;
static int    osd_theme = -1; /* what osd_tex was painted in; -1 nothing yet */

void plat_osd_show(int kind, int val, int max)
{
	Uint32 now = SDL_GetTicks();
	float a, rise;

	/* Coming in only when it was not already there: a press while it holds
	 * or fades keeps it where it is. */
	if (!osd_kind || !osdbar_envelope(now - osd_shown_at, now - osd_changed_at, &a, &rise))
		osd_shown_at = now;
	if (kind != osd_kind || max != osd_max || val != osd_val) osd_dirty = true;
	osd_kind = kind;
	osd_max = max > 0 ? max : 1;
	osd_val = val < 0 ? 0 : (val > osd_max ? osd_max : val);
	osd_changed_at = now;
}

/* Out at once, as if it had held its time already: POWER let go before its
 * hold was done */
void plat_osd_fade(void)
{
	Uint32 now = SDL_GetTicks();
	float a, rise;

	if (!osd_kind || !osdbar_envelope(now - osd_shown_at, now - osd_changed_at, &a, &rise))
		return;
	if (now - osd_changed_at < OSDBAR_HOLD_MS) osd_changed_at = now - OSDBAR_HOLD_MS;
}

/* The next tick the bar looks different: the coming frame while it slides or
 * fades, the start of the fade while it holds, and POWER's next segment
 * while a hold fills it. */
Uint32 plat_osd_until(void)
{
	Uint32 now = SDL_GetTicks();
	float a, rise;

	if (!osd_kind || !osdbar_envelope(now - osd_shown_at, now - osd_changed_at, &a, &rise))
		return UINT32_MAX;
	/* The boost charging, opening or folding, and the mark following the
	 * level: every frame until it stops (src/levels.c) */
	if (osd_kind != OSDBAR_POWER && levels_moving()) return now;
	if (g_power_pill) {
		Uint32 next = now + osdbar_next_change(now - osd_shown_at, now - osd_changed_at);
		Uint32 step = now + plat_power_wait(OSDBAR_HOLD_MS);

		return step < next ? step : next;
	}
	return now + osdbar_next_change(now - osd_shown_at, now - osd_changed_at);
}

/* The bar's texture and the renderer it belongs to. A renderer is known by
 * its pointer, so one made after the last was destroyed can have the same
 * one: plat_osd_forget is how the device's plat_video_quit says it is gone,
 * before SDL frees the texture with it, or the next frame would update a
 * texture that is no longer there. */
static SDL_Texture *osd_tex;
static SDL_Renderer *osd_tex_for;

void plat_osd_forget(void)
{
	if (osd_tex) SDL_DestroyTexture(osd_tex);
	osd_tex = NULL;
	osd_tex_for = NULL;
}

/* What the launcher draws over every screen, under the bar (plat_set_overlay) */
static void (*g_overlay)(SDL_Renderer *r);
static bool osd_drawn;          /* plat_draw_osd ran since the last present */

void plat_set_overlay(void (*fn)(SDL_Renderer *r)) { g_overlay = fn; }

/* Each device's plat_present first: a frame that did not come through
 * plat_draw_osd (a cleared screen, a loading screen, a power screen) gets
 * the overlay here, last, so it is over everything on every frame */
void plat_overlay_present(SDL_Renderer *r)
{
	if (g_overlay && !osd_drawn) g_overlay(r);
	osd_drawn = false;
}

void plat_draw_osd(SDL_Renderer *r)
{
	if (g_overlay) g_overlay(r);
	osd_drawn = true;
	static uint32_t px[OSDBAR_W * OSDBAR_H];
	static osdbar_look drawn;
	Uint32 now = SDL_GetTicks();
	osdbar_look look;
	float a, rise;

	if (!osd_kind) return;
	if (!osdbar_envelope(now - osd_shown_at, now - osd_changed_at, &a, &rise)) {
		osd_kind = 0;
		levels_bar_gone();
		return;
	}
	if (osd_tex_for != r) {
		if (osd_tex) SDL_DestroyTexture(osd_tex);
		osd_tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC,
		                            OSDBAR_W, OSDBAR_H);
		osd_tex_for = NULL;
		if (!osd_tex) return;
		SDL_SetTextureBlendMode(osd_tex, SDL_BLENDMODE_BLEND);
		osd_tex_for = r;
		osd_dirty = true;
	}
	/* A theme that changed while the bar was up - Auto at night under a
	 * volume press, or the Appearance row itself - repaints it. osdbar_render
	 * would paint the new theme anyway, its own canvas being keyed on it; but
	 * the texture is only uploaded when something is dirty, and without this
	 * the bar would hold the old theme until the level next moved. */
	if (theme_index() != osd_theme) osd_dirty = true;
	/* The level's bar as the boost has it now (src/levels.c), moving while
	 * it charges or opens; POWER's as it was */
	if (!levels_look(osd_kind, &look)) osdbar_look_plain(&look, osd_kind, osd_val, osd_max);
	if (memcmp(&look, &drawn, sizeof look)) osd_dirty = true;
	if (osd_dirty) {
		osd_theme = theme_index();
		osdbar_render_look(px, OSDBAR_W, OSDBAR_H, OSDBAR_W, 1.0f, osd_theme, &look, 1.0f, false);
		SDL_UpdateTexture(osd_tex, NULL, px, OSDBAR_W * 4);
		drawn = look;
		osd_dirty = false;
	}
	SDL_SetTextureAlphaMod(osd_tex, (Uint8)(a * 255 + 0.5f));
	SDL_RenderCopyF(r, osd_tex, NULL, &(SDL_FRect){ OSDBAR_X, OSDBAR_Y - rise, OSDBAR_W, OSDBAR_H });
}

/* ---- Date & Time ---------------------------------------------------------- */

bool plat_clock_set(time_t t)
{
	struct timeval tv = { .tv_sec = t, .tv_usec = 0 };

	if (settimeofday(&tv, NULL) != 0) {
		fprintf(stderr, "clock: settimeofday: %s\n", strerror(errno));
		return false;
	}
#ifdef __linux__
	/* The hardware clock keeps UTC, as the kernel reads it back at boot
	 * (CONFIG_RTC_HCTOSYS). Without this the time set here would last until
	 * the next power-off - on the Pixel 2, back to whatever the clock held
	 * before, 2018 on one never set. */
	{
		struct tm tm;
		struct rtc_time rt;
		int fd = open("/dev/rtc0", O_WRONLY | O_CLOEXEC);

		if (fd < 0) {
			fprintf(stderr, "clock: no hardware clock: %s\n", strerror(errno));
			return true;
		}
		gmtime_r(&t, &tm);
		memset(&rt, 0, sizeof rt);
		rt.tm_year = tm.tm_year; rt.tm_mon = tm.tm_mon; rt.tm_mday = tm.tm_mday;
		rt.tm_hour = tm.tm_hour; rt.tm_min = tm.tm_min; rt.tm_sec = tm.tm_sec;
		if (ioctl(fd, RTC_SET_TIME, &rt) != 0)
			fprintf(stderr, "clock: hardware clock: %s\n", strerror(errno));
		close(fd);
	}
#endif
	return true;
}

void plat_clock_zone(const char *id)
{
	if (!id || !id[0]) return;
	setenv("TZ", id, 1);
	tzset();
}
