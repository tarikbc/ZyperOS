/* The theme file: what src/themefile.c reads and writes, without a screen.
 *
 * The launcher is the one process that knows which theme is on, and two others
 * draw the volume and brightness bar without it - Diatom over a game and
 * portkeys over a port - so the choice crosses between them as a file,
 * /tmp/zyperos-theme. A file another process writes is the kind of input a
 * reader must not trust, and the readers here draw a pill on the player's
 * screen over a running game, so what is held is: a file that is not there, or
 * is empty, or says something that is not a theme, reads as Beard, the default,
 * and never as an error; the two words the writer says are read back as
 * themselves, with or without the whitespace a person editing it by hand
 * leaves; and a reader never sees half a file, which is the writer's
 * temporary and a rename, so none is ever left behind - after a write, after
 * a stale one from a crash, after a write that failed.
 *
 * Links src/themefile.c and NOT SDL.
 */
#include "../src/design.h"
#include "../src/themefile.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int fails;

static void ck(int cond, const char *what)
{
	if (!cond) { printf("  FAIL %s\n", what); fails++; }
}

static char dir[64];
static char p[128], tmp[136];

/* A file holding exactly these bytes, as a person or another program might
 * leave it - not through themefile_write, whose output is the one thing that
 * is certain to read back. */
static void put(const char *path, const void *bytes, size_t n)
{
	FILE *f = fopen(path, "wb");

	if (f) {
		if (n) fwrite(bytes, 1, n, f);
		fclose(f);
	}
}

static void says(const char *text, int want, const char *what)
{
	put(p, text, strlen(text));
	ck(themefile_read(p) == want, what);
}

static int exists(const char *path)
{
	struct stat st;

	return lstat(path, &st) == 0;
}

/* What is in the file, up to 63 bytes, for the exact-text claims */
static size_t slurp(const char *path, char *out)
{
	FILE *f = fopen(path, "rb");
	size_t n = 0;

	if (f) {
		n = fread(out, 1, 63, f);
		fclose(f);
	}
	out[n] = 0;
	return n;
}

/* What themefile_read says about path, asked of a child process that is given
 * two seconds: a reader that blocks on the file (a FIFO nobody writes to) must
 * fail this check and not hang it. -1 if the child did not come back. */
static int read_in_child(const char *path)
{
	struct timespec nap = { 0, 10 * 1000 * 1000 };
	int i, status;
	pid_t pid;

	fflush(stdout);
	pid = fork();
	if (pid < 0) return -1;
	if (pid == 0) _exit(themefile_read(path) == DS_DARK ? 1 : 0);
	for (i = 0; i < 200; i++) {
		if (waitpid(pid, &status, WNOHANG) == pid)
			return WIFEXITED(status) ? WEXITSTATUS(status) ? DS_DARK : DS_LIGHT : -1;
		nanosleep(&nap, NULL);
	}
	kill(pid, SIGKILL);
	waitpid(pid, &status, 0);
	return -1;
}

