/* SPDX-License-Identifier: GPL-3.0-only */
/* Does an achievement set survive the round trip?
 *
 *     make check-cheevos
 *
 * The launcher's half of RetroAchievements is three claims that are easy to
 * get wrong and impossible to see going wrong on a device:
 *
 *   - the set file parses, including its comment-borne metadata
 *   - what is already earned is left OUT of what Diatom is asked to watch,
 *     because sending it would re-award things the player did last week
 *   - the earned store round-trips, since it is the only record there is
 *     until this client is registered with RetroAchievements
 *
 * Offline, no device, no emulator. Builds its own fixture, including a
 * condition long enough to break any fixed line buffer - a real one has been
 * measured at 30,897 characters.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>

#include "../src/atomic.h"
#include "../src/cheevos.h"
#include "../src/db.h"

static int failures;

#define CHECK(cond, ...)                                                      \
	do {                                                                      \
		if (!(cond)) {                                                        \
			printf("  FAIL: ");                                               \
			printf(__VA_ARGS__);                                              \
			printf("\n        at %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
			failures++;                                                       \
		}                                                                     \
	} while (0)

static char g_set[128], g_active[128], g_store[128], g_lib[128];

/* The store is a database now, so "remove it and start again" is a couple of
 * unlinks and a reopen rather than one. Everything below that used to poke the
 * file goes through these. */
static void store_reset(void)
{
	char side[160];
	db_shutdown();
	remove(g_store); remove(g_lib);
	snprintf(side, sizeof side, "%s-wal", g_store); remove(side);
	snprintf(side, sizeof side, "%s-shm", g_store); remove(side);
	snprintf(side, sizeof side, "%s-wal", g_lib); remove(side);
	snprintf(side, sizeof side, "%s-shm", g_lib); remove(side);
	db_init(g_store, g_lib, NULL);
}

static bool count_row(const char *k, const char *v, void *ctx)
{
	(void)k; (void)v;
	(*(int *)ctx)++;
	return true;
}

static int store_rows(void)
{
	int n = 0;
	db_each_prefix(db_lib(), "chv.", count_row, &n);
	return n;
}

static void fixture(void)
{
	FILE *f = fopen(g_set, "w");
	size_t i;

	if (!f) { printf("  FAIL: cannot write %s\n", g_set); failures++; return; }
	/* Format 2, with badges; the second has no description, which strtok
	 * read as its badge; the third is a format 1 line, with no badge */
	fputs("#! zyperos-cheevos 2\tgame=1459\tconsole=7\ttitle=Blaster Master\n", f);
	fputs("#:\t24698\t5\tBringing the Heat-Seeker\tUse a homing missile.\t85741\n", f);
	fputs("24698\t0xH06f3=0_0xH0400=3_0xH00ba=0_0xH06f0<d0xH06f0\n", f);
	fputs("#:\t24699\t10\tMaking Lightning Waves\t\t85742\n", f);
	fputs("24699\t0xH06f3=0_0xH0400=3_0xH00ba=1_0xH06f1<d0xH06f1\n", f);
	fputs("#:\t24700\t50\tSpread out the Power\tUse a multi-warhead missile.\n", f);

	/* Longer than any buffer anyone would guess at, and the reason both
	 * readers use getline. Measured across 428 real achievements: median 113
	 * characters, longest 30,897. */
	fputs("24700\t0xH06f3=0", f);
	for (i = 0; i < 4000; i++) fprintf(f, "_0xH%04x=1", (unsigned)(i & 0xff));
	fputs("\n", f);
	fclose(f);
}

/* getline, for the same reason the code under test uses it: a 256-byte fgets
 * split the 40KB condition into 157 pieces and this counted every one of them,
 * which read exactly like the filter having failed. */
static int line_count(const char *path, const char *prefix)
{
	char  *line = NULL;
	size_t cap = 0;
	FILE  *f = fopen(path, "r");
	int    n = 0;

	if (!f) return -1;
	while (getline(&line, &cap, f) > 0)
		if (!prefix || !strncmp(line, prefix, strlen(prefix))) n++;
	free(line);
	fclose(f);
	return n;
}

