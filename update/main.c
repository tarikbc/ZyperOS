/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* zyperos-update: System Update for the card's ZyperOS folder.
 *
 *   zyperos-update check       is there a newer release? (status: available,
 *                              uptodate, needs-system, ready, ...)
 *   zyperos-update download    stage it in ZyperOS.next/, check every file,
 *                              and mark it ready; the next start switches it
 *                              in (update/apply.sh, from the system image)
 *   zyperos-update rollback    ask the next start to swap back to
 *                              ZyperOS.prev/
 *   zyperos-update status      print the status file
 *
 * Options: --card DIR (/mnt/SDCARD), --device NAME (pixel2), --source URL,
 * --cacert FILE, --keys DIR (/etc/zyperos/update-keys), --system FILE
 * (/etc/zyperos/system), --min-battery PCT (25). See docs/updates.md.
 *
 * Exit status: 0 done (check: an update is there), 1 nothing to do (check:
 * up to date), 2 refused or failed (the status file says why), 3 another
 * copy is running. */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

#include <curl/curl.h>
#include <openssl/evp.h>
#include <zlib.h>

#include "update.h"

#ifndef ZYPEROS_VERSION
#define ZYPEROS_VERSION "0.0"
#endif
#define DEFAULT_SOURCE "https://github.com/tarikbc/ZyperOS/releases/latest/download/"
#define LATEST_PART    "/releases/latest/download/"
#define SPACE_MARGIN   (32ull << 20)
#define RUN_GAP        (512u << 10)   /* fetch through a gap this small rather than ask again */

static struct {
	char card[PATH_MAX], device[32];
	char live[PATH_MAX], next[PATH_MAX], prev[PATH_MAX], state[PATH_MAX];
	char keys[PATH_MAX], system[PATH_MAX], cacert[PATH_MAX];
	char source[1024];
	bool source_given;          /* not the default: http and file allowed (tests) */
	int min_battery;
	char sys_version[32];
	int sys_level;
} C;

static up_status S;
/* A system update (docs/updates.md, layer 2): the system's list and pack,
 * staged in .zyperos-system on the card, written into the slot that is not
 * running. Off: the card's ZyperOS folder, as before. */
static bool G_SYS;
static char g_live_folder[PATH_MAX], g_next_folder[PATH_MAX], g_sysnext[PATH_MAX];
static volatile sig_atomic_t g_stop;
static FILE *g_log;

static void on_signal(int sig) { (void)sig; g_stop = 1; }

static void logf_(const char *fmt, ...)
{
	va_list ap;
	char line[600];
	time_t t = time(NULL);
	struct tm tm;

	va_start(ap, fmt);
	vsnprintf(line, sizeof line, fmt, ap);
	va_end(ap);
	fprintf(stderr, "zyperos-update: %s\n", line);
	if (g_log) {
		/* UTC: run from the launcher and from a shell, the zone differs */
		gmtime_r(&t, &tm);
		fprintf(g_log, "%04d-%02d-%02dT%02d:%02d:%02dZ %s\n", tm.tm_year + 1900, tm.tm_mon + 1,
		        tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, line);
		fflush(g_log);
	}
}

/* Set the state and message, write the status file, and log the message. */
static void say(const char *state, const char *fmt, ...)
{
	snprintf(S.state, sizeof S.state, "%s", state);
	S.message[0] = 0;
	if (fmt) {
		va_list ap;
		va_start(ap, fmt);
		vsnprintf(S.message, sizeof S.message, fmt, ap);
		va_end(ap);
		logf_("%s: %s", state, S.message);
	}
	up_status_write(&S);
}

static int mkdirs(const char *path)
{
	char p[PATH_MAX];
	char *s;

	snprintf(p, sizeof p, "%s", path);
	for (s = p + 1; *s; s++) {
		if (*s != '/') continue;
		*s = 0;
		if (mkdir(p, 0755) && errno != EEXIST) return -1;
		*s = '/';
	}
	return mkdir(p, 0755) && errno != EEXIST ? -1 : 0;
}

static int mkparent(const char *path)
{
	char p[PATH_MAX], *s;
	snprintf(p, sizeof p, "%s", path);
	s = strrchr(p, '/');
	if (!s || s == p) return 0;
	*s = 0;
	return mkdirs(p);
}

static bool read_small(const char *path, char *out, size_t n)
{
	FILE *f = fopen(path, "r");
	size_t got;
	if (!f) return false;
	got = fread(out, 1, n - 1, f);
	fclose(f);
	out[got] = 0;
	out[strcspn(out, "\r\n")] = 0;
	return got > 0;
}

static bool write_file(const char *path, const char *data, size_t len)
{
	char tmp[PATH_MAX + 8];
	int fd;
	bool ok;

	snprintf(tmp, sizeof tmp, "%s.tmp", path);
	fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) return false;
	ok = write(fd, data, len) == (ssize_t)len && fsync(fd) == 0;
	ok = close(fd) == 0 && ok;
	if (ok) ok = rename(tmp, path) == 0;
	if (!ok) unlink(tmp);
	return ok;
}

static void rm_rf(const char *path)
{
	struct stat st;
	DIR *d;
	struct dirent *e;

	if (lstat(path, &st)) return;
	if (!S_ISDIR(st.st_mode)) { unlink(path); return; }
	d = opendir(path);
	if (d) {
		while ((e = readdir(d))) {
			char sub[PATH_MAX];
			if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
			snprintf(sub, sizeof sub, "%s/%s", path, e->d_name);
			rm_rf(sub);
		}
		closedir(d);
	}
	rmdir(path);
}

/* ---- the system image and the battery --------------------------------- */

/* /etc/zyperos/system: "version 0.2.0" and "update 1", written by plastron.
 * No file: an image from before System Update, which has no switch at boot
 * and no keys, so nothing staged would ever go in. */
static bool read_system(void)
{
	FILE *f = fopen(C.system, "r");
	char line[128];

	if (!f) return false;
	while (fgets(line, sizeof line, f)) {
		line[strcspn(line, "\r\n")] = 0;
		if (!strncmp(line, "version ", 8)) snprintf(C.sys_version, sizeof C.sys_version, "%s", line + 8);
		else if (!strncmp(line, "update ", 7)) C.sys_level = atoi(line + 7);
	}
	fclose(f);
	if (!C.sys_version[0] || C.sys_level < 1) return false;
	/* An image built before there was a key trusts nothing: no update
	 * could ever pass, so there is no System Update on it */
	{
		DIR *d = opendir(C.keys);
		struct dirent *e;
		bool any = false;
		if (!d) return false;
		while (!any && (e = readdir(d))) {
			size_t n = strlen(e->d_name);
			any = n > 4 && !strcmp(e->d_name + n - 4, ".pem");
		}
		closedir(d);
		return any;
	}
}

/* The battery's charge and whether a charger is in, from the kernel's power
 * supplies: the Battery-type one's capacity, and online on any other. No
 * battery at all (a desktop) counts as charging. ZYPEROS_UPDATE_BATTERY
 * ("18 0") stands in for both in tests. */
