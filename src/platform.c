/* SPDX-License-Identifier: GPL-3.0-only */
/* The portable half of the platform layer: what every device shares. The
 * settings the library database holds for games, the input helpers, running
 * and spawning children, the power-off request and the volume and brightness
 * line. The device itself is in src/device/<name>.c, chosen at build time,
 * and the Diatom transport is in resident.c. */
#include "db.h"
#include "device.h"
#include "dslang.h"
#include "i18n.h"
#include "keyrepeat.h" /* hold-to-repeat's 300 ms and 90 ms, which portkeys repeats on too */
#include "platform.h"

#include "osdbar.h" /* the volume and brightness bar, drawn here and elsewhere */
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
#define COREOPT_MAX 48
static struct { char tag[8]; char kv[192]; } coreopts[COREOPT_MAX];
static int  ncoreopts = -1;   /* -1 = not read yet */

static bool coreopt_row(const char *key, const char *value, void *ctx)
{
	const char *rest = key + strlen("coreopt.");
	const char *dot = strchr(rest, '.');
	char kv[192];

	(void)ctx;
	if (!dot || ncoreopts >= COREOPT_MAX) return ncoreopts < COREOPT_MAX;

	/* Refused, not stored short. Half a key=value pair is still a
	 * valid-looking one, and it would be sent to a core as though someone
	 * meant it. */
	if ((size_t)(dot - rest) >= sizeof coreopts[0].tag ||
	    snprintf(kv, sizeof kv, "%s=%s", dot + 1, value) >= (int)sizeof kv) {
		fprintf(stderr, "coreopts: entry too long, ignoring: %.40s\n", key);
		return true;
	}
	snprintf(coreopts[ncoreopts].tag, sizeof coreopts[0].tag, "%.*s",
	         (int)(dot - rest), rest);
	snprintf(coreopts[ncoreopts].kv, sizeof coreopts[0].kv, "%s", kv);
	ncoreopts++;
	return true;
}

/* Keys are "coreopt.<tag>.<option>", with an empty tag for a global - so
 * "coreopt..mgba_sgb_borders" is global and "coreopt.GB.mgba_gb_model" is not.
 * The empty segment looks odd and is the point: one prefix scan brings back
 * both kinds, and coreopt_nth below still decides precedence. */
static void coreopts_load(void)
{
	ncoreopts = 0;
	db_each_prefix(db_lib(), "coreopt.", coreopt_row, NULL);
}

/* Global entries first, then this tag's, so a system can override a global. */
static int coreopt_nth(const char *tag, int want, const char **out)
{
	int pass, i, seen = 0;
	for (pass = 0; pass < 2; pass++)
		for (i = 0; i < ncoreopts; i++) {
			bool global = coreopts[i].tag[0] == '\0';
			if (pass == 0 ? !global : global) continue;
			if (pass == 1 && (!tag || strcmp(coreopts[i].tag, tag))) continue;
			if (seen++ == want) { if (out) *out = coreopts[i].kv; return 1; }
		}
	return 0;
}

/* The one opinion that is not a stored setting: a Nintendo DS game's language
 * follows MENU > Language (src/dslang.h), which can change between two games,
 * so it is made when asked and not read once with the rest. It goes first,
 * so an entry of the same key in the database, sent after it, wins. */
static const char *derived_coreopt(const char *tag)
{
	static char kv[48];

	if (!tag || strcmp(tag, DSLANG_TAG)) return NULL;
	snprintf(kv, sizeof kv, "%s=%s", DSLANG_KEY, dslang_value(i18n_lang()));
	return kv;
}

