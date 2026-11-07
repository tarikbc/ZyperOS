/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* The two system slots (docs/updates.md, layer 2): which one runs, the
 * partitions, the boot partition and its bootstate.txt, and writing a slot.
 *
 * A card with slots has root A in partition 2 and root B in partition 4,
 * each slot's kernel and device tree in /a and /b of partition 1, and
 * /bootstate.txt there: slot=, trial=, try=, fell=, a=, b= (the version in
 * each slot). U-Boot's boot.scr (plastron's board/zyperos/boot-slots.cmd)
 * reads it, counts trial starts, and goes back to the other slot after three;
 * it tells the kernel zyperos.slot= and zyperos.trial=.
 *
 * Tests point everything at files: ZYPEROS_UPDATE_CMDLINE (a file standing
 * for /proc/cmdline), ZYPEROS_UPDATE_BOOT (a folder standing for the mounted
 * boot partition), ZYPEROS_UPDATE_PART2 and ZYPEROS_UPDATE_PART4 (files
 * standing for the root partitions). */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __linux__
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#endif

#include <openssl/evp.h>

#include "update.h"

#define BOOT_MOUNT "/run/zyperos-boot"

static char g_part[2][256], g_bootdev[256], g_bootdir[256], g_slot;
static bool g_mounted, g_trial;

static bool read_line_file(const char *path, char *out, size_t n)
{
	FILE *f = fopen(path, "r");
	size_t got;
	if (!f) return false;
	got = fread(out, 1, n - 1, f);
	fclose(f);
	out[got] = 0;
	return true;
}

/* The card's disk, as /etc/default/datapart names it (CARD=), and its
 * partition n: /dev/mmcblk0p2, /dev/vda4 */
static void part_name(const char *card, int n, char *out, size_t len)
{
	size_t l = strlen(card);
	bool digit = l && card[l - 1] >= '0' && card[l - 1] <= '9';
	snprintf(out, len, "%s%s%d", card, digit ? "p" : "", n);
}

bool up_slots_init(void)
{
	char text[2048], card[128] = "/dev/mmcblk0";
	const char *cmd = getenv("ZYPEROS_UPDATE_CMDLINE"), *s;
	struct stat sb;

	g_slot = 0;
	g_trial = false;
	if (!read_line_file(cmd ? cmd : "/proc/cmdline", text, sizeof text)) return false;
	s = strstr(text, "zyperos.slot=");
	if (!s || (s[13] != 'a' && s[13] != 'b')) return false;
	g_slot = s[13];
	s = strstr(text, "zyperos.trial=");
	g_trial = s && s[14] == '1';

	if (read_line_file("/etc/default/datapart", text, sizeof text)) {
		/* The value after CARD=, on the first line or after a newline.
		 * Never text - 1 for the first line's: a pointer before the
		 * array is undefined even unused. */
		char *c = strstr(text, "\nCARD=");
		if (c) c += 6;
		else if (!strncmp(text, "CARD=", 5)) c = text + 5;
		if (c) {
			c[strcspn(c, "\n \t\"'")] = 0;
			if (*c) snprintf(card, sizeof card, "%s", c);
		}
	}
	part_name(card, 1, g_bootdev, sizeof g_bootdev);
	part_name(card, 2, g_part[0], sizeof g_part[0]);
	part_name(card, 4, g_part[1], sizeof g_part[1]);
	if ((s = getenv("ZYPEROS_UPDATE_PART2"))) snprintf(g_part[0], sizeof g_part[0], "%s", s);
	if ((s = getenv("ZYPEROS_UPDATE_PART4"))) snprintf(g_part[1], sizeof g_part[1], "%s", s);
	/* A card from before the slots boots slot a and has no partition 4 */
	return !stat(g_part[0], &sb) && !stat(g_part[1], &sb);
}