static void battery(int *pct, bool *charging)
{
	const char *fake = getenv("ZYPEROS_UPDATE_BATTERY");
	const char *dir = getenv("ZYPEROS_UPDATE_POWER") ? getenv("ZYPEROS_UPDATE_POWER") : "/sys/class/power_supply";
	DIR *d;
	struct dirent *e;
	bool have = false;

	*pct = 100;
	*charging = false;
	if (fake) {
		int c = 0;
		sscanf(fake, "%d %d", pct, &c);
		*charging = c != 0;
		return;
	}
	d = opendir(dir);
	if (!d) { *charging = true; return; }
	while ((e = readdir(d))) {
		char p[PATH_MAX], v[32];
		if (e->d_name[0] == '.') continue;
		snprintf(p, sizeof p, "%s/%s/type", dir, e->d_name);
		if (!read_small(p, v, sizeof v)) continue;
		if (!strcmp(v, "Battery")) {
			snprintf(p, sizeof p, "%s/%s/capacity", dir, e->d_name);
			if (read_small(p, v, sizeof v)) { *pct = atoi(v); have = true; }
		} else {
			snprintf(p, sizeof p, "%s/%s/online", dir, e->d_name);
			if (read_small(p, v, sizeof v) && atoi(v) == 1) *charging = true;
		}
	}
	closedir(d);
	if (!have) *charging = true;
}

/* ---- the network ----------------------------------------------------------- */

static CURL *g_curl;

static void curl_common(CURL *c)
{
	char ua[96];
	snprintf(ua, sizeof ua, "ZyperOS/%s (System Update; +https://github.com/tarikbc/ZyperOS)", ZYPEROS_VERSION);
	curl_easy_reset(c);
	curl_easy_setopt(c, CURLOPT_USERAGENT, ua);
	curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(c, CURLOPT_MAXREDIRS, 5L);
	curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 20L);
	/* Not a total time: a big download on slow Wi-Fi is fine, a stalled one
	 * is not */
	curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 512L);
	curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 45L);
	curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
	if (C.cacert[0]) curl_easy_setopt(c, CURLOPT_CAINFO, C.cacert);
#if LIBCURL_VERSION_NUM >= 0x075500
	/* GitHub's own address and its download host are HTTPS; a source set
	 * for a test (a file:// folder, a server on the cable) may be anything */
	curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, C.source_given ? "https,http,file" : "https");
	curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#endif
}

struct membuf { char *p; size_t n; bool over; };

static size_t mem_write(char *d, size_t sz, size_t nm, void *u)
{
	struct membuf *b = u;
	size_t n = sz * nm;
	char *q;
	if (b->n + n > UP_MAX_MANIFEST) { b->over = true; return 0; }
	q = realloc(b->p, b->n + n + 1);
	if (!q) return 0;
	b->p = q;
	memcpy(b->p + b->n, d, n);
	b->n += n;
	b->p[b->n] = 0;
	return n;
}

static int progress_cb(void *u, curl_off_t dt, curl_off_t dn, curl_off_t ut, curl_off_t un)
{
	(void)u; (void)dt; (void)dn; (void)ut; (void)un;
	return g_stop ? 1 : 0;
}