int plat_coreopt_count(const char *tag)
{
	int n = 0;
	if (ncoreopts < 0) coreopts_load();
	while (coreopt_nth(tag, n, NULL)) n++;
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
	coreopt_nth(tag, i, &kv);
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

void plat_set_btn(in_state *st, in_button b, bool down)
{
	if (b == IN_NONE) return;
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

int plat_run(char *const argv[], const char *const envkv[], const char *workdir)
{
	pid_t pid;
	char **env = child_environ(envkv);
	size_t alen = strlen(argv[0]);

	if (!env) return -1;
	run_power_pressed = false;
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
		if (device_power_pressed() && ms_since_term < 0) {
			run_power_pressed = true;
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

/* Start a child that outlives this process. Double-forked, so the middle
 * child is reaped here and the grandchild is inherited by init rather than
 * becoming a zombie nobody waits for. */
bool plat_spawn_detached(char *const argv[], const char *const envkv[],
                         const char *workdir)
{
	char **env = child_environ(envkv);
	struct rlimit rl;
	int top = 1024, fd;
	pid_t pid;

	/* EVERYTHING ABOVE STDERR IS CLOSED IN THE CHILD, before it becomes the
	 * program asked for.
	 *
	 * Fork hands a child every descriptor this process has open: the resident
	 * emulator's socket, Muse's, the display, the GPU, four input devices,
	 * the font. A short-lived child drops them when it exits. A DAEMON keeps
	 * them for as long as it lives, and Muse is meant to outlive the launcher.
	 * That froze the Brick on 2026-09-18: Muse held a dead launcher's end of
	 * Diatom's socket, so Diatom never saw it close, and the restarted
	 * launcher blocked in connect before its first frame. Diatom's half is
	 * that the newest connection wins; this is ours.
	 *
	 * By number up to the limit, not by listing /proc/self/fd: listing
	 * allocates, and after fork in a threaded process only async-signal-safe
	 * calls are safe - close is one. The limit is read here, before fork, for
	 * the same reason. 1024 on the Brick, with about thirty open. stdout and
	 * stderr stay: they are the log, and a daemon's lines belong in it. */
	if (getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_cur != RLIM_INFINITY)
		top = rl.rlim_cur < 65536 ? (int)rl.rlim_cur : 65536;

	if (!env) return false;
	pid = fork();
	if (pid < 0) { free(env); return false; }
	if (pid == 0) {
		if (fork() == 0) {
			setsid();
			for (fd = 3; fd < top; fd++) close(fd);
			if (workdir) { if (chdir(workdir) != 0) { /* still try */ } }
			execve(argv[0], argv, env);
			_exit(127);
		}
		_exit(0);
	}
	free(env);
	waitpid(pid, NULL, 0);
	return true;
}

void plat_request_poweroff(void)
{
	FILE *f = fopen(ZYPEROS_POWEROFF_FLAG, "w");
	if (f) fclose(f);
}

void plat_request_reboot(void)
{
	FILE *f = fopen(ZYPEROS_REBOOT_FLAG, "w");
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

/* The next tick the bar looks different: the coming frame while it slides or
 * fades, the start of the fade while it holds. */
Uint32 plat_osd_until(void)
{
	Uint32 now = SDL_GetTicks();
	float a, rise;

	if (!osd_kind || !osdbar_envelope(now - osd_shown_at, now - osd_changed_at, &a, &rise))
		return UINT32_MAX;
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

void plat_draw_osd(SDL_Renderer *r)
{
	static uint32_t px[OSDBAR_W * OSDBAR_H];
	Uint32 now = SDL_GetTicks();
	float a, rise;

	if (!osd_kind) return;
	if (!osdbar_envelope(now - osd_shown_at, now - osd_changed_at, &a, &rise)) {
		osd_kind = 0;
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
	/* A theme that changed while the bar was up - Auto at 19:00 under a
	 * volume press, or the Appearance row itself - repaints it. osdbar_render
	 * would paint the new theme anyway, its own canvas being keyed on it; but
	 * the texture is only uploaded when something is dirty, and without this
	 * the bar would hold the old theme until the level next moved. */
	if (theme_index() != osd_theme) osd_dirty = true;
	if (osd_dirty) {
		osd_theme = theme_index();
		osdbar_render(px, OSDBAR_W, OSDBAR_H, OSDBAR_W, 1.0f, osd_theme, osd_kind,
		              osd_val, osd_max, 1.0f, false);
		SDL_UpdateTexture(osd_tex, NULL, px, OSDBAR_W * 4);
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
