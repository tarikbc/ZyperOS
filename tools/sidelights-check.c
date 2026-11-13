/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * What the launcher tells the side lights' gauge (src/sidelights.h): the
 * lines, word for word as plastron's gauge reads them (package/gauge/src/
 * lights.h, lights_line), the FIFO writer that must never make the launcher
 * wait or die, and when a File Transfer batch counts as finished.
 *
 * The FIFO is a real one, in a folder of this run's own, so two runs at once
 * do not meet.
 */
#include "../src/sidelights.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int fails, checks;

#define CHECK(cond, ...) do { \
	checks++; \
	if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

static double ms_since(const struct timespec *t0)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return (t.tv_sec - t0->tv_sec) * 1e3 + (t.tv_nsec - t0->tv_nsec) / 1e6;
}

#define ON(p) (1u << (p))

/* The setting's line, read as plastron's gauge reads it (lights_parse) */
static void setting(void)
{
	char l[96];
	side_lights on;

	CHECK(side_lights_parse(NULL) == LIGHTS_ALL && side_lights_parse("") == LIGHTS_ALL,
	      "no file, or an empty one: every part (My pick: everything on)");
	CHECK(side_lights_parse("auto\n") == LIGHTS_ALL, "today's Auto: every part");
	CHECK(side_lights_parse("charging\n") == ON(LP_BAR), "today's Charging: the bar, and no newer part");
	CHECK(side_lights_parse("low") == ON(LP_LOW), "today's Low Battery: the red flash");
	CHECK(side_lights_parse("off") == 0, "today's Off: nothing");
	CHECK(side_lights_parse("auto -level -cheer") == (ON(LP_BAR) | ON(LP_LOW) | ON(LP_DONE)),
	      "auto with two parts off");
	CHECK(side_lights_parse("off level done") == (ON(LP_LEVEL) | ON(LP_DONE)), "off with two parts on");
	CHECK(side_lights_parse("charging low -bar") == ON(LP_LOW), "a later bar or low word wins");
	CHECK(side_lights_parse("auto shiny -done game") == (LIGHTS_ALL & ~ON(LP_DONE)), "unknown words are left alone");
	CHECK(side_lights_parse("rainbow") == LIGHTS_ALL, "an unknown first word reads as auto");
	CHECK(side_lights_parse("  auto\t-level  \n") == (LIGHTS_ALL & ~ON(LP_LEVEL)), "spaces and tabs");
	CHECK(side_lights_parse("lowish") == LIGHTS_ALL && side_lights_parse("of") == LIGHTS_ALL,
	      "a word is whole, not its start");

	side_lights_line(LIGHTS_ALL, l, sizeof l);
	CHECK(!strcmp(l, "auto level done cheer"), "every part: \"%s\"", l);
	side_lights_line(0, l, sizeof l);
	CHECK(!strcmp(l, "off -level -done -cheer"), "none: \"%s\"", l);
	side_lights_line(ON(LP_BAR) | ON(LP_CHEER), l, sizeof l);
	CHECK(!strcmp(l, "charging -level -done cheer"), "the bar and achievements: \"%s\"", l);
	side_lights_line(ON(LP_LOW), l, sizeof l);
	CHECK(!strncmp(l, "low ", 4), "the red flash alone starts with low, as older gauges read it");
	for (on = 0; on <= LIGHTS_ALL; on++) {
		side_lights_line(on, l, sizeof l);
		CHECK(side_lights_parse(l) == on, "every setting reads back as itself (%u: \"%s\")", on, l);
	}
	CHECK(side_lights_flip(LIGHTS_ALL, LP_DONE) == (LIGHTS_ALL & ~ON(LP_DONE)) &&
	      side_lights_has(ON(LP_LEVEL), LP_LEVEL) && !side_lights_has(ON(LP_LEVEL), LP_BAR), "flip and has");
}