static bool fetch_text(const char *url, struct membuf *b, char *err, size_t errn)
{
	CURLcode rc;
	char eb[CURL_ERROR_SIZE] = "";

	memset(b, 0, sizeof *b);
	curl_common(g_curl);
	curl_easy_setopt(g_curl, CURLOPT_URL, url);
	curl_easy_setopt(g_curl, CURLOPT_WRITEFUNCTION, mem_write);
	curl_easy_setopt(g_curl, CURLOPT_WRITEDATA, b);
	curl_easy_setopt(g_curl, CURLOPT_ERRORBUFFER, eb);
	curl_easy_setopt(g_curl, CURLOPT_TIMEOUT, 60L);
	curl_easy_setopt(g_curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(g_curl, CURLOPT_XFERINFOFUNCTION, progress_cb);
	rc = curl_easy_perform(g_curl);
	if (rc != CURLE_OK) {
		snprintf(err, errn, "%s", eb[0] ? eb : curl_easy_strerror(rc));
		free(b->p);
		b->p = NULL;
		return false;
	}
	if (!b->p) { snprintf(err, errn, "empty answer"); return false; }
	return true;
}

static void manifest_url(char *out, size_t n)
{
	snprintf(out, n, "%sZyperOS-%s-%s.txt", C.source, C.device, G_SYS ? "system" : "update");
}

/* The pack of one exact release: GitHub's "latest" address moves on when the
 * next release comes out, and a download that resumes the next day must not
 * get the next release's pack. */
static void pack_url(const up_manifest *m, char *out, size_t n)
{
	const char *at = strstr(C.source, LATEST_PART);
	if (at)
		snprintf(out, n, "%.*s/releases/download/v%s/%s", (int)(at - C.source), C.source, m->version, m->pack);
	else
		snprintf(out, n, "%s%s", C.source, m->pack);
}

/* ---- the installed folder's index ------------------------------------------ */

typedef struct { char *path; char sha[UP_SHA_HEX + 1]; } idx_entry;
typedef struct { idx_entry *e; int n; } index_t;

static int cmp_idx(const void *a, const void *b)
{
	return strcmp(((const idx_entry *)a)->path, ((const idx_entry *)b)->path);
}

/* "<sha256> <size> <path>" lines: ZyperOS/.files, which mk/payload.sh writes
 * for a built payload and this program for every update it stages; and the
 * staging journal (.staged) in the same form. */
static void index_read(const char *path, index_t *ix)
{
	FILE *f = fopen(path, "r");
	char line[UP_MAX_PATH + 128];
	int cap = 0;

	memset(ix, 0, sizeof *ix);
	if (!f) return;
	while (fgets(line, sizeof line, f)) {
		char *sp1, *sp2;
		line[strcspn(line, "\n")] = 0;
		sp1 = strchr(line, ' ');
		if (!sp1 || sp1 - line != UP_SHA_HEX) continue;
		sp2 = strchr(sp1 + 1, ' ');
		if (!sp2 || !up_path_ok(sp2 + 1)) continue;
		if (ix->n == cap) {
			idx_entry *ne;
			cap = cap ? cap * 2 : 256;
			ne = realloc(ix->e, (size_t)cap * sizeof *ne);
			if (!ne) break;
			ix->e = ne;
		}
		memcpy(ix->e[ix->n].sha, line, UP_SHA_HEX);
		ix->e[ix->n].sha[UP_SHA_HEX] = 0;
		ix->e[ix->n].path = strdup(sp2 + 1);
		if (ix->e[ix->n].path) ix->n++;
	}
	fclose(f);
	qsort(ix->e, (size_t)ix->n, sizeof *ix->e, cmp_idx);
}

static const char *index_sha(const index_t *ix, const char *path)
{
	idx_entry key = { (char *)path, "" }, *hit;
	if (!ix->n) return NULL;
	hit = bsearch(&key, ix->e, (size_t)ix->n, sizeof *ix->e, cmp_idx);
	return hit ? hit->sha : NULL;
}

static void index_free(index_t *ix)
{
	int i;
	for (i = 0; i < ix->n; i++) free(ix->e[i].path);
	free(ix->e);
	memset(ix, 0, sizeof *ix);
}

/* ---- the manifest: fetched, checked, compared ------------------------------ */

static void installed_version(char *out, size_t n)
{
	char p[PATH_MAX];
	if (G_SYS) { snprintf(out, n, "%s", C.sys_version[0] ? C.sys_version : "0"); return; }
	snprintf(p, sizeof p, "%s/VERSION", C.live);
	if (!read_small(p, out, n)) snprintf(out, n, "0");
}

/* Parse and check a manifest's text: signature, device. */
static bool manifest_load(const char *text, size_t len, up_manifest *m, char *err, size_t errn)
{
	if (!up_manifest_parse(text, len, m, err, errn)) return false;
	if (!up_manifest_verify(m, C.keys, err, errn)) { up_manifest_free(m); return false; }
	if (strcmp(m->device, C.device)) {
		snprintf(err, errn, "the list is for %s, this is %s", m->device, C.device);
		up_manifest_free(m);
		return false;
	}
	/* A system's list is never read as the folder's, nor the other way */
	if (strcmp(m->kind, G_SYS ? "system" : "folder")) {
		snprintf(err, errn, "a %s list where a %s list belongs", m->kind, G_SYS ? "system" : "folder");
		up_manifest_free(m);
		return false;
	}
	return true;
}

static const char *latest_name(void) { return G_SYS ? "latest-system.txt" : "latest.txt"; }

/* Point the staging at the system's folder, or back at ZyperOS's */
static void use_system(bool on)
{
	G_SYS = on;
	if (on) {
		/* Nothing of the running folder is copied into a system */
		snprintf(C.live, sizeof C.live, "%s/.zyperos-system-none", C.card);
		snprintf(C.next, sizeof C.next, "%s", g_sysnext);
		snprintf(S.kind, sizeof S.kind, "system");
	} else {
		snprintf(C.live, sizeof C.live, "%s", g_live_folder);
		snprintf(C.next, sizeof C.next, "%s", g_next_folder);
		S.kind[0] = 0;
	}
}

static bool manifest_from_file(const char *path, up_manifest *m, char *err, size_t errn)
{
	FILE *f = fopen(path, "r");
	char *buf;
	long n;
	bool ok;

	if (!f) { snprintf(err, errn, "no %s", path); return false; }
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	rewind(f);
	if (n <= 0 || n > (long)UP_MAX_MANIFEST || !(buf = malloc((size_t)n))) { fclose(f); snprintf(err, errn, "bad %s", path); return false; }
	ok = fread(buf, 1, (size_t)n, f) == (size_t)n;
	fclose(f);
	ok = ok && manifest_load(buf, (size_t)n, m, err, errn);
	free(buf);
	return ok;
}

/* Bytes the update downloads: every file whose content the installed folder
 * does not have, less what is already staged for this version. */
static uint64_t download_size(const up_manifest *m, int *nfiles)
{
	index_t live, staged;
	char p[PATH_MAX];
	uint64_t sum = 0;
	int i;

	snprintf(p, sizeof p, "%s/.files", C.live);
	index_read(p, &live);
	snprintf(p, sizeof p, "%s/.staged", C.next);
	index_read(p, &staged);
	*nfiles = 0;
	for (i = 0; i < m->nfiles; i++) {
		const char *a = index_sha(&live, m->files[i].path), *b = index_sha(&staged, m->files[i].path);
		struct stat sb;
		if ((a && !strcmp(a, m->files[i].sha)) || (b && !strcmp(b, m->files[i].sha))) continue;
		/* No index (a card from before System Update): as download does,
		 * a file of the same size is taken to be the same until copied */
		if (!live.n) {
			snprintf(p, sizeof p, "%s/%s", C.live, m->files[i].path);
			if (!stat(p, &sb) && (uint64_t)sb.st_size == m->files[i].size) continue;
		}
		sum += m->files[i].stored;
		(*nfiles)++;
	}
	index_free(&live);
	index_free(&staged);
	return sum;
}

/* A staging folder for another version than m (or for none) goes. */
static bool staging_is_for(const up_manifest *m)
{
	char p[PATH_MAX], v[32];
	snprintf(p, sizeof p, "%s/.ready", C.next);
	if (read_small(p, v, sizeof v)) return !strcmp(v, m->version);
	snprintf(p, sizeof p, "%s/.update", C.next);
	{
		up_manifest old;
		char err[200];
		bool same = false;
		if (manifest_from_file(p, &old, err, sizeof err)) {
			same = !strcmp(old.version, m->version);
			up_manifest_free(&old);
		}
		return same;
	}
}

static bool staged_ready(const up_manifest *m)
{
	char p[PATH_MAX], v[32];
	snprintf(p, sizeof p, "%s/.ready", C.next);
	return read_small(p, v, sizeof v) && !strcmp(v, m->version);
}

/* ---- the system slots (update/slots.c) ------------------------------------ */

static bool copy_checked(const char *src, const char *tmp, const up_file *f);

static bool g_slots;    /* a card with two system slots, booted by them */
static bool g_sys_too_big; /* the newest system does not fit this card's slots */

/* The new system is in the other slot and the next start boots it */
static bool system_pending(const up_manifest *m)
{
	char err[200];
	const char *boot;
	up_bootstate bs;
	bool yes = false;

	if (!g_slots || !(boot = up_boot_open(err, sizeof err))) return false;
	if (up_bootstate_read(boot, &bs))
		yes = bs.slot[0] == up_slot_other() && !strcmp(bs.trial, "1") &&
		      !strcmp(up_bootstate_version(&bs, up_slot_other()), m->version);
	up_boot_close();
	return yes;
}

static void sys_tick(uint64_t done, uint64_t total)
{
	static time_t last;
	S.done = done;
	S.total = total;
	if (time(NULL) != last) { last = time(NULL); up_status_write(&S); }
}

/* The staged, checked system into the slot that is not running: the root
 * written over its partition and read back, the kernel and the device tree
 * into the boot partition's folder for that slot, and bootstate.txt told to
 * boot it on trial. The running slot is not touched. */
static int install_system(const up_manifest *m)
{
	char err[300], src[PATH_MAX], dir[PATH_MAX], dst[PATH_MAX], tmp[PATH_MAX + 8], old[32], pv[32];
	const char *boot;
	const up_file *root = NULL;
	up_bootstate bs;
	char other = up_slot_other();
	int i;

	for (i = 0; i < m->nfiles; i++)
		if (!strcmp(m->files[i].path, "root.squashfs")) root = &m->files[i];
	if (!root) { say("error", "the system's list has no root.squashfs"); return 2; }
	S.files_done = 0;
	S.files_total = 0;
	/* The other slot's system goes from here: a cut write leaves it half
	 * written, so bootstate says it holds none (Roll Back does not go there)
	 * and the running slot starts next (a Roll Back waiting for a restart is
	 * off). The folder that came with that system, kept in ZyperOS.prev, goes
	 * with it. Found on the device, 2026-11-06. */
	if (!(boot = up_boot_open(err, sizeof err))) { say("error", "%s", err); return 2; }
	up_bootstate_read(boot, &bs);
	snprintf(old, sizeof old, "%s", up_bootstate_version(&bs, other));
	up_bootstate_set_version(&bs, other, "-");
	snprintf(bs.slot, sizeof bs.slot, "%c", up_slot_running());
	strcpy(bs.trial, "0");
	strcpy(bs.try_, "0");
	if (!up_bootstate_write(boot, &bs)) { up_boot_close(); say("error", "cannot write bootstate.txt"); return 2; }
	up_boot_close();
	snprintf(tmp, sizeof tmp, "%s/rollback", C.state);
	unlink(tmp);
	snprintf(tmp, sizeof tmp, "%s/VERSION", C.prev);
	if (strcmp(old, "-") && read_small(tmp, pv, sizeof pv) && !strcmp(pv, old)) rm_rf(C.prev);
	say("installing", "writing system %s into slot %c (%s)", m->version, other, up_slot_part(other));
	snprintf(src, sizeof src, "%s/root.squashfs", C.next);
	if (!up_slot_write(src, up_slot_part(other), root->size, root->sha, sys_tick, err, sizeof err)) {
		say("error", "%s", err);
		return 2;
	}
	if (!(boot = up_boot_open(err, sizeof err))) { say("error", "%s", err); return 2; }
	snprintf(dir, sizeof dir, "%s/%c", boot, other);
	mkdirs(dir);
	for (i = 0; i < m->nfiles; i++) {
		const up_file *f = &m->files[i];
		if (strncmp(f->path, "boot/", 5) || strchr(f->path + 5, '/')) continue;
		snprintf(src, sizeof src, "%s/%s", C.next, f->path);
		snprintf(dst, sizeof dst, "%s/%s", dir, f->path + 5);
		snprintf(tmp, sizeof tmp, "%s.new", dst);
		if (!copy_checked(src, tmp, f) || rename(tmp, dst)) {
			up_boot_close();
			say("error", "cannot put %s on the boot partition", f->path);
			return 2;
		}
	}
	up_bootstate_read(boot, &bs);
	snprintf(bs.slot, sizeof bs.slot, "%c", other);
	strcpy(bs.trial, "1");
	strcpy(bs.try_, "0");
	strcpy(bs.fell, "-");
	up_bootstate_set_version(&bs, other, m->version);
	if (!up_bootstate_write(boot, &bs)) {
		up_boot_close();
		say("error", "cannot write bootstate.txt");
		return 2;
	}
	up_boot_close();
	rm_rf(C.next);
	S.download = 0;
	S.done = S.total;
	say("ready", "system %s is in slot %c: restart to start it", m->version, other);
	return 0;
}

/* confirm: the launcher is up (it asks 10 s after it starts). A system on
 * trial is kept: U-Boot stops counting. And a system that U-Boot gave up on
 * is said in update/result, for System Update's page. */
static int do_confirm(void)
{
	char err[200], p[PATH_MAX];
	const char *boot;
	up_bootstate bs;
	bool changed = false;

	if (!g_slots) return 1;
	if (!(boot = up_boot_open(err, sizeof err))) { logf_("confirm: %s", err); return 2; }
	if (up_bootstate_read(boot, &bs)) {
		if (!strcmp(bs.trial, "1") && bs.slot[0] == up_slot_running()) {
			strcpy(bs.trial, "0");
			strcpy(bs.try_, "0");
			changed = true;
			logf_("system %s in slot %c started: kept", up_bootstate_version(&bs, bs.slot[0]), bs.slot[0]);
		}
		if (strcmp(bs.fell, "-")) {
			char r[64];
			snprintf(r, sizeof r, "failed %s\n", up_bootstate_version(&bs, bs.fell[0]));
			snprintf(p, sizeof p, "%s/result", C.state);
			write_file(p, r, strlen(r));
			logf_("system %s in slot %s did not start: back on slot %c", up_bootstate_version(&bs, bs.fell[0]),
			      bs.fell, up_slot_running());
			/* It holds no system Roll Back may go to */
			up_bootstate_set_version(&bs, bs.fell[0], "-");
			strcpy(bs.fell, "-");
			changed = true;
		}
		if (changed) up_bootstate_write(boot, &bs);
	}
	up_boot_close();
	/* The program's last status was about a system that has now started or
	 * failed (a "ready" from before the restart): it goes, as update-apply.sh
	 * drops it when it switches a folder. The page reads none as idle and
	 * looks again. Found on the device, 2026-11-06: after a fall back the
	 * page still offered Restart Now for the system that had failed. */
	if (changed) {
		snprintf(p, sizeof p, "%s/status", C.state);
		unlink(p);
	}
	return 0;
}

/* Roll Back on a card with slots, when the other slot holds a system: the
 * next start boots it. The ZyperOS folder that came with the newer system
 * (the image's own, switched in at its first start) goes back with it. */
static int rollback_system(void)
{
	char err[200], p[PATH_MAX], prev[32];
	const char *boot, *ver;
	up_bootstate bs;
	char other = up_slot_other();

	if (!(boot = up_boot_open(err, sizeof err))) { say("error", "%s", err); return 2; }
	up_bootstate_read(boot, &bs);
	ver = up_bootstate_version(&bs, other);
	if (!strcmp(ver, "-")) { up_boot_close(); return 1; }
	snprintf(S.version, sizeof S.version, "%s", ver);
	snprintf(bs.slot, sizeof bs.slot, "%c", other);
	strcpy(bs.trial, "0");
	strcpy(bs.try_, "0");
	if (!up_bootstate_write(boot, &bs)) { up_boot_close(); say("error", "cannot write bootstate.txt"); return 2; }
	up_boot_close();
	snprintf(p, sizeof p, "%s/VERSION", C.prev);
	if (read_small(p, prev, sizeof prev) && !strcmp(prev, S.version)) {
		snprintf(p, sizeof p, "%s/rollback", C.state);
		write_file(p, prev, strlen(prev));
	}
	sync();
	say("rollback", "restart to go back to system %s", S.version);
	return 0;
}

/* The other slot's version and the running slot, for the page */
static void slots_status(void)
{
	char err[200];
	const char *boot;
	up_bootstate bs;

	if (!g_slots) return;
	snprintf(S.slot, sizeof S.slot, "%c", up_slot_running());
	if (!(boot = up_boot_open(err, sizeof err))) return;
	if (up_bootstate_read(boot, &bs)) {
		const char *v = up_bootstate_version(&bs, up_slot_other());
		if (strcmp(v, "-")) snprintf(S.other, sizeof S.other, "%s", v);
	}
	up_boot_close();
}

/* A newer system's list, checked and kept, is waiting for download */
static bool system_offered(void)
{
	char p[PATH_MAX], err[300];
	up_manifest m;
	bool yes;

	use_system(true);
	snprintf(p, sizeof p, "%s/%s", C.state, latest_name());
	yes = manifest_from_file(p, &m, err, sizeof err);
	if (yes) {
		yes = up_version_cmp(m.version, C.sys_version) > 0;
		up_manifest_free(&m);
	}
	use_system(false);
	return yes;
}

/* check: the newest release's list, and what it means for this device.
 * Returns 0 when there is something to install (or it is staged), 1 when
 * up to date, 2 otherwise. On 0 the checked list is kept as latest.txt, for
 * download. */
static int do_check(up_manifest *m, bool keep)
{
	char url[1200], err[300], p[PATH_MAX];
	struct membuf b;
	int nfiles = 0;

	installed_version(S.installed, sizeof S.installed);
	if (!read_system()) {
		say("unsupported", "this system image has no System Update (no %s): use the ZyperOS Installer", C.system);
		return 2;
	}
	say("checking", NULL);
	manifest_url(url, sizeof url);
	if (!fetch_text(url, &b, err, sizeof err)) {
		say("error", "could not read %s: %s", url, err);
		return 2;
	}
	if (!manifest_load(b.p, b.n, m, err, sizeof err)) {
		free(b.p);
		say("error", "%s", err);
		return 2;
	}
	snprintf(S.version, sizeof S.version, "%s", m->version);
	S.size = m->total;
	if (up_version_cmp(m->version, S.installed) <= 0) {
		free(b.p);
		say("uptodate", "%s is the newest release", m->version);
		return 1;
	}
	if (!G_SYS && up_version_cmp(m->min_system, C.sys_version) > 0) {
		free(b.p);
		say("needs-system", "%s needs system %s; this one is %s", m->version, m->min_system, C.sys_version);
		return 2;
	}
	/* A card keeps the slot size it was laid out with (docs/updates.md): a
	 * system that outgrew it goes on only by a fresh install, said here
	 * before anything is fetched */
	if (G_SYS) {
		uint64_t cap = 0;
		int i;
		for (i = 0; i < m->nfiles; i++) {
			if (strcmp(m->files[i].path, "root.squashfs")) continue;
			if (up_slot_size(up_slot_other(), &cap) && m->files[i].size > cap) {
				free(b.p);
				g_sys_too_big = true;
				say("too-big", "System %s does not fit this card's system slots (%llu MB, it needs %llu MB). "
				    "The ZyperOS Installer can install it fresh, which erases the card.", m->version,
				    (unsigned long long)(cap >> 20), (unsigned long long)((m->files[i].size + (1 << 20) - 1) >> 20));
				return 2;
			}
		}
	}
	if (keep) {
		snprintf(p, sizeof p, "%s/%s", C.state, latest_name());
		write_file(p, b.p, b.n);
	}
	free(b.p);
	if (G_SYS && system_pending(m)) {
		S.download = 0;
		say("ready", "system %s is in slot %c: restart to start it", m->version, up_slot_other());
		return 0;
	}
	/* A system staged but not in its slot (the install stopped) is still to
	 * do: Download installs it without fetching */
	if (!G_SYS && staged_ready(m)) {
		S.download = 0;
		say("ready", "%s is downloaded and checked: restart to install it", m->version);
		return 0;
	}
	S.download = download_size(m, &nfiles);
	if (!S.download) S.download = 1;   /* nothing to fetch still has a step */
	say("available", "%s: %d files, %llu bytes to download", m->version, nfiles, (unsigned long long)S.download);
	return 0;
}

/* ---- staging ------------------------------------------------------------- */

typedef struct {
	const up_manifest *m;
	FILE *journal;
	uint64_t done, total;
	int files_done, files_total;
	int bad;
} stage_t;

static void progress(stage_t *st, bool force)
{
	static struct timespec last;
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	if (!force && (now.tv_sec - last.tv_sec) * 1000 + (now.tv_nsec - last.tv_nsec) / 1000000 < 250) return;
	last = now;
	S.done = st->done;
	S.total = st->total;
	S.files_done = st->files_done;
	S.files_total = st->files_total;
	up_status_write(&S);
}

static void next_path(const up_file *f, char *out, size_t n, const char *suffix)
{
	snprintf(out, n, "%s/%s%s", C.next, f->path, suffix ? suffix : "");
}

/* A file is in place: synced, dropped from the cache (so the final check
 * reads the card), and written to the journal. */
static bool file_placed(stage_t *st, const up_file *f, const char *tmp)
{
	char dst[PATH_MAX];
	next_path(f, dst, sizeof dst, NULL);
	if (rename(tmp, dst)) return false;
	fprintf(st->journal, "%s %llu %s\n", f->sha, (unsigned long long)f->size, f->path);
	fflush(st->journal);
	st->files_done++;
	return true;
}

/* Copy src to tmp, hashing as it goes; true when the copy is f. */
static bool copy_checked(const char *src, const char *tmp, const up_file *f)
{
	unsigned char buf[1 << 16], d[32];
	unsigned int dn;
	char hex[UP_SHA_HEX + 1];
	int in = open(src, O_RDONLY), out;
	EVP_MD_CTX *ctx;
	ssize_t r = 0;
	uint64_t total = 0;
	bool ok = false;

	if (in < 0) return false;
	out = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0755);
	if (out < 0) { close(in); return false; }
	ctx = EVP_MD_CTX_new();
	if (ctx && EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) == 1) {
		while (!g_stop && (r = read(in, buf, sizeof buf)) > 0) {
			if (write(out, buf, (size_t)r) != r || EVP_DigestUpdate(ctx, buf, (size_t)r) != 1) { r = -1; break; }
			total += (uint64_t)r;
		}
		if (r == 0 && EVP_DigestFinal_ex(ctx, d, &dn) == 1) {
			up_sha256_hex(d, hex);
			ok = total == f->size && !strcmp(hex, f->sha);
		}
	}
	EVP_MD_CTX_free(ctx);
	close(in);
	if (fsync(out)) ok = false;