int main(void)
{
	char text[64];
	char big[4096];
	char other[128];

	snprintf(dir, sizeof dir, "/tmp/zyperos-themefile-%ld", (long)getpid());
	if (mkdir(dir, 0700) != 0) { printf("themefile: no scratch directory\n"); return 1; }
	snprintf(p, sizeof p, "%s/theme", dir);
	snprintf(tmp, sizeof tmp, "%s.tmp", p);

	/* The path every process agrees on */
	ck(!strcmp(THEME_FILE, "/tmp/zyperos-theme"), "the theme file is /tmp/zyperos-theme");

	/* Not there: after a cold boot, until the launcher has written it, and for
	 * a port started straight from a boot. Beard, not a failure. */
	ck(themefile_read(p) == DS_LIGHT, "no file reads Beard");
	ck(themefile_read(NULL) == DS_LIGHT, "no path reads Beard");
	ck(themefile_read(dir) == DS_LIGHT, "a directory where the file should be reads Beard");

	/* What the writer says, and that it reads back as itself */
	ck(themefile_write(p, DS_DARK), "writes dark");
	ck(themefile_read(p) == DS_DARK, "dark reads back as dark");
	ck(slurp(p, text) == 5 && !strcmp(text, "dark\n"), "and the file is exactly `dark` and a newline");
	ck(themefile_write(p, DS_LIGHT), "writes light");
	ck(themefile_read(p) == DS_LIGHT, "light reads back as light");
	ck(slurp(p, text) == 6 && !strcmp(text, "light\n"), "and the file is exactly `light` and a newline");
	ck(themefile_write(p, DS_DARK) && themefile_write(p, DS_DARK) && themefile_read(p) == DS_DARK,
	   "writing the same theme again is no trouble");

	/* A rewrite replaces, never appends */
	ck(themefile_write(p, DS_DARK) && themefile_write(p, DS_LIGHT) &&
	   slurp(p, text) == 6 && !strcmp(text, "light\n"), "a rewrite replaces what was there");

	/* The temporary is a step of the write and is never left behind */
	ck(!exists(tmp), "no temporary left after a write");

	/* What a person editing the file by hand leaves */
	says("dark\n", DS_DARK, "`dark` and a newline reads dark");
	says("dark", DS_DARK, "`dark` with no newline reads dark");
	says(" dark \n", DS_DARK, "spaces around it read dark");
	says("\t dark\r\n", DS_DARK, "a tab, and a Windows line end, read dark");
	says("\n\ndark\n\n", DS_DARK, "blank lines around it read dark");
	says("light\n", DS_LIGHT, "`light` and a newline reads light");
	says(" light ", DS_LIGHT, "spaces around light read light");

	/* What is not a theme reads as the default - and the default, not the
	 * other theme: a file gone wrong must not turn a player's bar dark */
	says("purple", DS_LIGHT, "`purple` reads Beard");
	says("", DS_LIGHT, "an empty file reads Beard");
	says("\n", DS_LIGHT, "a newline alone reads Beard");
	says("   ", DS_LIGHT, "spaces alone read Beard");
	says("darkness\n", DS_LIGHT, "a word that only starts with dark reads Beard");
	says("dar\n", DS_LIGHT, "a word that dark starts reads Beard");
	says("dark light\n", DS_LIGHT, "two words read Beard");
	says("dark\nlight\n", DS_LIGHT, "two lines read Beard");
	says("1\n", DS_LIGHT, "a number reads Beard");
	put(p, "dark\0junk", 9);
	ck(themefile_read(p) == DS_LIGHT, "dark with a NUL and more after it reads Beard");

	/* Far more than a word: not ours, whatever it begins with, and not read
	 * to the end either */
	memset(big, ' ', sizeof big);
	memcpy(big + sizeof big - 5, "dark\n", 5);
	put(p, big, sizeof big);
	ck(themefile_read(p) == DS_LIGHT, "dark at the end of four kilobytes of spaces reads Beard");
	memset(big, 'd', sizeof big);
	put(p, big, sizeof big);
	ck(themefile_read(p) == DS_LIGHT, "four kilobytes of one letter reads Beard");
	memcpy(big, "dark\n", 5);
	memset(big + 5, ' ', sizeof big - 5);
	put(p, big, sizeof big);
	ck(themefile_read(p) == DS_LIGHT, "dark and then four kilobytes of spaces reads Beard");

	/* A theme that is no theme is not written: the file keeps what it said,
	 * and a reader is never handed something it would have to guess at */
	ck(themefile_write(p, DS_DARK), "dark again, to have something there");
	ck(!themefile_write(p, 99), "a theme past the table is refused");
	ck(!themefile_write(p, -1), "so is a negative one");
	ck(!themefile_write(p, DS_THEMES), "so is the count itself");
	ck(slurp(p, text) == 5 && !strcmp(text, "dark\n"), "and the file still says dark");
	ck(!exists(tmp), "with no temporary left");
	ck(!themefile_write(NULL, DS_DARK), "no path is refused");

	/* A write that cannot happen says so, with why in errno for the line the
	 * caller logs, and leaves nothing */
	snprintf(other, sizeof other, "%s/no/such/dir/theme", dir);
	errno = 0;
	ck(!themefile_write(other, DS_DARK), "a directory that is not there: refused");
	ck(errno == ENOENT, "and errno says why");
	snprintf(other, sizeof other, "%s/sub", dir);
	mkdir(other, 0700);
	ck(!themefile_write(other, DS_DARK), "a directory where the file should be: refused");
	snprintf(other, sizeof other, "%s/sub.tmp", dir);
	ck(!exists(other), "and its temporary is not left");
	{
		char longp[1200];

		memset(longp, 'a', sizeof longp - 1);
		longp[sizeof longp - 1] = 0;
		ck(!themefile_write(longp, DS_DARK), "a path too long for its temporary is refused");
	}

	/* The bar's colors under the word, as the player's look has them
	 * (themefile_write_bar): written, and read back as the six roles, the
	 * rest the theme's own; the word alone gives the tokens' table; a bar
	 * line that is not whole gives the tokens' colors and still the theme */
	{
		ds_theme bar = DS_THEME[DS_DARK], got;

		bar.surface = (ds_rgb){ 0x1a, 0x20, 0x30 };
		bar.accent_ink = (ds_rgb){ 0x7a, 0xb8, 0xff };
		ck(themefile_write_bar(p, DS_DARK, &bar), "writes the bar's colors");
		ck(slurp(p, text) > 0 && !strcmp(text, "dark\nbar 1a2030 3a3e3c 3f4341 7ab8ff f2d27a 111310\n"),
		   "as the word and a line of six colors");
		ck(themefile_read_bar(p, &got) == DS_DARK && got.surface.b == 0x30 &&
		   got.accent_ink.r == 0x7a && got.text.r == DS_THEME[DS_DARK].text.r,
		   "read back: the six roles, the rest Salt & Pepper's");
		ck(themefile_read(p) == DS_DARK, "and themefile_read still says dark");
		ck(themefile_write_bar(p, DS_LIGHT, NULL) && themefile_read_bar(p, &got) == DS_LIGHT &&
		   got.surface.r == DS_THEME[DS_LIGHT].surface.r, "no colors is the word alone, the tokens'");
		put(p, "dark\nbar 1a2030 zz\n", 18);
		ck(themefile_read_bar(p, &got) == DS_DARK && got.surface.r == DS_THEME[DS_DARK].surface.r,
		   "a bar line that is not whole: dark, in the tokens' colors");
		put(p, "light\nbar 1a2030 3a3e3c 3f4341 7ab8ff f2d27a 111310 ffffff\n", 58);
		ck(themefile_read_bar(p, &got) == DS_LIGHT && got.surface.r == DS_THEME[DS_LIGHT].surface.r,
		   "a seventh color is no bar line either");
	}

	/* A temporary left by a launcher that was killed mid-write is no reason
	 * the next write cannot happen */
	put(tmp, "dar", 3);
	ck(themefile_write(p, DS_LIGHT), "writes over a stale temporary");
	ck(themefile_read(p) == DS_LIGHT && !exists(tmp), "and it is gone");

	/* /tmp is writable by every user on a development host, so a temporary
	 * that is somebody's symlink must not be written through: the write goes
	 * to the temporary's own place, and what it pointed at is untouched */
	snprintf(other, sizeof other, "%s/victim", dir);
	put(other, "keep\n", 5);
	ck(symlink(other, tmp) == 0, "a symlink where the temporary goes (setup)");
	ck(themefile_write(p, DS_DARK), "still writes");
	ck(slurp(other, text) == 5 && !strcmp(text, "keep\n"), "and wrote nothing through the link");
	ck(themefile_read(p) == DS_DARK && !exists(tmp), "the file says dark, and no link is left");
	/* ...and the file itself being a symlink is replaced, not followed */
	unlink(p);
	ck(symlink(other, p) == 0, "a symlink where the file goes (setup)");
	ck(themefile_write(p, DS_LIGHT), "writes");
	ck(slurp(other, text) == 5 && !strcmp(text, "keep\n"), "what the file linked to is untouched");
	ck(themefile_read(p) == DS_LIGHT, "and the file says light");

	/* The file is written by another process, and a FIFO left at its name is
	 * the one thing there that makes a plain open wait: for a writer that
	 * may never come, on the key path of a program that has no time to wait.
	 * It reads as Beard, and at once, with nobody writing to it and with
	 * somebody who has written nothing; and it reads what a writer has said. */
	snprintf(other, sizeof other, "%s/fifo", dir);
	if (mkfifo(other, 0600) != 0) {
		ck(0, "a FIFO where the file should be (setup)");
	} else {
		int w;

		ck(read_in_child(other) == DS_LIGHT, "a FIFO nobody writes to reads Beard, and does not wait");
		w = open(other, O_RDWR | O_NONBLOCK);
		ck(w >= 0, "a writer on the FIFO (setup)");
		if (w >= 0) {
			ck(read_in_child(other) == DS_LIGHT, "a FIFO with a writer that said nothing reads Beard, and does not wait");
			ck(write(w, "dark\n", 5) == 5, "the writer says dark (setup)");
			ck(read_in_child(other) == DS_DARK, "and the FIFO reads dark");
			close(w);
		}
		unlink(other);
	}

	/* The size is what makes it cheap: one small read, nothing kept. A reader
	 * on every key press can afford it. */
	{
		int i, ok = 1;

		ck(themefile_write(p, DS_DARK), "dark once more");
		for (i = 0; i < 2000; i++)
			if (themefile_read(p) != DS_DARK) ok = 0;
		ck(ok, "two thousand reads in a row, no descriptor left open");
	}

	/* Cleanup: whatever is in the scratch directory, then it */
	snprintf(other, sizeof other, "%s/sub", dir);
	rmdir(other);
	snprintf(other, sizeof other, "%s/victim", dir);
	unlink(other);
	unlink(tmp);
	unlink(p);
	rmdir(dir);

	if (fails) { printf("themefile: %d FAILED\n", fails); return 1; }
	printf("themefile: ok\n");
	return 0;
}