static void lines(void)
{
	char l[64];

	side_lights_level_line(l, sizeof l, true, 12, 20);
	CHECK(!strcmp(l, "level volume 12 20"), "volume: \"%s\"", l);
	side_lights_level_line(l, sizeof l, false, 0, 11);
	CHECK(!strcmp(l, "level brightness 0 11"), "brightness: \"%s\"", l);
	CHECK(!strcmp(SIDE_LIGHTS_DONE, "done") && !strcmp(SIDE_LIGHTS_CHEER, "cheer"), "done and cheer");
	CHECK(!strcmp(SIDE_LIGHTS_FIFO, "/run/gauge"), "the FIFO the gauge makes");
}

static void fifo(void)
{
	char dir[] = "/tmp/sidelights-check.XXXXXX", path[64], buf[256];
	struct timespec t0;
	int rd, wr, n, sent;
	ssize_t got;

	if (!mkdtemp(dir)) { CHECK(0, "mkdtemp: %s", strerror(errno)); return; }
	snprintf(path, sizeof path, "%s/gauge", dir);

	CHECK(!side_lights_say_to(path, "done"), "no FIFO at all: false");
	CHECK(access(path, F_OK) != 0, "and nothing made in its place");

	if (mkfifo(path, 0600) < 0) { CHECK(0, "mkfifo: %s", strerror(errno)); return; }
	clock_gettime(CLOCK_MONOTONIC, &t0);
	CHECK(!side_lights_say_to(path, "done"), "a FIFO nobody reads: false");
	CHECK(ms_since(&t0) < 100, "and at once, not waiting for a reader (%.1f ms)", ms_since(&t0));

	/* The gauge holds it open for reading and writing both */
	rd = open(path, O_RDWR | O_NONBLOCK);
	CHECK(rd >= 0, "open as the gauge does");
	CHECK(side_lights_say_to(path, "level volume 3 20"), "with a reader: sent");
	CHECK(side_lights_say_to(path, SIDE_LIGHTS_DONE), "and another");
	got = read(rd, buf, sizeof buf - 1);
	buf[got > 0 ? got : 0] = '\0';
	CHECK(!strcmp(buf, "level volume 3 20\ndone\n"), "the lines, whole, a newline each: \"%s\"", buf);

	CHECK(!side_lights_say_to(path, ""), "an empty line is not sent");
	memset(buf, 'x', 100);
	buf[100] = '\0';
	CHECK(!side_lights_say_to(path, buf), "a line too long for one write is not sent");
	got = read(rd, buf, sizeof buf - 1);
	CHECK(got < 0 && errno == EAGAIN, "and nothing reached the reader");

	/* A gauge that stopped reading: the pipe fills, and the launcher goes on */
	clock_gettime(CLOCK_MONOTONIC, &t0);
	for (n = 0, sent = 0; n < 20000; n++)
		if (side_lights_say_to(path, "level volume 10 20")) sent++;
	CHECK(sent < 20000, "a full pipe refuses lines (%d of 20000 went)", sent);
	CHECK(ms_since(&t0) < 5000, "without waiting on it (%.0f ms for 20000)", ms_since(&t0));
	/* What went in went whole: every line complete */
	{
		long total = 0, newlines = 0;

		while ((got = read(rd, buf, sizeof buf)) > 0) {
			total += got;
			for (ssize_t i = 0; i < got; i++) newlines += buf[i] == '\n';
		}
		CHECK(total == (long)sent * 19 && newlines == sent, "only whole lines (%ld bytes, %ld lines, %d sent)",
		      total, newlines, sent);
	}

	/* A writer opened while the gauge reads, which then goes: the write
	 * raises SIGPIPE, which must not end this process */
	wr = open(path, O_WRONLY | O_NONBLOCK);
	close(rd);
	CHECK(!side_lights_say_to(path, "done"), "the gauge gone: false");
	if (wr >= 0) close(wr);
	{
		sigset_t pend;

		sigpending(&pend);
		CHECK(!sigismember(&pend, SIGPIPE), "and no SIGPIPE left waiting");
	}

	unlink(path);
	rmdir(dir);
}