#ifdef POSIX_FADV_DONTNEED
	posix_fadvise(out, 0, 0, POSIX_FADV_DONTNEED);
#endif
	if (close(out)) ok = false;
	if (!ok) unlink(tmp);
	return ok;
}

/* Turn a downloaded member (.zpart) into the file, checked. */
static bool finish_member(stage_t *st, const up_file *f)
{
	char part[PATH_MAX], tmp[PATH_MAX];
	bool ok = false;

	next_path(f, part, sizeof part, ".zpart");
	next_path(f, tmp, sizeof tmp, ".tmp");
	if (f->method == 'r') {
		ok = copy_checked(part, tmp, f);
	} else {
		unsigned char in[1 << 16], outb[1 << 16], d[32];
		unsigned int dn;
		char hex[UP_SHA_HEX + 1];
		int fi = open(part, O_RDONLY), fo = -1, zr = Z_OK;
		EVP_MD_CTX *ctx = EVP_MD_CTX_new();
		z_stream z;
		uint64_t total = 0;
		ssize_t r = 0;

		memset(&z, 0, sizeof z);
		if (fi >= 0) fo = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0755);
		if (fi >= 0 && fo >= 0 && ctx && EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) == 1 &&
		    inflateInit2(&z, 16 + MAX_WBITS) == Z_OK) {
			ok = true;
			while (ok && zr != Z_STREAM_END && (r = read(fi, in, sizeof in)) > 0) {
				z.next_in = in;
				z.avail_in = (uInt)r;
				do {
					size_t got;
					z.next_out = outb;
					z.avail_out = sizeof outb;
					zr = inflate(&z, Z_NO_FLUSH);
					/* No progress possible: the last call filled the output
					 * just as this input ran out. Not an error (zlib.h): read
					 * on. Found 2026-11-06 at 59 MB into a 331 MB root, packed
					 * from LZ4 squashfs, so mostly stored blocks */
					if (zr == Z_BUF_ERROR && z.avail_in == 0) zr = Z_OK;
					if (zr != Z_OK && zr != Z_STREAM_END) { ok = false; break; }
					got = sizeof outb - z.avail_out;
					/* Never more than the list says: a member that inflates
					 * past its file's size is refused before it fills the card */
					if (got > f->size - total) { ok = false; break; }
					if (got && (write(fo, outb, got) != (ssize_t)got || EVP_DigestUpdate(ctx, outb, got) != 1)) { ok = false; break; }
					total += got;
				} while (z.avail_out == 0 || (z.avail_in > 0 && zr != Z_STREAM_END));
			}
			inflateEnd(&z);
			if (ok && zr == Z_STREAM_END && EVP_DigestFinal_ex(ctx, d, &dn) == 1) {
				up_sha256_hex(d, hex);
				ok = total == f->size && !strcmp(hex, f->sha);
			} else {
				ok = false;
			}
		}
		EVP_MD_CTX_free(ctx);
		if (fi >= 0) close(fi);
		if (fo >= 0) {
			if (fsync(fo)) ok = false;
#ifdef POSIX_FADV_DONTNEED
			posix_fadvise(fo, 0, 0, POSIX_FADV_DONTNEED);
#endif
			if (close(fo)) ok = false;
		}
		if (!ok) unlink(tmp);
	}
	unlink(part);
	if (!ok) {
		logf_("%s did not match the list after download", f->path);
		st->bad++;
		return false;
	}
	return file_placed(st, f, tmp);
}

