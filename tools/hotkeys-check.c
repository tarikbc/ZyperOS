/* SPDX-License-Identifier: GPL-3.0-only */
/* The hotkeys' rules (src/hotkeys.h) and what is kept for them
 * (src/hkstore.h): ZyperOS's own four, a key set, the swap that loses no
 * hotkey, what is stored and read back, Diatom's RUN form, and the three
 * scopes a game falls back through.
 *
 *     make check-hotkeys
 *
 * No SDL; a temporary library database. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../src/db.h"
#include "../src/hkstore.h"
#include "../src/hotkeys.h"
#include "../src/menukeys.h"

static int fails;

static void ck(int ok, const char *what)
{
	printf("  %s %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok) fails++;
}

static void rules(void)
{
	hk_table t, u;
	char s[200];

	printf("the rules\n");
	hk_default(&t);
	ck(t.key[HK_SAVE] == HK_R1 && t.key[HK_LOAD] == HK_L1 && t.key[HK_FASTER] == HK_R2 &&
	   t.key[HK_SLOWER] == HK_L2 && t.key[HK_SHOT] == HK_NONE && t.key[HK_QUIT] == HK_NONE,
	   "ZyperOS's own: the shoulders' four, the rest None");
	hk_store(&t, s, sizeof s);
	ck(s[0] == '\0', "ZyperOS's own is stored as nothing");
	hk_run(&t, s, sizeof s);
	ck(!strcmp(s, "save:r1,load:l1,faster:r2,slower:l2"), "Diatom's form of ZyperOS's own");
	hk_set(&t, HK_SHOT, HK_Y);
	ck(t.key[HK_SHOT] == HK_Y && hk_act_on(&t, HK_Y, -1) == HK_SHOT, "Screenshot on Y");
	hk_chord(&t, HK_SHOT, s, sizeof s);
	ck(!strcmp(s, "SELECT+Y"), "said SELECT+Y");
	hk_chord(&t, HK_QUIT, s, sizeof s);
	ck(s[0] == '\0', "Quit None says nothing");
	/* a clash is a swap: Screenshot on R2 hands Faster Y */
	hk_swap(&t, HK_SHOT, HK_R2);
	ck(t.key[HK_SHOT] == HK_R2 && t.key[HK_FASTER] == HK_Y, "the swap: Faster takes Screenshot's Y");
	/* from None: the other goes None */
	hk_swap(&t, HK_QUIT, HK_L2);
	ck(t.key[HK_QUIT] == HK_L2 && t.key[HK_SLOWER] == HK_NONE, "from None: Slower goes None");
	hk_store(&t, s, sizeof s);
	ck(!strcmp(s, "1;faster:y,slower:none,shot:r2,quit:l2"), "stored as the pairs that differ");
	ck(hk_load(s, &u) && hk_equal(&t, &u), "and read back as it was");
	hk_run(&t, s, sizeof s);
	ck(!strcmp(s, "save:r1,load:l1,faster:y,shot:r2,quit:l2"), "Diatom's form, None left out");
	ck(!hk_load("2;shot:y", &u) && !hk_load("1;shot:select", &u) && !hk_load("1;zoom:y", &u) &&
	   !hk_load("1;shot", &u), "another version, SELECT, an act or a pair it does not know: refused");
	hk_default(&t);
	ck(hk_equal(&u, &t), "and read as ZyperOS's own");
	/* a default's key given to another, its own pair unchanged: one chord */
	ck(hk_load("1;shot:r1", &u) && u.key[HK_SHOT] == HK_R1 && u.key[HK_SAVE] == HK_NONE,
	   "shot:r1 stored: Quick Save, whose R1 it is from the start, goes None");
	for (int i = 0; i < HK_ACTS; i++) t.key[i] = HK_NONE;
	hk_run(&t, s, sizeof s);
	ck(!strcmp(s, "none"), "no hotkey at all: none");
}

/* Fast Forward (2026-11-23): one press to its speed and back, None from the
 * start, listed after Slower; its speed 2x from the start, kept per scope */