char up_slot_running(void) { return g_slot; }
bool up_slot_trial(void) { return g_trial; }
char up_slot_other(void) { return g_slot == 'b' ? 'a' : 'b'; }
const char *up_slot_part(char slot) { return g_part[slot == 'b' ? 1 : 0]; }

const char *up_boot_open(char *err, size_t errn)
{
	const char *dir = getenv("ZYPEROS_UPDATE_BOOT");

	if (dir) {
		snprintf(g_bootdir, sizeof g_bootdir, "%s", dir);
		return g_bootdir;
	}
#ifdef __linux__
	mkdir(BOOT_MOUNT, 0700);
	if (mount(g_bootdev, BOOT_MOUNT, "vfat", MS_NOATIME | MS_NOSUID | MS_NODEV, "")) {
		snprintf(err, errn, "cannot mount %s: %s", g_bootdev, strerror(errno));
		return NULL;
	}
	g_mounted = true;
	snprintf(g_bootdir, sizeof g_bootdir, "%s", BOOT_MOUNT);
	return g_bootdir;
#else
	snprintf(err, errn, "no boot partition here");
	return NULL;
#endif
}

void up_boot_close(void)
{
	sync();
#ifdef __linux__
	if (g_mounted) umount(BOOT_MOUNT);
#endif
	g_mounted = false;
}

bool up_bootstate_read(const char *dir, up_bootstate *b)
{
	char p[512], text[1024], *line, *next;

	memset(b, 0, sizeof *b);
	strcpy(b->slot, "a");
	strcpy(b->trial, "0");
	strcpy(b->try_, "0");
	strcpy(b->fell, "-");
	strcpy(b->a, "-");
	strcpy(b->b, "-");
	snprintf(p, sizeof p, "%s/bootstate.txt", dir);
	if (!read_line_file(p, text, sizeof text)) return false;
	for (line = text; line && *line; line = next) {
		char *v;
		next = strchr(line, '\n');
		if (next) *next++ = 0;
		v = strchr(line, '=');
		if (!v) continue;
		*v++ = 0;
		v[strcspn(v, "\r\t ")] = 0;
		if (!strcmp(line, "slot")) snprintf(b->slot, sizeof b->slot, "%s", v);
		else if (!strcmp(line, "trial")) snprintf(b->trial, sizeof b->trial, "%s", v);
		else if (!strcmp(line, "try")) snprintf(b->try_, sizeof b->try_, "%s", v);
		else if (!strcmp(line, "fell")) snprintf(b->fell, sizeof b->fell, "%s", v);
		else if (!strcmp(line, "a")) snprintf(b->a, sizeof b->a, "%s", v);
		else if (!strcmp(line, "b")) snprintf(b->b, sizeof b->b, "%s", v);
	}
	return true;
}

/* Written whole under another name, synced, and renamed over: a cut write
 * leaves the old file, which U-Boot reads as it was */
bool up_bootstate_write(const char *dir, const up_bootstate *b)
{
	char p[512], tmp[520], text[256];
	int fd, n;
	bool ok;

	n = snprintf(text, sizeof text, "slot=%s\ntrial=%s\ntry=%s\nfell=%s\na=%s\nb=%s\n",
	             b->slot, b->trial, b->try_, b->fell, b->a, b->b);
	snprintf(p, sizeof p, "%s/bootstate.txt", dir);
	snprintf(tmp, sizeof tmp, "%s/bootstate.new", dir);
	fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) return false;
	ok = write(fd, text, (size_t)n) == n && fsync(fd) == 0;
	ok = close(fd) == 0 && ok;
	ok = ok && rename(tmp, p) == 0;
	if (!ok) unlink(tmp);
	sync();
	return ok;
}

const char *up_bootstate_version(const up_bootstate *b, char slot)
{
	return slot == 'b' ? b->b : b->a;
}

void up_bootstate_set_version(up_bootstate *b, char slot, const char *v)
{
	snprintf(slot == 'b' ? b->b : b->a, sizeof b->a, "%s", v);
}