/* One ranged request: the members of files[0..n), in pack order, the first
 * maybe partly there already. */
typedef struct {
	stage_t *st;
	const up_file **files;
	uint64_t *have;           /* bytes of each member already in its .zpart */
	int n, cur;
	uint64_t start, pos;
	bool started;
	int fd;
} run_t;

static size_t run_write(char *data, size_t sz, size_t nm, void *u)
{
	run_t *r = u;
	size_t len = sz * nm, left = len;

	if (g_stop) return 0;
	if (!r->started) {
		long code = 0;
		curl_easy_getinfo(g_curl, CURLINFO_RESPONSE_CODE, &code);
		/* 206 (or a file:// read, code 0) starts where it was asked; a
		 * server that ignored the range sends the whole pack from 0 */
		r->pos = code == 200 ? 0 : r->start;
		r->started = true;
	}
	while (left && r->cur < r->n) {
		const up_file *f = r->files[r->cur];
		uint64_t from = f->offset + r->have[r->cur], end = f->offset + f->stored;
		size_t take;

		if (r->pos < from) {                      /* a gap, or bytes already had */
			uint64_t skip = from - r->pos;
			take = skip < left ? (size_t)skip : left;
			r->pos += take; data += take; left -= take;
			continue;
		}
		if (r->fd < 0) {
			char part[PATH_MAX];
			next_path(f, part, sizeof part, ".zpart");
			r->fd = open(part, O_WRONLY | O_CREAT | O_APPEND, 0644);
			if (r->fd < 0) return 0;
		}
		take = end - r->pos < left ? (size_t)(end - r->pos) : left;
		if (write(r->fd, data, take) != (ssize_t)take) return 0;
		r->pos += take; data += take; left -= take;
		r->st->done += take;
		r->have[r->cur] += take;
		if (r->pos == end) {
			close(r->fd);
			r->fd = -1;
			finish_member(r->st, f);
			r->cur++;
		}
		progress(r->st, false);
	}
	return len;
}