/* The SIGPIPE path proper: a write with no reader left. Opened while one
 * reads, then the reader closes before the write; done by hand, as the
 * writer cannot be paused between its open and its write. */
static void sigpipe(void)
{
	char dir[] = "/tmp/sidelights-check.XXXXXX", path[64];
	int rd, wr;

	if (!mkdtemp(dir)) return;
	snprintf(path, sizeof path, "%s/gauge", dir);
	if (mkfifo(path, 0600) < 0) return;
	rd = open(path, O_RDONLY | O_NONBLOCK);
	wr = open(path, O_WRONLY | O_NONBLOCK);
	close(rd);
	/* The same steps side_lights_say_to takes after its open */
	{
		sigset_t p, old, pend;
		int sig;
		ssize_t w;

		sigemptyset(&p);
		sigaddset(&p, SIGPIPE);
		sigprocmask(SIG_BLOCK, &p, &old);
		w = write(wr, "done\n", 5);
		CHECK(w < 0 && errno == EPIPE, "no reader: EPIPE");
		if (!sigpending(&pend) && sigismember(&pend, SIGPIPE)) sigwait(&p, &sig);
		sigprocmask(SIG_SETMASK, &old, NULL);
		sigpending(&pend);
		CHECK(!sigismember(&pend, SIGPIPE), "the signal taken off, and this process alive");
	}
	close(wr);
	unlink(path);
	rmdir(dir);
}

static void batches(void)
{
	side_lights_batch b = { 0 };
	unsigned t = 1000;

	CHECK(!side_lights_batch_done(&b, false, t), "nothing arrived: no done");
	CHECK(!side_lights_batch_done(&b, false, t + 60000), "not even later");
	CHECK(!side_lights_batch_done(&b, true, t), "a file arriving: not yet");
	CHECK(!side_lights_batch_done(&b, false, t + 500), "half a second after: not yet");
	CHECK(!side_lights_batch_done(&b, true, t + 900), "the next file of the batch");
	CHECK(!side_lights_batch_done(&b, false, t + 900 + SIDE_LIGHTS_BATCH_MS - 1), "still under the wait");
	CHECK(side_lights_batch_done(&b, false, t + 900 + SIDE_LIGHTS_BATCH_MS), "the wait after the last file: done");
	CHECK(!side_lights_batch_done(&b, false, t + 9000), "once");
	CHECK(!side_lights_batch_done(&b, true, 0xFFFFFF00u), "a batch across the clock's wrap");
	CHECK(side_lights_batch_done(&b, false, 0xFFFFFF00u + SIDE_LIGHTS_BATCH_MS), "done after the wrap too");
}

/* Files moving on File Transfer's page: a line at most every
 * SIDE_LIGHTS_MOVE_MS while bytes move, the speed over that time, and
 * "move 0 0" once SIDE_LIGHTS_MOVE_IDLE_MS after the last of them */
