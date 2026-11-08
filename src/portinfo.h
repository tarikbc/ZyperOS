/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_PORTINFO_H
#define ZYPEROS_PORTINFO_H

/* Which files on the Ports shelf are a port's own (Delete Game, DESIGN.md
 * 11.31; Storage, 11.32).
 *
 * A port is a script at the top of Roms/Ports and the folders it runs from.
 * PortMaster writes what is whose into each port's folder, port.json: its
 * "items" are the script and the folders ("stardewvalley/",
 * "StardewValley.sh"), and attr.title its name. A port without one names its
 * folder in its script, GAMEDIR="/$directory/ports/stardewvalley". PortMaster
 * itself - PortMaster.sh and its folder, the runtimes and libraries every
 * port runs on - is no port's, and is never one's to delete.
 *
 * No SDL: tools/gamedel-check.c and tools/storage-check.c link it. */

#include <stdbool.h>
#include <stddef.h>

#define PI_NAME  256
#define PI_ITEMS 8
#define PI_MAX   128

typedef struct {
	char folder[PI_NAME];               /* the folder its port.json is in */
	char title[PI_NAME];                /* attr.title, "" for none */
	char scripts[PI_ITEMS][PI_NAME];    /* its items that are files */
	int  nscripts;
	char folders[PI_ITEMS][PI_NAME];    /* its items that are folders, no slash */
	int  nfolders;
} pi_port;

/* Every port.json in a folder of `dir` (Roms/Ports): how many, at most `max` */
int pi_scan(const char *dir, pi_port *out, int max);

/* The folders `script` runs from: the items of the port.json that names it,
 * else the folder its GAMEDIR names; only folders at the top of `dir` that
 * are there, never PortMaster's. How many, at most `max`. */
int pi_folders(const char *dir, const pi_port *all, int n, const char *script,
               char out[][PI_NAME], int max);

/* The folder at the top of `dir` that `name` is, by the listing itself, its
 * real name into `out`: never a path handed to the filesystem, which on the
 * card's exFAT can drop trailing dots or spaces and land on another folder
 * ("PortMaster." on PortMaster). False for none, and for a name that ends
 * in a dot or a space. */
bool pi_real_folder(const char *dir, const char *name, char *out, size_t n);

/* PortMaster's own: its script or its folder, by name */
bool pi_runtime(const char *name);

/* The port.json of `folder`, or NULL */
const pi_port *pi_of_folder(const pi_port *all, int n, const char *folder);

#endif
