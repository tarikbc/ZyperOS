/* SPDX-License-Identifier: GPL-3.0-only */
/* A game's clock in the zone chosen in Date & Time, from the launcher to
 * Diatom:
 *
 *   make check-diatom     (after Diatom's own checks; needs its desktop build)
 *
 * launch.sh starts Diatom with no zone, so a core's localtime read UTC: a
 * Pokemon game's day and night came at the wrong hours. The launcher holds
 * the zone in TZ (plat_clock_zone) and sends it with every game's RUN
 * (src/resident.c), and Diatom sets it before the core loads. This drives the
 * launcher's own resident.c, with the device stubbed out, against a real
 * Diatom on a socket of its own, and reads Diatom's log: the zone arrives and
 * is in force, the same zone again is no change, a new zone in Date & Time
 * reaches the next game with no restart, and a Diatom started again (as
 * launch.sh does after a crash) has it from its first game. The core is one
 * that does not exist: the zone is set on the way to loading it, before any
 * core could read a clock, so none is needed.
 */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../src/device.h"
#include "../src/platform.h"

static int failures;

#define CHECK(c, ...) do { if (!(c)) { printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); putchar('\n'); failures++; } else { printf("  ok   "); printf(__VA_ARGS__); putchar('\n'); } } while (0)

/* ---- the device, stubbed: resident.c's other neighbours ------------------ */

void device_display_release(void) {}
void device_display_take(void) {}
void device_levels_forget(void) {}
bool device_power_pressed(void) { return false; }
int  plat_brightness_get(void) { return -1; }
void plat_brightness_set(int level) { (void)level; }
int  plat_coreopt_count(const char *tag) { (void)tag; return 0; }
const char *plat_coreopt(const char *tag, int i) { (void)tag; (void)i; return NULL; }
bool plat_mute_poll(bool own_volume) { (void)own_volume; return false; }
bool plat_muted(void) { return false; }
void plat_set_power_pressed(bool on) { (void)on; }
void plat_side_lights_say(const char *line) { (void)line; }
bool plat_terminating(void) { return false; }
const char *plat_turbo_map(const char *tag) { (void)tag; return NULL; }
int  plat_volume_get(void) { return -1; }
void plat_volume_set_pct(int pct) { (void)pct; }
int  side_lights_level_line(char *out, size_t cap, bool volume, int val, int max)
{
	(void)volume; (void)val; (void)max;
	if (cap) out[0] = '\0';
	return 0;
}

unsigned plat_now_ms(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return (unsigned)(t.tv_sec * 1000 + t.tv_nsec / 1000000);
}

/* ---- a Diatom of its own ------------------------------------------------- */

static const char *g_bin;
static char g_sock[64], g_log[64];
static pid_t g_pid = -1;

static void diatom_start(void)
{
	int i;

	unlink(g_sock);
	fflush(stdout);
	if ((g_pid = fork()) == 0) {
		FILE *e = freopen(g_log, "w", stderr);

		(void)e;
		if (!freopen("/dev/null", "w", stdout)) _exit(127);
		/* Diatom's own zone is none, as launch.sh starts it */
		unsetenv("TZ");
		setenv("SDL_VIDEODRIVER", "dummy", 1);
		setenv("SDL_RENDER_DRIVER", "software", 1);
		setenv("SDL_AUDIODRIVER", "dummy", 1);
		execl(g_bin, g_bin, "--socket", g_sock, (char *)NULL);
		_exit(127);
	}
	for (i = 0; i < 100 && access(g_sock, F_OK) != 0; i++) usleep(50000);
	if (access(g_sock, F_OK) != 0) {
		printf("FAIL Diatom (%s) never listened on %s\n", g_bin, g_sock);
		exit(1);
	}
}

static void diatom_stop(void)
{
	if (g_pid > 0) {
		kill(g_pid, SIGTERM);
		waitpid(g_pid, NULL, 0);
	}
	g_pid = -1;
	unlink(g_sock);
}

/* How many times Diatom's log says `needle`, after waiting for its answer */
static int said(const char *needle)
{
	char line[512];
	int n = 0;
	FILE *f;

	if (!(f = fopen(g_log, "r"))) return -1;
	while (fgets(line, sizeof line, f))
		if (strstr(line, needle)) n++;
	fclose(f);
	return n;
}

/* One game, as the launcher starts one with the zone it holds now: Date &
 * Time's plat_clock_zone, then resident.c's RUN, and a moment for Diatom
 * to try the core (it cannot load) and write its log. */
static void game(const char *zone)
{
	if (zone) plat_clock_zone(zone);
	CHECK(plat_resident_send("GBA", "/nonexistent/core.so", "/nonexistent/game.gba",
	                         NULL, NULL, NULL, 0, NULL),
	      "RUN sent (%s)", zone ? zone : "the zone as it was");
	usleep(400000);
}

/* As platform.c's: the zone in TZ, for this process and what it sends */
void plat_clock_zone(const char *id)
{
	if (!id || !id[0]) return;
	setenv("TZ", id, 1);
	tzset();
}

int main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "usage: %s DIATOM\n", argv[0]);
		return 2;
	}
	g_bin = argv[1];
	snprintf(g_sock, sizeof g_sock, "/tmp/gamezone-check-%d.sock", (int)getpid());
	snprintf(g_log, sizeof g_log, "/tmp/gamezone-check-%d.log", (int)getpid());
	setenv("ZYPEROS_DIATOM_SOCKET", g_sock, 1);
	signal(SIGPIPE, SIG_IGN);

	printf("a game's clock in the chosen zone:\n");
	diatom_start();
	game("Asia/Tokyo");
	CHECK(said("time zone Asia/Tokyo, +0900") == 1, "Diatom has the launcher's zone, +0900");
	game(NULL);
	CHECK(said("time zone Asia/Tokyo") == 1, "the same zone again changes nothing");
	game("America/Sao_Paulo");
	CHECK(said("time zone America/Sao_Paulo, -0300") == 1,
	      "a zone chosen in Date & Time reaches the next game, no restart");

	/* Diatom started again, as launch.sh does when it dies: a new process
	 * with no zone, and the launcher's connection to the old one gone */
	diatom_stop();
	diatom_start();
	game(NULL);
	CHECK(said("time zone America/Sao_Paulo, -0300") == 1,
	      "a Diatom started again has the zone from its first game");
	diatom_stop();
	unlink(g_log);

	if (failures) printf("%d FAILED\n", failures);
	else printf("ok: the chosen zone reaches every game's clock\n");
	return failures != 0;
}
