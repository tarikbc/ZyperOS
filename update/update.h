/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* zyperos-update: System Update for the card's ZyperOS folder. See
 * docs/updates.md for the design; this header is the program's own pieces.
 *
 * The program runs beside the launcher, never inside it: a download can take
 * minutes and must go on while the player leaves the screen, and everything
 * that touches the network and the signature lives here, where it can be run
 * and tested on its own (tools/update-check.sh, over SSH on the device).
 *
 * What it trusts: the public keys in the system image (/etc/zyperos/
 * update-keys) and nothing else. The release list (the manifest) is signed
 * with Ed25519; every file it names is checked by SHA-256 against it, after
 * download and again after it is on the card. The installed folder's file
 * index (ZyperOS/.files) and the staging journal only save work; a lie in
 * either is caught by the final check. */
#ifndef ZYPEROS_UPDATE_H
#define ZYPEROS_UPDATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UP_FORMAT        1
#define UP_SHA_HEX       64
#define UP_MAX_MANIFEST  (8u << 20)
#define UP_MAX_FILES     20000
#define UP_MAX_PATH      400

/* A file of the new folder. method 'g' is one gzip member in the pack, 'r'
 * the bytes as they are. size and sha are the file's own, decoded. */
typedef struct {
	char sha[UP_SHA_HEX + 1];
	uint64_t size, offset, stored;
	char method;
	char *path;
} up_file;

typedef struct {
	char kind[16];          /* "folder" (the card's ZyperOS) or "system" */
	char device[32], version[32], min_system[32];
	char pack[160];
	uint64_t pack_size;
	char pack_sha[UP_SHA_HEX + 1];
	up_file *files;
	int nfiles;
	char **dirs;            /* empty folders to make, relative like paths */
	int ndirs;
	uint64_t total;         /* every file's size added up */
	char keyid[17];
	unsigned char sig[64];
	char *text;             /* the whole manifest as read */
	size_t text_len;
	size_t body_len;        /* the signed part: up to and including "end" */
} up_manifest;

/* Parse a manifest. On failure, err says why, in words for the log. */
bool up_manifest_parse(const char *text, size_t len, up_manifest *m, char *err, size_t errn);
/* The signature, against <keys_dir>/<keyid>.pem. */
bool up_manifest_verify(const up_manifest *m, const char *keys_dir, char *err, size_t errn);
void up_manifest_free(up_manifest *m);
/* A relative path the manifest may name: no "..", no absolute path, no
 * backslash or control characters, no empty part. */
bool up_path_ok(const char *p);
/* The reserved top-level names in a ZyperOS folder, never in a manifest:
 * .files, .update, .ready, .trial, .staged. */
bool up_path_reserved(const char *p);
/* 0.10.0 > 0.9.2; 0.2.0-beta.1 < 0.2.0. Returns <0, 0, >0. */
int up_version_cmp(const char *a, const char *b);
/* The key id of a raw Ed25519 public key: the first 8 bytes of its SHA-256,
 * in hex. Used for the manifest's sign line and the key's file name. */
bool up_key_id(const char *pem_path, char out[17]);

/* SHA-256 of a whole file, in hex. With drop, the file's pages are dropped
 * from the cache first, so the bytes come off the card and not from memory. */
bool up_sha256_file(const char *path, char out[UP_SHA_HEX + 1], bool drop);
void up_sha256_hex(const unsigned char d[32], char out[UP_SHA_HEX + 1]);

/* The status file the launcher reads. */
typedef struct {
	const char *path;
	char state[24];
	char installed[32], version[32];
	uint64_t download, done, total;
	uint64_t size;          /* the new folder, every file */
	char kind[16];          /* "system" for a system update, else empty */
	char slot[4], other[32];/* the slot that runs, the other slot's version */
	int files_done, files_total;
	char message[160];
} up_status;
void up_status_write(up_status *s);

/* The two system slots (update/slots.c, docs/updates.md layer 2) */
typedef struct { char slot[4], trial[8], try_[8], fell[4], a[32], b[32]; } up_bootstate;
bool up_slots_init(void);              /* true on a card with slots, booted by them */
char up_slot_running(void);            /* 'a' or 'b'; 0 when not booted by slots */
char up_slot_other(void);
bool up_slot_trial(void);              /* this start is a trial (zyperos.trial=1) */
const char *up_slot_part(char slot);   /* its root partition */
bool up_slot_size(char slot, uint64_t *out);  /* how many bytes it holds */
const char *up_boot_open(char *err, size_t errn);  /* the boot partition, mounted */
void up_boot_close(void);
bool up_bootstate_read(const char *dir, up_bootstate *b);
bool up_bootstate_write(const char *dir, const up_bootstate *b);
const char *up_bootstate_version(const up_bootstate *b, char slot);
void up_bootstate_set_version(up_bootstate *b, char slot, const char *v);
bool up_slot_write(const char *src, const char *dev, uint64_t size, const char *sha,
                   void (*tick)(uint64_t done, uint64_t total), char *err, size_t errn);
#endif