int main(void)
{
	snprintf(g_set,    sizeof g_set,    "/tmp/zyperos-chv-%ld.set", (long)getpid());
	snprintf(g_active, sizeof g_active, "/tmp/zyperos-chv-%ld.active", (long)getpid());
	snprintf(g_store,  sizeof g_store,  "/tmp/zyperos-chv-%ld-dev.db", (long)getpid());
	snprintf(g_lib,    sizeof g_lib,    "/tmp/zyperos-chv-%ld-lib.db", (long)getpid());
	store_reset();

	printf("cheevos: the launcher's half\n");
	fixture();

	printf("  reading a set:\n");
	chv_earned_load();                        /* absent: nothing earned yet */
	CHECK(chv_load(g_set), "the set did not load");
	CHECK(chv_count() == 3, "expected 3 achievements, got %d", chv_count());
	CHECK(chv_game() == 1459, "game id wrong: %d", chv_game());
	CHECK(chv_console() == 7, "console id wrong: %d", chv_console());
	CHECK(!strcmp(chv_game_title(), "Blaster Master"),
	      "title wrong: %s", chv_game_title());
	CHECK(chv_points_total() == 65, "points total wrong: %d", chv_points_total());
	CHECK(chv_earned() == 0, "nothing should be earned yet");
	CHECK(chv_at(0) && !strcmp(chv_at(0)->title, "Bringing the Heat-Seeker"),
	      "first title wrong: %s", chv_at(0) ? chv_at(0)->title : "(none)");
	CHECK(chv_at(2) && chv_at(2)->points == 50,
	      "the achievement behind the 40KB condition was not parsed");
	CHECK(chv_at(0) && !strcmp(chv_at(0)->badge, "85741"), "badge not read: %s",
	      chv_at(0) ? chv_at(0)->badge : "(none)");
	CHECK(chv_at(1) && !chv_at(1)->desc[0] && !strcmp(chv_at(1)->badge, "85742"),
	      "an empty description took the badge's place");
	CHECK(chv_at(2) && !chv_at(2)->badge[0], "a format 1 line has no badge");
	CHECK(chv_note_progress(24699, "3/10") && !strcmp(chv_at(1)->progress, "3/10"),
	      "progress not kept on the achievement");
	CHECK(!chv_note_progress(1, "1/2"), "progress for an id not in the set");

	printf("  what Diatom is asked to watch:\n");
	CHECK(chv_write_active(g_active), "nothing was written");
	CHECK(line_count(g_active, NULL) == 3,
	      "expected 3 condition lines, got %d", line_count(g_active, NULL));
	CHECK(line_count(g_active, "#") == 0,
	      "comments leaked into the file Diatom reads");

	printf("  an unlock, and what it changes:\n");
	CHECK(chv_note_unlock(24699), "the unlock was not news");
	CHECK(!chv_note_unlock(24699), "the same unlock counted twice");
	CHECK(chv_earned() == 1, "earned count wrong: %d", chv_earned());
	CHECK(chv_new_this_session() == 1, "session count wrong: %d",
	      chv_new_this_session());
	CHECK(chv_points_earned() == 10, "points earned wrong: %d",
	      chv_points_earned());

	CHECK(chv_write_active(g_active), "still two to watch");
	CHECK(line_count(g_active, NULL) == 2,
	      "an earned achievement was sent back to Diatom (%d lines)",
	      line_count(g_active, NULL));

	printf("  the earned store survives a restart:\n");
	CHECK(chv_earned_save(), "the store did not write");
	chv_clear();
	chv_earned_load();
	CHECK(chv_load(g_set), "the set did not reload");
	CHECK(chv_at(1) && chv_at(1)->earned,
	      "24699 came back unearned after a reload");
	CHECK(chv_new_this_session() == 0,
	      "an achievement earned last session counted as new");
	CHECK(chv_earned() == 1, "earned count wrong after reload: %d", chv_earned());

	/* Everything earned means nothing to watch, and the caller must be told
	 * so rather than handing Diatom an empty file every launch. */
	chv_note_unlock(24698);
	chv_note_unlock(24700);
	CHECK(!chv_write_active(g_active),
	      "an all-earned set should report nothing to watch");

	/* ---- what is owed to the account ------------------------------------
	 *
	 * The store is the durable record and RetroAchievements is a peer to
	 * reconcile with, so every row has to remember whether it has been sent.
	 * Getting this wrong is silent in both directions: an unlock that is
	 * never submitted, or one submitted every time the device boots. */
	printf("  pending and synced:\n");
	store_reset();
	chv_earned_load();
	CHECK(chv_load(g_set), "the set did not load for the pending checks");

	CHECK(chv_note_earned(1459, 24698, true), "learning one from the account is news");
	CHECK(chv_pending_count() == 0, "one already on the account owes nothing");

	CHECK(chv_note_unlock(24699), "earning one here is news");
	CHECK(chv_pending_count() == 1, "an unlock earned here is owed to the account");

	{	int g = 0, i = 0;
		CHECK(chv_pending_at(0, &g, &i) && g == 1459 && i == 24699,
		      "the pending one is not the one that was earned: %d/%d", g, i);
		chv_mark_synced(g, i);
	}
	CHECK(chv_pending_count() == 0, "marking it sent did not clear it");
	CHECK(chv_earned() == 2, "marking it sent lost the achievement");

	/* Across a restart, both the fact and the debt have to survive. */
	CHECK(chv_note_unlock(24700), "a third is news");
	CHECK(chv_earned_save(), "the store did not write");
	chv_clear();
	chv_earned_load();
	CHECK(chv_load(g_set), "the set did not reload");
	CHECK(chv_earned() == 3, "reload lost an achievement: %d", chv_earned());
	CHECK(chv_pending_count() == 1,
	      "reload lost track of what is owed: %d", chv_pending_count());

	/* A key that is not "chv.<game>.<id>" is not a row. There is one writer
	 * and it always writes both halves, so anything else is damage rather than
	 * an older format - and half a key read as a whole one would invent an
	 * achievement nobody earned. */
	{	store_reset();
		db_set_str(db_lib(), "chv.1459", "s");        /* no id half */
		db_set_str(db_lib(), "chv.nonsense", "s");    /* no numbers at all */
		db_set_str(db_lib(), "chv.1459.24700", "s");  /* the only real row */
		chv_clear();
		chv_earned_load();
		CHECK(chv_load(g_set), "the set did not reload over a damaged store");
		CHECK(chv_earned() == 1,
		      "expected the one intact row, got %d", chv_earned());
		CHECK(chv_pending_count() == 0,
		      "the intact row was marked synced and owes nothing, got %d",
		      chv_pending_count());
	}

	/* ---- the store is replaced, never emptied ---------------------------
	 *
	 * Every config in this launcher used to be written with fopen(path,"w"),
	 * which truncates first. A power cut between the truncate and the write
	 * leaves nothing - on a device whose normal shutdown is a button press,
	 * and which is about to start powering itself off on a timer. */
	printf("  a written row survives a reopen:\n");
	{
		/* What this replaces tested atomic_open's all-or-nothing property
		 * against the store file. That property now belongs to sqlite rather
		 * than to us, so the thing worth asserting is the guarantee it was
		 * buying: what was saved is what comes back, and nothing else is.
		 *
		 * From nothing: the block above leaves rows behind, and inheriting
		 * them made an earlier version of this expect one and find two - the
		 * test being wrong, not the code. */
		store_reset();
		chv_earned_load();
		chv_note_earned(1459, 24698, true);
		CHECK(chv_earned_save(), "the store did not write");
		CHECK(store_rows() == 1, "expected one row, got %d", store_rows());

		chv_clear();
		store_reset();
		chv_earned_load();
		CHECK(chv_earned() == 0, "a fresh store came back with rows in it");
	}

	/* ---- offline: what an owed row carries -------------------------------
	 *
	 * An unlock earned offline is sent days later, so the row is all there is
	 * of it: when, in which mode, and the ROM's hash from the set's header.
	 * A row from before those were kept reads as a bare "p". */
	printf("  an owed row keeps when, the mode and the hash:\n");
	{
		FILE *f;
		chv_owed o;
		char v[128];

		store_reset();
		f = fopen(g_set, "w");
		fputs("#! zyperos-cheevos 2\tgame=1459\tconsole=7\ttitle=Blaster Master"
		      "\thash=0123456789abcdef0123456789abcdef\n", f);
		fputs("#:\t24698\t5\tA\tB\t1\n24698\t0xH0=1\n", f);
		fputs("#:\t24699\t10\tC\tD\t2\n24699\t0xH0=2\n", f);
		fputs("#:\t24700\t50\tE\tF\t3\n24700\t0xH0=3\n", f);
		fclose(f);
		db_set_str(db_lib(), "chv.1459.24700", "p");     /* from before */
		chv_earned_load();
		CHECK(chv_load(g_set), "the set with a hash did not load");
		CHECK(!strcmp(chv_game_hash(), "0123456789abcdef0123456789abcdef"),
		      "the header's hash: %s", chv_game_hash());
		CHECK(chv_note_unlock_at(24698, 1767400000L, false, true), "an offline unlock is news");
		CHECK(chv_earned_save(), "the store did not write");
		CHECK(db_get_str(db_lib(), "chv.1459.24698", v, sizeof v, "") &&
		      !strcmp(v, "p\t1767400000\t0\t0123456789abcdef0123456789abcdef"),
		      "the row as written: %s", v);
		CHECK(chv_pending_for(1459) == 2 && chv_pending_for(1) == 0,
		      "owed by game: %d", chv_pending_for(1459));
		CHECK(chv_is_pending(24698) && chv_is_pending(24700) && !chv_is_pending(24699),
		      "owed by achievement");

		/* Only what changed is written: a row put there behind the store's
		 * back survives the next save, which it would not if every row were
		 * written each time (thousands of writes for one unlock) */
		db_set_str(db_lib(), "chv.1459.24700", "p\tmarker");
		CHECK(chv_note_unlock_at(24699, 1767400100L, false, false), "an online unlock is news");
		CHECK(chv_earned_save(), "the store did not write");
		CHECK(db_get_str(db_lib(), "chv.1459.24700", v, sizeof v, "") && !strcmp(v, "p\tmarker"),
		      "a save wrote a row that had not changed: %s", v);
		db_set_str(db_lib(), "chv.1459.24700", "p");

		chv_clear();
		chv_earned_load();
		CHECK(chv_load(g_set), "the set did not reload");
		CHECK(chv_pending_count() == 3, "three owed after a reload: %d", chv_pending_count());
		{
			int k, seen = 0;

			for (k = 0; chv_pending_get(k, &o); k++) {
				if (o.id == 24698) {
					seen++;
					CHECK(o.when == 1767400000L && !o.hardcore &&
					      !strcmp(o.hash, "0123456789abcdef0123456789abcdef"),
					      "24698 came back as %ld %d %s", o.when, o.hardcore, o.hash);
				}
				if (o.id == 24700) {
					seen++;
					CHECK(o.when == 0 && !o.hash[0], "the old row has no age and no hash");
				}
			}
			CHECK(seen == 2, "both rows were read back: %d", seen);
		}
		/* Everything owed at a start has waited: the notice counts it */
		CHECK(chv_mark_synced(1459, 24699), "a row owed at the start had waited");
		CHECK(chv_earned_save(), "the store did not write");
		CHECK(db_get_str(db_lib(), "chv.1459.24699", v, sizeof v, "") && !strcmp(v, "s"),
		      "a sent row is s: %s", v);
	}

	/* ---- a game never played with a network ------------------------------ */
	printf("  a game with no set says why:\n");
	{
		char none[160], absent[160];

		snprintf(absent, sizeof absent, "/tmp/zyperos-chv-%ld-absent.set", (long)getpid());
		chv_none_path(absent, none, sizeof none);
		CHECK(strstr(none, "-absent.none") != NULL, "the marker's name: %s", none);
		remove(none);
		CHECK(chv_state_of(g_set, true) == CHV_SET, "a set on the card");
		CHECK(chv_state_of(absent, true) == CHV_UNCHECKED, "no set, signed in: not checked yet");
		CHECK(chv_state_of(absent, false) == CHV_NONE, "no set, nobody signed in: none");
		chv_none_mark(absent, true);
		CHECK(chv_state_of(absent, true) == CHV_NONE, "RetroAchievements said none");
		chv_none_mark(absent, false);
		CHECK(chv_state_of(absent, true) == CHV_UNCHECKED, "a set arriving takes the marker away");
		remove(none);
	}
	/* The marker on a shelf no set ever came to: Roms/<System>/.cheevos is
	 * not there yet, and only a fetched set made it. The marker went nowhere,
	 * and the game said "not checked yet" for good (2026-11-14). */
	{
		char shelf[160], dir[200], set[260], none[280];

		snprintf(shelf, sizeof shelf, "/tmp/zyperos-chv-%ld-shelf", (long)getpid());
		snprintf(dir, sizeof dir, "%s/.cheevos", shelf);
		snprintf(set, sizeof set, "%s/Game.set", dir);
		chv_none_path(set, none, sizeof none);
		mkdir(shelf, 0755);
		CHECK(access(dir, F_OK) != 0, "the shelf starts with no .cheevos folder");
		CHECK(chv_state_of(set, true) == CHV_UNCHECKED, "not checked yet, before any answer");
		chv_none_mark(set, true);
		CHECK(chv_state_of(set, true) == CHV_NONE,
		      "RetroAchievements said none, on a shelf with no .cheevos folder");
		remove(none);
		rmdir(dir);
		rmdir(shelf);
	}

	remove(g_set); remove(g_active);
	store_reset();
	db_shutdown();
	remove(g_store); remove(g_lib);
	if (failures) { printf("\n%d check(s) failed\n", failures); return 1; }
	printf("\nok: every check passed\n");
	return 0;
}