static void fastforward(void)
{
	hk_table t, u, back;
	char s[200];

	printf("fast forward\n");
	hk_default(&t);
	ck(t.key[HK_FFWD] == HK_NONE && t.ff == HK_FF_2 && !strcmp(hk_ff_word(t.ff), "2") &&
	   hk_ff_half(t.ff) == 4, "None from the start, its speed 2x");
	ck(hk_order[3] == HK_SLOWER && hk_order[4] == HK_FFWD && hk_order[5] == HK_SHOT,
	   "listed after Slower, beside the speed's steps");
	ck(hk_ff_half(HK_FF_15) == 3 && hk_ff_half(HK_FF_3) == 6 && hk_ff_half(HK_FF_MAX) == 0 &&
	   !strcmp(hk_ff_word(HK_FF_MAX), "max"), "1.5x, 3x and Max as SPEED says them");
	hk_set(&t, HK_FFWD, HK_Y);
	t.ff = HK_FF_MAX;
	hk_store(&t, s, sizeof s);
	ck(!strcmp(s, "1;ffwd:y,ffspeed:max"), "stored: its key and its speed where they differ");
	ck(hk_load(s, &u) && hk_equal(&t, &u), "and read back as it was");
	hk_run(&t, s, sizeof s);
	ck(!strcmp(s, "save:r1,load:l1,faster:r2,slower:l2,ffwd:y"), "Diatom's keys: ffwd:y");
	hk_default(&t);
	t.ff = HK_FF_15;
	ck(!hk_equal(&t, &u) && (hk_default(&u), !hk_equal(&t, &u)), "its speed alone makes a table differ");
	hk_store(&t, s, sizeof s);
	ck(!strcmp(s, "1;ffspeed:1.5"), "its speed alone is stored alone");
	ck(!hk_load("1;ffspeed:4", &u) && !hk_load("1;ffspeed:", &u), "a speed it does not know: refused");
	/* kept per scope, as the keys: Every Game's Max, a system falls back */
	hk_default(&t);
	t.ff = HK_FF_MAX;
	hks_put(HKS_EVERY, NULL, NULL, &t);
	ck(!hks_get(HKS_GAME, "GB", "p.gb", &u) && u.ff == HK_FF_MAX, "a game plays with Every Game's speed");
	u.ff = HK_FF_3;
	hks_put(HKS_SYSTEM, "GB", NULL, &u);
	ck(hks_get(HKS_GAME, "GB", "p.gb", &u) == false && u.ff == HK_FF_3, "and with its system's over it");
	hks_fallback(HKS_SYSTEM, "GB", NULL, &back);
	hks_put(HKS_SYSTEM, "GB", NULL, &back);
	ck(!db_has(db_lib(), "hotkeys.GB"), "set back to Every Game's: no key");
	hks_reset(HKS_EVERY, NULL, NULL);
}

static void scopes(void)
{
	hk_table t, back, def;
	char v[200];

	printf("the scopes\n");
	hk_default(&def);
	ck(!hks_get(HKS_GAME, "NES", "a.nes", &t) && hk_equal(&t, &def), "nothing kept: ZyperOS's own");
	hk_set(&t, HK_SHOT, HK_Y);
	hks_put(HKS_EVERY, NULL, NULL, &t);
	ck(db_get_str(db_lib(), "hotkeys", v, sizeof v, NULL) && !strcmp(v, "1;shot:y"), "Every Game kept");
	ck(!hks_get(HKS_GAME, "NES", "a.nes", &t) && t.key[HK_SHOT] == HK_Y, "a game falls back to it");
	hk_set(&t, HK_SHADER, HK_X);
	hks_put(HKS_SYSTEM, "NES", NULL, &t);
	ck(db_get_str(db_lib(), "hotkeys.NES", v, sizeof v, NULL), "the system's kept");
	ck(hks_get(HKS_SYSTEM, "NES", NULL, &t) && t.key[HK_SHADER] == HK_X && t.key[HK_SHOT] == HK_Y,
	   "the system's: its own");
	ck(!hks_get(HKS_GAME, "NES", "a.nes", &t) && t.key[HK_SHADER] == HK_X, "a game of it: the system's");
	ck(!hks_get(HKS_GAME, "SNES", "b.sfc", &t) && t.key[HK_SHADER] == HK_NONE && t.key[HK_SHOT] == HK_Y,
	   "another system's game: Every Game's");
	/* This Game: copied at its first change, deleted when back to the system's */
	hks_get(HKS_GAME, "NES", "a.nes", &t);
	hk_set(&t, HK_QUIT, HK_START);
	hks_put(HKS_GAME, "NES", "a.nes", &t);
	ck(hks_get(HKS_GAME, "NES", "a.nes", &t) && t.key[HK_QUIT] == HK_START && t.key[HK_SHADER] == HK_X,
	   "This Game: its own, the system's under it");
	hks_fallback(HKS_GAME, "NES", "a.nes", &back);
	ck(back.key[HK_QUIT] == HK_NONE, "its fallback is the system's");
	hks_put(HKS_GAME, "NES", "a.nes", &back);
	ck(!db_has(db_lib(), "hotkeys.NES.a.nes"), "set back to the system's: no key");
	hks_reset(HKS_SYSTEM, "NES", NULL);
	hks_reset(HKS_EVERY, NULL, NULL);
	ck(!db_has(db_lib(), "hotkeys.NES") && !db_has(db_lib(), "hotkeys") &&
	   !hks_get(HKS_GAME, "NES", "a.nes", &t) && hk_equal(&t, &def), "Reset Hotkeys: ZyperOS's own again");
	/* a system set to Every Game's own keeps no key */
	hks_put(HKS_SYSTEM, "NES", NULL, &def);
	ck(!db_has(db_lib(), "hotkeys.NES"), "a scope equal to its fallback keeps no key");
	fastforward();
}