/* How many bytes the partition holds: a block device's size, or for a test's
 * file standing in for one, its own size */
static bool part_size(int fd, uint64_t *out)
{
	struct stat sb;
	if (fstat(fd, &sb)) return false;
#ifdef __linux__
	if (S_ISBLK(sb.st_mode)) return ioctl(fd, BLKGETSIZE64, out) == 0;
#endif
	*out = (uint64_t)sb.st_size;
	return true;
}

bool up_slot_size(char slot, uint64_t *out)
{
	int fd = open(up_slot_part(slot), O_RDONLY);
	bool ok;
	if (fd < 0) return false;
	ok = part_size(fd, out);
	close(fd);
	return ok;
}

/* `src` written over the start of partition `dev`, synced, and read back
 * off the card (the kernel's cache of the device dropped first) against
 * `sha`. The rest of the partition is left as it was: squashfs reads only
 * as far as its own size. */
bool up_slot_write(const char *src, const char *dev, uint64_t size, const char *sha,
                   void (*tick)(uint64_t done, uint64_t total), char *err, size_t errn)
{
	unsigned char buf[1 << 20], d[32];
	unsigned int dn;
	char hex[UP_SHA_HEX + 1];
	int in = open(src, O_RDONLY), out = -1;
	uint64_t cap = 0, done = 0;
	ssize_t r = 0;
	EVP_MD_CTX *ctx = NULL;
	bool ok = false;

	if (in < 0) { snprintf(err, errn, "cannot read %s", src); return false; }
	out = open(dev, O_RDWR);
	if (out < 0) { snprintf(err, errn, "cannot open %s: %s", dev, strerror(errno)); goto done; }
	if (!part_size(out, &cap) || (cap && size > cap)) {
		snprintf(err, errn, "%s holds %llu bytes, the system is %llu", dev,
		         (unsigned long long)cap, (unsigned long long)size);
		goto done;
	}
	while ((r = read(in, buf, sizeof buf)) > 0) {
		if (write(out, buf, (size_t)r) != r) { snprintf(err, errn, "writing %s: %s", dev, strerror(errno)); goto done; }
		done += (uint64_t)r;
		if (tick) tick(done, size * 2);
	}
	if (r < 0 || done != size) { snprintf(err, errn, "reading %s", src); goto done; }
	if (fsync(out)) { snprintf(err, errn, "syncing %s: %s", dev, strerror(errno)); goto done; }
#ifdef __linux__
	{
		struct stat sb;
		if (!fstat(out, &sb) && S_ISBLK(sb.st_mode)) ioctl(out, BLKFLSBUF, 0);
	}
#endif
#ifdef POSIX_FADV_DONTNEED
	posix_fadvise(out, 0, 0, POSIX_FADV_DONTNEED);
#endif
	if (lseek(out, 0, SEEK_SET) != 0) { snprintf(err, errn, "seeking %s", dev); goto done; }
	ctx = EVP_MD_CTX_new();
	if (!ctx || EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) != 1) { snprintf(err, errn, "no SHA-256"); goto done; }
	for (done = 0; done < size; ) {
		size_t want = size - done < sizeof buf ? (size_t)(size - done) : sizeof buf;
		r = read(out, buf, want);
		if (r <= 0) { snprintf(err, errn, "reading %s back", dev); goto done; }
		EVP_DigestUpdate(ctx, buf, (size_t)r);
		done += (uint64_t)r;
		if (tick) tick(size + done, size * 2);
	}
	EVP_DigestFinal_ex(ctx, d, &dn);
	up_sha256_hex(d, hex);
	ok = !strcmp(hex, sha);
	if (!ok) snprintf(err, errn, "%s reads back wrong", dev);
done:
	EVP_MD_CTX_free(ctx);
	if (out >= 0) close(out);
	close(in);
	return ok;
}