static int cmp_offset(const void *a, const void *b)
{
	const up_file *x = *(const up_file *const *)a, *y = *(const up_file *const *)b;
	return x->offset < y->offset ? -1 : x->offset > y->offset;
}

static bool fetch_members(stage_t *st, const up_manifest *m, const up_file **need, int n, char *err, size_t errn)
{
	char url[1400];
	uint64_t *have = calloc((size_t)(n ? n : 1), sizeof *have);
	int i = 0;
	bool ok = true;

	if (!have) { snprintf(err, errn, "out of memory"); return false; }
	pack_url(m, url, sizeof url);
	qsort(need, (size_t)n, sizeof *need, cmp_offset);
	for (i = 0; i < n; i++) {
		char part[PATH_MAX];
		struct stat sb;
		next_path(need[i], part, sizeof part, ".zpart");
		if (!stat(part, &sb) && (uint64_t)sb.st_size <= need[i]->stored) have[i] = (uint64_t)sb.st_size;
		else unlink(part);
		st->done += have[i];
	}
	i = 0;
	while (ok && i < n && !g_stop) {
		/* A run: from file i on, while the next member starts close to where
		 * the last ended, and always a new run at a member already begun */
		int j = i + 1;
		uint64_t end = need[i]->offset + need[i]->stored;
		run_t r;
		char range[64], eb[CURL_ERROR_SIZE] = "";
		CURLcode rc;

		if (have[i] == need[i]->stored) {     /* downloaded, not yet decoded */
			if (!need[i]->stored) {           /* an empty file: nothing to fetch */
				char part[PATH_MAX];
				int fd;
				next_path(need[i], part, sizeof part, ".zpart");
				if ((fd = open(part, O_WRONLY | O_CREAT | O_TRUNC, 0644)) >= 0) close(fd);
			}
			finish_member(st, need[i]);
			i++;
			continue;
		}
		while (j < n && have[j] == 0 && need[j]->stored && need[j]->offset >= end && need[j]->offset - end <= RUN_GAP) {
			end = need[j]->offset + need[j]->stored;
			j++;
		}
		memset(&r, 0, sizeof r);
		r.st = st;
		r.files = need + i;
		r.have = have + i;
		r.n = j - i;
		r.start = need[i]->offset + have[i];
		r.fd = -1;
		snprintf(range, sizeof range, "%llu-%llu", (unsigned long long)r.start, (unsigned long long)(end - 1));
		curl_common(g_curl);
		curl_easy_setopt(g_curl, CURLOPT_URL, url);
		curl_easy_setopt(g_curl, CURLOPT_RANGE, range);
		curl_easy_setopt(g_curl, CURLOPT_WRITEFUNCTION, run_write);
		curl_easy_setopt(g_curl, CURLOPT_WRITEDATA, &r);
		curl_easy_setopt(g_curl, CURLOPT_ERRORBUFFER, eb);
		curl_easy_setopt(g_curl, CURLOPT_NOPROGRESS, 0L);
		curl_easy_setopt(g_curl, CURLOPT_XFERINFOFUNCTION, progress_cb);
		rc = curl_easy_perform(g_curl);
		if (r.fd >= 0) { fsync(r.fd); close(r.fd); }
		if (rc != CURLE_OK || r.cur < r.n) {
			snprintf(err, errn, "%s", g_stop ? "stopped" : eb[0] ? eb : rc != CURLE_OK ? curl_easy_strerror(rc) : "the answer ended early");
			ok = false;
		}
		i = j;
	}
	free(have);
	if (g_stop) { snprintf(err, errn, "stopped"); ok = false; }
	return ok;
}

/* Everything in next/ that the list does not name goes: a file of an older
 * staging, a .tmp a power cut left. */
static void sweep(const up_manifest *m, const char *dir, const char *rel)
{
	DIR *d = opendir(dir);
	struct dirent *e;
	if (!d) return;
	while ((e = readdir(d))) {
		char full[PATH_MAX], r[PATH_MAX];
		struct stat sb;
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
		snprintf(full, sizeof full, "%s/%s", dir, e->d_name);
		snprintf(r, sizeof r, "%s%s%s", rel, *rel ? "/" : "", e->d_name);
		if (!*rel && (!strcmp(e->d_name, ".update") || !strcmp(e->d_name, ".staged"))) continue;
		if (lstat(full, &sb)) continue;
		if (S_ISDIR(sb.st_mode)) { sweep(m, full, r); continue; }
		{
			int lo = 0, hi = m->nfiles - 1, found = 0;
			while (lo <= hi) {
				int mid = (lo + hi) / 2, c = strcmp(r, m->files[mid].path);
				if (!c) { found = 1; break; }
				if (c < 0) hi = mid - 1; else lo = mid + 1;
			}
			if (!found) unlink(full);
		}
	}
	closedir(d);
}

static bool enough_space(uint64_t need)
{
	struct statvfs v;
	if (statvfs(C.card, &v)) return true;
	return (uint64_t)v.f_bavail * v.f_frsize >= need + SPACE_MARGIN;
}