/* MENU's chords (src/menukeys.h) */
static void menukeys(void)
{
	mk_table t, back;
	char s[96], c[24];

	mk_default(&t);
	ck(t.key[MK_SHOT] == HK_L1 && t.key[MK_LOCK] == HK_L2 && t.hold_ms == 2000,
	   "MENU's own: L1 a screenshot, L2 the lock, 2 s to unlock");
	mk_chord(&t, MK_LOCK, c, sizeof c);
	ck(!strcmp(c, "MENU+L2"), "the lock's chord as the page writes it");
	ck(mk_act_on(&t, HK_L1) == MK_SHOT && mk_act_on(&t, HK_R1) < 0, "the action a key is on");
	ck(!mk_key_ok(HK_START) && !mk_key_ok(HK_NONE) && mk_key_ok(HK_A) && mk_key_ok(HK_R2),
	   "never START (MENU+START is Home): the faces and the shoulders only");
	mk_store(&t, s, sizeof s);
	ck(!s[0], "MENU's own keeps nothing");
	mk_swap(&t, MK_SHOT, HK_L2);
	ck(t.key[MK_SHOT] == HK_L2 && t.key[MK_LOCK] == HK_L1, "a key the other has: the two swap");
	mk_swap(&t, MK_LOCK, HK_START);
	ck(t.key[MK_LOCK] == HK_L1, "START refused, nothing changed");
	ck(mk_hold_step(2000, 1) == 3000 && mk_hold_step(5000, 1) == 500 && mk_hold_step(500, -1) == 5000 &&
	   mk_hold_step(500, 1) == 1000 && mk_hold_step(1000, -1) == 500,
	   "Hold to Unlock steps 0.5, 1, 2, 3, 5 s and round (0.5 s: the Pixel 2 test of 1.1.1-test.3)");
	t.hold_ms = 500;
	mk_store(&t, s, sizeof s);
	ck(!strcmp(s, "1;shot:l2,lock:l1,hold:500") && mk_load(s, &back) && back.hold_ms == 500,
	   "half a second is kept and read back");
	{
		mk_table d;
		char r[48];

		mk_default(&d);
		mk_run(&d, r, sizeof r);
		ck(!strcmp(r, "shot:l1,lock:l2"), "RUN menukeys= for a game: MENU's own, shot:l1,lock:l2");
		mk_swap(&d, MK_SHOT, HK_R2);
		mk_run(&d, r, sizeof r);
		ck(!strcmp(r, "shot:r2,lock:l2"), "and the keys in force");
	}
	mk_secs(500, false, c, sizeof c);
	{
		char d[24], e[24];

		mk_secs(500, true, d, sizeof d);
		mk_secs(2000, true, e, sizeof e);
		ck(!strcmp(c, "0.5") && !strcmp(d, "0,5") && !strcmp(e, "2"),
		   "the seconds as the pages write them: 0.5, 0,5 with a decimal comma, 2");
	}
	t.hold_ms = 3000;
	mk_store(&t, s, sizeof s);
	ck(!strcmp(s, "1;shot:l2,lock:l1,hold:3000"), "what is kept");
	ck(mk_load(s, &back) && !memcmp(back.key, t.key, sizeof t.key) && back.hold_ms == 3000, "and read back");
	ck(!mk_load("1;shot:l1,lock:l1", &back) && back.key[MK_SHOT] == HK_L1 && back.key[MK_LOCK] == HK_L2,
	   "two on one key: not read, MENU's own");
	ck(!mk_load("1;shot:start", &back) && !mk_load("1;hold:2500", &back) && !mk_load("2;x", &back),
	   "START, a hold not offered, another version: not read");
}

int main(void)
{
	char dev[] = "/tmp/hotkeys-check-dev-XXXXXX", lib[] = "/tmp/hotkeys-check-lib-XXXXXX";
	int fd = mkstemp(dev), fl = mkstemp(lib);

	printf("hotkeys: the player's hotkeys, pure\n");
	rules();
	if (fd >= 0) close(fd);
	if (fl >= 0) close(fl);
	if (fd < 0 || fl < 0 || !db_init(dev, lib, "/dev/null")) {
		printf("  (no temporary database)\n");
		return 1;
	}
	scopes();
	menukeys();
	unlink(dev);
	unlink(lib);
	if (fails) {
		printf("%d failed\n", fails);
		return 1;
	}
	printf("ok\n");
	return 0;
}
