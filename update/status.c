/* SPDX-License-Identifier: GPL-3.0-only */
/* The status file: what the launcher's System Update screen shows, written
 * whole to a temporary name and renamed over the old one, so a reader never
 * sees half of it. One "key value" line each:
 *
 *   state <word>        see docs/updates.md for the list
 *   installed 0.1.0     the ZyperOS folder's VERSION
 *   version 0.2.0       the release this is about, when there is one
 *   download <bytes>    what the update downloads (state available)
 *   size <bytes>        the new folder, every file
 *   done <bytes>        progress through total, for the bar
 *   total <bytes>
 *   files <done> <all>
 *   message <words>     for the log and the screen's note
 *   pid <pid>           the program, while it runs
 *   time <unix time>    when this was written */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "update.h"

void up_status_write(up_status *s)
{
	char tmp[600];
	FILE *f;

	if (!s->path) return;
	snprintf(tmp, sizeof tmp, "%s.%ld", s->path, (long)getpid());
	f = fopen(tmp, "w");
	if (!f) return;
	fprintf(f, "state %s\n", s->state);
	if (s->installed[0]) fprintf(f, "installed %s\n", s->installed);
	if (s->version[0]) fprintf(f, "version %s\n", s->version);
	if (s->download) fprintf(f, "download %llu\n", (unsigned long long)s->download);
	if (s->size) fprintf(f, "size %llu\n", (unsigned long long)s->size);
	if (s->kind[0]) fprintf(f, "kind %s\n", s->kind);
	if (s->slot[0]) fprintf(f, "slot %s\n", s->slot);
	if (s->other[0]) fprintf(f, "other %s\n", s->other);
	if (s->total) fprintf(f, "done %llu\ntotal %llu\n", (unsigned long long)s->done, (unsigned long long)s->total);
	if (s->files_total) fprintf(f, "files %d %d\n", s->files_done, s->files_total);
	if (s->message[0]) fprintf(f, "message %s\n", s->message);
	fprintf(f, "pid %ld\ntime %lld\n", (long)getpid(), (long long)time(NULL));
	if (fclose(f) == 0) rename(tmp, s->path);
	else unlink(tmp);
}