/* download: stage the update in next/, check it, mark it ready. */
static int do_download(void)
{
	up_manifest m;
	char p[PATH_MAX], err[400];
	index_t live, staged;
	stage_t st;
	const up_file **need = NULL;
	int nneed = 0, i, pass, pct;
	bool charging;
	uint64_t space = 0;

	installed_version(S.installed, sizeof S.installed);
	if (!read_system()) {
		say("unsupported", "this system image has no System Update (no %s): use the ZyperOS Installer", C.system);
		return 2;
	}
	snprintf(p, sizeof p, "%s/%s", C.state, latest_name());
	if (!manifest_from_file(p, &m, err, sizeof err)) {
		int rc = do_check(&m, true);
		if (rc) return rc;
	}
	snprintf(S.version, sizeof S.version, "%s", m.version);
	S.size = m.total;
	if (up_version_cmp(m.version, S.installed) <= 0) { say("uptodate", "%s is installed", m.version); up_manifest_free(&m); return 1; }
	if (!G_SYS && up_version_cmp(m.min_system, C.sys_version) > 0) {
		say("needs-system", "%s needs system %s; this one is %s", m.version, m.min_system, C.sys_version);
		up_manifest_free(&m);
		return 2;
	}
	if (G_SYS && system_pending(&m)) {
		say("ready", "system %s is in slot %c: restart to start it", m.version, up_slot_other());
		up_manifest_free(&m);
		return 0;
	}
	if (staged_ready(&m)) {
		int rc = 0;
		if (G_SYS) rc = install_system(&m);
		else say("ready", "%s is downloaded and checked: restart to install it", m.version);
		up_manifest_free(&m);
		return rc;
	}
	battery(&pct, &charging);
	if (!charging && pct < C.min_battery) {
		say("battery", "battery %d%%: connect the charger to download", pct);
		up_manifest_free(&m);
		return 2;
	}

	{
		struct stat sb;
		if (!stat(C.next, &sb) && !staging_is_for(&m)) {
			logf_("a staging for another version is there: removed");
			rm_rf(C.next);
		}
	}
	if (mkdirs(C.next)) { say("error", "cannot make %s: %s", C.next, strerror(errno)); up_manifest_free(&m); return 2; }
	{
		/* For the line under the versions while it works */
		int nf;
		S.download = download_size(&m, &nf);
	}
	snprintf(p, sizeof p, "%s/.ready", C.next);
	unlink(p);
	snprintf(p, sizeof p, "%s/.update", C.next);
	if (!write_file(p, m.text, m.text_len)) { say("error", "cannot write in %s", C.next); up_manifest_free(&m); return 2; }

	for (pass = 0; pass < 2; pass++) {
		memset(&st, 0, sizeof st);
		st.m = &m;
		snprintf(p, sizeof p, "%s/.files", C.live);
		index_read(p, &live);
		snprintf(p, sizeof p, "%s/.staged", C.next);
		index_read(p, &staged);
		st.journal = fopen(p, "a");
		if (!st.journal) { say("error", "cannot write %s", p); index_free(&live); index_free(&staged); up_manifest_free(&m); return 2; }
		free(need);
		need = calloc((size_t)m.nfiles, sizeof *need);
		nneed = 0;
		space = 0;
		st.files_total = m.nfiles;

		/* What is there already, then what the installed folder has */
		say("copying", NULL);
		for (i = 0; i < m.nfiles && !g_stop; i++) {
			const up_file *f = &m.files[i];
			const char *was = index_sha(&staged, f->path), *had = index_sha(&live, f->path);
			char dst[PATH_MAX], src[PATH_MAX], tmp[PATH_MAX];
			struct stat sb;

			next_path(f, dst, sizeof dst, NULL);
			if (was && !strcmp(was, f->sha) && !stat(dst, &sb) && (uint64_t)sb.st_size == f->size) {
				st.files_done++;
				continue;
			}
			if (mkparent(dst)) { say("error", "cannot make the folder for %s", f->path); goto fail; }
			snprintf(src, sizeof src, "%s/%s", C.live, f->path);
			/* With an index, only what it says is the same; without one
			 * (a card from before System Update), anything the same size */
			if ((had && !strcmp(had, f->sha)) || (!live.n && !stat(src, &sb) && (uint64_t)sb.st_size == f->size)) {
				if (!enough_space(f->size)) { say("space", "the card is full: %s needs room", f->path); goto fail; }
				next_path(f, tmp, sizeof tmp, ".tmp");
				if (copy_checked(src, tmp, f) && file_placed(&st, f, tmp)) {
					progress(&st, false);
					continue;
				}
			}
			need[nneed++] = f;
			st.total += f->stored;
			space += f->size + f->stored;
		}
		if (g_stop) goto stopped;
		index_free(&live);
		index_free(&staged);

		if (nneed) {
			if (!enough_space(space)) {
				say("space", "the card needs %llu MB free for the update", (unsigned long long)((space + SPACE_MARGIN) >> 20));
				goto fail;
			}
			say("downloading", "%d files, %llu bytes", nneed, (unsigned long long)st.total);
			if (!fetch_members(&st, &m, need, nneed, err, sizeof err)) {
				if (g_stop) goto stopped;
				fclose(st.journal);
				S.done = st.done;
				say("stopped", "the download stopped: %s", err);
				free(need);
				up_manifest_free(&m);
				return 2;
			}
		}
		fclose(st.journal);
		st.journal = NULL;

		/* Every file again, off the card, against the signed list */
		S.total = m.total;
		S.done = 0;
		S.files_done = 0;
		say("verifying", NULL);
		st.bad = 0;
		{
			FILE *j;
			snprintf(p, sizeof p, "%s/.staged", C.next);
			j = fopen(p, "w");
			for (i = 0; i < m.nfiles && !g_stop; i++) {
				const up_file *f = &m.files[i];
				char dst[PATH_MAX], hex[UP_SHA_HEX + 1];
				struct stat sb;
				next_path(f, dst, sizeof dst, NULL);
				if (!stat(dst, &sb) && (uint64_t)sb.st_size == f->size && up_sha256_file(dst, hex, true) &&
				    !strcmp(hex, f->sha)) {
					if (j) fprintf(j, "%s %llu %s\n", f->sha, (unsigned long long)f->size, f->path);
				} else {
					logf_("%s did not match the list on the card", f->path);
					unlink(dst);
					st.bad++;
				}
				S.done += f->size;
				S.files_done = i + 1;
				st.done = S.done;
				st.total = S.total;
				st.files_done = S.files_done;
				st.files_total = m.nfiles;
				progress(&st, false);
			}
			if (j) { fflush(j); fsync(fileno(j)); fclose(j); }
		}
		if (g_stop) goto stopped;
		if (!st.bad) break;
		logf_("%d files did not match: getting them again", st.bad);
	}
	if (st.bad) {
		say("error", "%d files did not match the list twice", st.bad);
		free(need);
		up_manifest_free(&m);
		return 2;
	}

	for (i = 0; i < m.ndirs; i++) {
		snprintf(p, sizeof p, "%s/%s", C.next, m.dirs[i]);
		mkdirs(p);
	}
	sweep(&m, C.next, "");
	/* The new folder's index, as payload.sh writes one: it is what the next
	 * update compares against */
	{
		FILE *ix;
		snprintf(p, sizeof p, "%s/.files", C.next);
		ix = fopen(p, "w");
		if (!ix) { say("error", "cannot write %s", p); goto fail_nojournal; }
		for (i = 0; i < m.nfiles; i++)
			fprintf(ix, "%s %llu %s\n", m.files[i].sha, (unsigned long long)m.files[i].size, m.files[i].path);
		if (fflush(ix) || fsync(fileno(ix))) { fclose(ix); say("error", "cannot write %s", p); goto fail_nojournal; }
		fclose(ix);
	}
	snprintf(p, sizeof p, "%s/.staged", C.next);
	unlink(p);
	sync();
	{
		char v[40];
		snprintf(v, sizeof v, "%s\n", m.version);
		snprintf(p, sizeof p, "%s/.ready", C.next);
		if (!write_file(p, v, strlen(v))) { say("error", "cannot write %s", p); goto fail_nojournal; }
	}
	sync();
	if (G_SYS) {
		int rc = install_system(&m);
		free(need);
		up_manifest_free(&m);
		return rc;
	}
	S.done = S.total;
	say("ready", "%s is downloaded and checked: restart to install it", m.version);
	free(need);
	up_manifest_free(&m);
	return 0;

stopped:
	if (st.journal) fclose(st.journal);
	say("stopped", "stopped: it goes on from here next time");
	free(need);
	up_manifest_free(&m);
	return 2;
fail:
	if (st.journal) fclose(st.journal);
	index_free(&live);
	index_free(&staged);
fail_nojournal:
	free(need);
	up_manifest_free(&m);
	return 2;
}

/* rollback: the swap happens at the next start (update/apply.sh). Refused
 * when the previous version is older than the system image's own ZyperOS,
 * which /etc/init.d/zyperos would copy straight back over it. */
static int do_rollback(void)
{
	char p[PATH_MAX], v[32];

	installed_version(S.installed, sizeof S.installed);
	if (!read_system()) { say("unsupported", "this system image has no System Update"); return 2; }
	snprintf(p, sizeof p, "%s/VERSION", C.prev);
	if (!read_small(p, v, sizeof v)) { say("error", "there is no previous version to go back to"); return 2; }
	snprintf(S.version, sizeof S.version, "%s", v);
	if (up_version_cmp(v, C.sys_version) < 0) {
		say("error", "%s is older than this system image's %s", v, C.sys_version);
		return 2;
	}
	snprintf(p, sizeof p, "%s/rollback", C.state);
	if (!write_file(p, v, strlen(v))) { say("error", "cannot write %s", p); return 2; }
	sync();
	say("rollback", "restart to go back to %s", v);
	return 0;
}

