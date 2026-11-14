/* SPDX-License-Identifier: GPL-3.0-only */
/* See rahashjob.h. */
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#if defined(__linux__)
#include <sys/resource.h>
#endif

#include "rahash.h"
#include "rahashjob.h"

enum { J_NONE, J_RUNNING, J_DONE };

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int  g_state = J_NONE;
static bool g_ok;
static char g_rom[2048], g_tag[32], g_set[2048], g_hash[33];

static void *job(void *arg)
{
	char rom[sizeof g_rom], tag[sizeof g_tag], hash[33] = "";
	bool ok;

	(void)arg;
#if defined(__linux__)
	/* This thread only, on Linux: the game's frames come first */
	setpriority(PRIO_PROCESS, 0, 10);
#endif
	pthread_mutex_lock(&g_lock);
	snprintf(rom, sizeof rom, "%s", g_rom);
	snprintf(tag, sizeof tag, "%s", g_tag);
	pthread_mutex_unlock(&g_lock);

	ok = ra_hash_rom(rom, tag, hash);

	pthread_mutex_lock(&g_lock);
	g_ok = ok;
	snprintf(g_hash, sizeof g_hash, "%s", ok ? hash : "");
	g_state = J_DONE;
	pthread_mutex_unlock(&g_lock);
	return NULL;
}

bool rhj_begin(const char *rom, const char *tag, const char *set_path)
{
	pthread_t t;
	pthread_attr_t at;
	bool started;

	if (!rom || !tag || !set_path) return false;
	pthread_mutex_lock(&g_lock);
	if (g_state == J_RUNNING) {
		pthread_mutex_unlock(&g_lock);
		fprintf(stderr, "cheevos: a hash is still being made; this game's set is asked next time\n");
		return false;
	}
	snprintf(g_rom, sizeof g_rom, "%s", rom);
	snprintf(g_tag, sizeof g_tag, "%s", tag);
	snprintf(g_set, sizeof g_set, "%s", set_path);
	g_hash[0] = '\0';
	g_state = J_RUNNING;
	pthread_mutex_unlock(&g_lock);

	pthread_attr_init(&at);
	pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
	started = pthread_create(&t, &at, job, NULL) == 0;
	pthread_attr_destroy(&at);
	if (!started) {
		pthread_mutex_lock(&g_lock);
		g_state = J_NONE;
		pthread_mutex_unlock(&g_lock);
	}
	return started;
}

int rhj_poll(char *hash, char *set, size_t setn)
{
	int r;

	pthread_mutex_lock(&g_lock);
	if (g_state == J_RUNNING) {
		r = 0;
	} else if (g_state == J_DONE) {
		r = g_ok ? 1 : -1;
		if (hash) snprintf(hash, 33, "%s", g_hash);
		if (set) snprintf(set, setn, "%s", g_set);
		g_state = J_NONE;
	} else {
		r = -2;
	}
	pthread_mutex_unlock(&g_lock);
	return r;
}