static void moves(void)
{
	side_lights_move m = { 0 };
	char l[64] = "";
	unsigned t;

	side_lights_move_line(l, sizeof l, 18000000, 0);
	CHECK(!strcmp(l, "move 18000000 0"), "the line, as the gauge reads it: \"%s\"", l);
	side_lights_move_line(l, sizeof l, 0, 600000);
	CHECK(!strcmp(l, "move 0 600000"), "files going out: \"%s\"", l);

	CHECK(!side_lights_move_step(&m, 0, 0, 1000, l, sizeof l), "nothing moves: a line");
	CHECK(!side_lights_move_step(&m, 0, 0, 1600, l, sizeof l), "nothing moves, later: a line");
	CHECK(!side_lights_move_step(&m, 0, 0, 61000, l, sizeof l), "nothing moves, a minute on: a line");
	/* A file arrives at 600 KB/s, a frame every 50 ms */
	CHECK(!side_lights_move_step(&m, 30000, 0, 62000, l, sizeof l), "the first bytes: a line at once");
	for (t = 62050; t < 62500; t += 50)
		CHECK(!side_lights_move_step(&m, 30000, 0, t, l, sizeof l), "a line before the window at %u", t);
	CHECK(side_lights_move_step(&m, 30000, 0, 62500, l, sizeof l) && !strcmp(l, "move 660000 0"),
	      "the window: \"%s\"", l);
	for (t = 62550; t < 63000; t += 50)
		CHECK(!side_lights_move_step(&m, 30000, 0, t, l, sizeof l), "two lines in a window at %u", t);
	CHECK(side_lights_move_step(&m, 30000, 0, 63000, l, sizeof l) && !strcmp(l, "move 600000 0"),
	      "the next window: \"%s\"", l);
	/* A pause of 700 ms between two files: the wave goes on */
	for (t = 63050; t <= 63700; t += 50)
		CHECK(!side_lights_move_step(&m, 0, 0, t, l, sizeof l), "a pause said \"%s\" at %u", l, t);
	CHECK(side_lights_move_step(&m, 0, 2100000, 64000, l, sizeof l) && !strcmp(l, "move 0 4200000"),
	      "a download after the pause: \"%s\"", l);
	/* The last bytes at 64000: "move 0 0" a second on, once */
	for (t = 64050; t < 65000; t += 50)
		CHECK(!side_lights_move_step(&m, 0, 0, t, l, sizeof l), "stopped early at %u: \"%s\"", t, l);
	CHECK(side_lights_move_step(&m, 0, 0, 65000, l, sizeof l) && !strcmp(l, "move 0 0"), "the stop: \"%s\"", l);
	CHECK(!side_lights_move_step(&m, 0, 0, 65500, l, sizeof l) && !side_lights_move_step(&m, 0, 0, 70000, l, sizeof l),
	      "move 0 0 twice");
	CHECK(!side_lights_move_end(&m, l, sizeof l), "an end with nothing moving said \"%s\"", l);
	/* The screen closes while a file arrives: "move 0 0" at once */
	side_lights_move_step(&m, 50000, 0, 71000, l, sizeof l);
	side_lights_move_step(&m, 50000, 0, 71500, l, sizeof l);
	CHECK(side_lights_move_end(&m, l, sizeof l) && !strcmp(l, "move 0 0"), "the end while moving: \"%s\"", l);
	CHECK(!side_lights_move_end(&m, l, sizeof l), "the end said twice");
	/* From the server's counts: a page and its covers are not a transfer;
	 * an upload's last bytes, in the poll that ends it, are */
	memset(&m, 0, sizeof m);
	for (t = 80000; t <= 82000; t += 50)
		CHECK(!side_lights_move_files(&m, 0, 0, 2000, 400000, t, l, sizeof l), "the page lit the lights: \"%s\"", l);
	side_lights_move_files(&m, 1, 0, 100000, 9000, 82050, l, sizeof l);
	CHECK(!side_lights_move_files(&m, 0, 0, 100000, 9000, 82100, l, sizeof l), "a line before the window");
	CHECK(side_lights_move_files(&m, 0, 0, 0, 0, 82550, l, sizeof l) && !strcmp(l, "move 400000 0"),
	      "the upload's last bytes counted, the page's not: \"%s\"", l);
	CHECK(side_lights_move_files(&m, 0, 2, 0, 300000, 83050, l, sizeof l) && !strcmp(l, "move 0 600000"),
	      "a download's bytes out: \"%s\"", l);
	/* Across the clock's wrap */
	memset(&m, 0, sizeof m);
	side_lights_move_step(&m, 0, 0, 0xFFFFFE00u, l, sizeof l);
	side_lights_move_step(&m, 100000, 0, 0xFFFFFF00u, l, sizeof l);
	CHECK(side_lights_move_step(&m, 100000, 0, 0xFFFFFF00u + SIDE_LIGHTS_MOVE_MS, l, sizeof l) &&
	      !strcmp(l, "move 400000 0"), "across the wrap: \"%s\"", l);
}

int main(void)
{
	setting();
	lines();
	fifo();
	sigpipe();
	batches();
	moves();
	printf("sidelights-check: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
