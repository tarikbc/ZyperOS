/* SPDX-License-Identifier: GPL-3.0-only */
/* The launcher's reader of Diatom's answers (src/resident.c), the device
 * stubbed, against a made-up Diatom on a socket of its own: a forked child
 * that says READY and answers each command it is sent with lines it was
 * given. What it holds: plat_resident_sync_line, which the in-game menu's
 * Player 2 page waits on for SETPLAYER2's answer, takes the line it asked
 * for and notes the picture on the way; and a game that ended meanwhile (an
 * EXIT before the answer: px2-open's quit, a crash) is not eaten, but left
 * for the wait loop, whose plat_resident_wait then says the game is over.
 * No SDL window: SDL_GetTicks only. */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../src/device.h"
#include "../src/platform.h"

static int failures;

#define CHECK(c, what) do { if (!(c)) { printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, what); failures++; } \
                            else printf("  ok   %s\n", what); } while (0)

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

/* What it says to the Nth command line it reads, after READY */
static const char *const ANSWERS[] = {
	/* SETPLAYER2 on=1: a picture first, then the answer */
	"PREVIEW\tpath=/tmp/resident-check.png\nPLAYER2\ton=1\tbuttons=0x01FD\tplayers=2\n",
	/* SETPLAYER2 on=1 again: the game ended first */
	"EXIT\treason=user\n",
	/* the third: nothing at all */
	"",
};

static void fake_diatom(int listener)
{
	char buf[512];
	size_t used = 0;
	int c = accept(listener, NULL, NULL), k = 0;

	if (c < 0) _exit(1);
	if (write(c, "READY\tproto=14\tstate=idle\n", 26) != 26) _exit(1);
	for (;;) {
		ssize_t n = read(c, buf + used, sizeof buf - used - 1);
		char *nl;

		if (n <= 0) _exit(0);
		used += (size_t)n;
		while ((nl = memchr(buf, '\n', used))) {
			size_t len = (size_t)(nl - buf) + 1;

			if (k < (int)(sizeof ANSWERS / sizeof ANSWERS[0])) {
				const char *a = ANSWERS[k++];

				if (*a && write(c, a, strlen(a)) != (ssize_t)strlen(a)) _exit(1);
			}
			memmove(buf, buf + len, used - len);
			used -= len;
		}
	}
}

int main(void)
{
	char path[64], out[256];
	struct sockaddr_un a;
	int listener;
	pid_t pid;
	unsigned t0;

	snprintf(path, sizeof path, "/tmp/zyperos-resident-check-%d.sock", (int)getpid());
	unlink(path);
	listener = socket(AF_UNIX, SOCK_STREAM, 0);
	memset(&a, 0, sizeof a);
	a.sun_family = AF_UNIX;
	snprintf(a.sun_path, sizeof a.sun_path, "%s", path);
	if (listener < 0 || bind(listener, (struct sockaddr *)&a, sizeof a) != 0 || listen(listener, 1) != 0) {
		printf("FAIL: no socket at %s\n", path);
		return 1;
	}
	if ((pid = fork()) == 0) fake_diatom(listener);
	close(listener);
	setenv("ZYPEROS_DIATOM_SOCKET", path, 1);

	printf("Diatom's answer to SETPLAYER2, read while paused:\n");
	CHECK(plat_resident_line("SETPLAYER2\ton=1\tbuttons=0x01FD\tplayers=2"), "the line goes");
	CHECK(plat_resident_sync_line("PLAYER2\t", out, sizeof out, 1000) &&
	      !strcmp(out, "PLAYER2\ton=1\tbuttons=0x01FD\tplayers=2"), "the answer is the line asked for");
	CHECK(!strcmp(plat_resident_last_preview(), "/tmp/resident-check.png"),
	      "and the picture on the way is noted, as the other readers note it");

	printf("a game that ended before the answer:\n");
	CHECK(plat_resident_line("SETPLAYER2\ton=1\tbuttons=0x01FD\tplayers=2"), "the line goes");
	t0 = plat_now_ms();
	CHECK(!plat_resident_sync_line("PLAYER2\t", out, sizeof out, 1000), "no answer is taken past its EXIT");
	CHECK(plat_now_ms() - t0 < 500, "and it does not wait out its time for one");
	CHECK(plat_resident_wait() == RES_EXIT, "the EXIT is left for the wait loop, which ends the game");

	printf("nothing at all:\n");
	CHECK(plat_resident_line("SETPLAYER2\ton=0"), "the line goes");
	t0 = plat_now_ms();
	CHECK(!plat_resident_sync_line("PLAYER2\t", out, sizeof out, 150), "no answer");
	CHECK(plat_now_ms() - t0 >= 140, "after the time it was given");

	kill(pid, SIGTERM);
	waitpid(pid, NULL, 0);
	unlink(path);
	if (failures) { printf("%d resident check(s) failed\n", failures); return 1; }
	printf("ok: the resident's answers\n");
	return 0;
}