static void usage(void)
{
	fprintf(stderr, "usage: zyperos-update check|download|rollback|confirm|status [--card DIR] [--device NAME]\n"
	                "       [--source URL] [--cacert FILE] [--keys DIR] [--system FILE] [--min-battery PCT]\n");
	exit(2);
}

int main(int argc, char **argv)
{
	const char *cmd = NULL, *card = "/mnt/SDCARD", *device = "pixel2";
	const char *source = NULL, *cacert = NULL, *keys = NULL, *sys = NULL;
	char p[PATH_MAX];
	int i, lock, rc = 2;

	C.min_battery = 25;
	for (i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (!strcmp(a, "--card") && i + 1 < argc) card = argv[++i];
		else if (!strcmp(a, "--device") && i + 1 < argc) device = argv[++i];
		else if (!strcmp(a, "--source") && i + 1 < argc) source = argv[++i];
		else if (!strcmp(a, "--cacert") && i + 1 < argc) cacert = argv[++i];
		else if (!strcmp(a, "--keys") && i + 1 < argc) keys = argv[++i];
		else if (!strcmp(a, "--system") && i + 1 < argc) sys = argv[++i];
		else if (!strcmp(a, "--min-battery") && i + 1 < argc) C.min_battery = atoi(argv[++i]);
		else if (a[0] != '-' && !cmd) cmd = a;
		else usage();
	}
	if (!cmd) usage();
	if (!up_path_ok(device) || strchr(device, '/')) usage();
	/* Every path this builds is the card's (at most 1 KB), a folder name,
	 * and a list's path (at most UP_MAX_PATH): well inside PATH_MAX, so no
	 * snprintf below can cut one short */
	if (strlen(card) > 1024) usage();
	snprintf(C.card, sizeof C.card, "%s", card);
	snprintf(C.device, sizeof C.device, "%s", device);
	snprintf(C.live, sizeof C.live, "%s/ZyperOS", card);
	snprintf(C.next, sizeof C.next, "%s/ZyperOS.next", card);
	snprintf(g_live_folder, sizeof g_live_folder, "%s", C.live);
	snprintf(g_next_folder, sizeof g_next_folder, "%s", C.next);
	snprintf(g_sysnext, sizeof g_sysnext, "%s/.zyperos-system", card);
	snprintf(C.prev, sizeof C.prev, "%s/ZyperOS.prev", card);
	snprintf(C.state, sizeof C.state, "%s/.userdata/%s/update", card, device);
	snprintf(C.keys, sizeof C.keys, "%s", keys ? keys : getenv("ZYPEROS_UPDATE_KEYS") ? getenv("ZYPEROS_UPDATE_KEYS") : "/etc/zyperos/update-keys");
	snprintf(C.system, sizeof C.system, "%s", sys ? sys : getenv("ZYPEROS_UPDATE_SYSTEM") ? getenv("ZYPEROS_UPDATE_SYSTEM") : "/etc/zyperos/system");
	snprintf(C.cacert, sizeof C.cacert, "%s/cacert.pem", C.live);
	snprintf(C.source, sizeof C.source, "%s", DEFAULT_SOURCE);
	/* A test release: the state folder's "source" file, a URL on its first
	 * line and "cacert <file>" on a second, for a server that is not GitHub.
	 * The signature is checked all the same. */
	{
		FILE *f;
		char line[1024];
		snprintf(p, sizeof p, "%s/source", C.state);
		if ((f = fopen(p, "r"))) {
			if (fgets(line, sizeof line, f)) {
				line[strcspn(line, "\r\n")] = 0;
				if (line[0]) { snprintf(C.source, sizeof C.source, "%s", line); C.source_given = true; }
			}
			if (fgets(line, sizeof line, f) && !strncmp(line, "cacert ", 7)) {
				line[strcspn(line, "\r\n")] = 0;
				snprintf(C.cacert, sizeof C.cacert, "%s", line + 7);
			}
			fclose(f);
		}
	}
	if (source) { snprintf(C.source, sizeof C.source, "%s", source); C.source_given = true; }
	if (cacert) snprintf(C.cacert, sizeof C.cacert, "%s", cacert);
	if (C.source[0] && C.source[strlen(C.source) - 1] != '/' && strlen(C.source) + 1 < sizeof C.source)
		strcat(C.source, "/");

	if (mkdirs(C.state)) { fprintf(stderr, "zyperos-update: cannot make %s\n", C.state); return 2; }
	snprintf(p, sizeof p, "%s/status", C.state);
	S.path = strdup(p);
	if (!strcmp(cmd, "status")) {
		FILE *f = fopen(p, "r");
		int c;
		if (!f) { puts("state idle"); return 0; }
		while ((c = fgetc(f)) != EOF) putchar(c);
		fclose(f);
		return 0;
	}
	snprintf(p, sizeof p, "%s/lock", C.state);
	lock = open(p, O_RDWR | O_CREAT, 0644);
	if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB)) {
		fprintf(stderr, "zyperos-update: another copy is running\n");
		return 3;
	}
	snprintf(p, sizeof p, "%s/log", C.state);
	{
		struct stat sb;
		if (!stat(p, &sb) && sb.st_size > (256 << 10)) {
			char old[PATH_MAX + 4];
			snprintf(old, sizeof old, "%s.1", p);
			rename(p, old);
		}
	}
	g_log = fopen(p, "a");
	signal(SIGTERM, on_signal);
	signal(SIGINT, on_signal);
	signal(SIGHUP, on_signal);
	signal(SIGPIPE, SIG_IGN);
	curl_global_init(CURL_GLOBAL_DEFAULT);
	g_curl = curl_easy_init();
	if (!g_curl) { fprintf(stderr, "zyperos-update: no curl\n"); return 2; }

	g_slots = up_slots_init();
	if (!strcmp(cmd, "check")) {
		up_manifest m;
		/* A card with slots: a newer system first, which brings its own
		 * ZyperOS folder (the image's, switched in at its first start).
		 * A release with no system list, or none newer, leaves the folder's
		 * check to say what there is. */
		rc = 1;
		if (g_slots && read_system()) {
			use_system(true);
			slots_status();
			rc = do_check(&m, true);
			if (rc != 2 && S.version[0]) up_manifest_free(&m);
			if (rc != 0) {
				char p[PATH_MAX];
				snprintf(p, sizeof p, "%s/%s", C.state, latest_name());
				unlink(p);
			}
		}
		/* A system too big for the card is the news: the folder's check
		 * would only say it needs that system */
		if (rc != 0 && !g_sys_too_big) {
			use_system(false);
			S.version[0] = 0;
			S.download = S.size = 0;
			rc = do_check(&m, true);
			if (rc != 2 && S.version[0]) up_manifest_free(&m);
		}
	} else if (!strcmp(cmd, "download")) {
		/* Behind the launcher and a running game */
		setpriority(PRIO_PROCESS, 0, 10);
		read_system();
		use_system(g_slots && system_offered());
		slots_status();
		rc = do_download();
	} else if (!strcmp(cmd, "rollback")) {
		read_system();
		rc = g_slots ? rollback_system() : 1;
		if (rc == 1) rc = do_rollback();
	} else if (!strcmp(cmd, "confirm")) {
		rc = do_confirm();
	} else {
		usage();
	}
	curl_easy_cleanup(g_curl);
	curl_global_cleanup();
	if (g_log) fclose(g_log);
	return rc;
}
