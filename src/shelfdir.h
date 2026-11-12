/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_SHELFDIR_H
#define ZYPEROS_SHELFDIR_H

/* Folders on a system's shelf (1.0.5). A system's games can be kept in
 * folders of its Roms/<folder>, to any depth: "Homebrew and Aftermarket",
 * "Rom Hacks", "Rom Hacks/Pokemon". The scan reads them all into one list,
 * each game keeping the folder it is in (game_entry.dir), and a shelf shows
 * one folder of it at a time: the folders in it first, A to Z, each with how
 * many games it holds at every depth, then its own games. A on a folder opens
 * it; B goes up one.
 *
 * A game that IS a folder - a disc set, a ScummVM game - stays one game
 * (src/library.c decides which is which), so a folder here only ever holds
 * games.
 *
 * Everything that is about a system's games rather than what its shelf shows
 * - Favorites, search, Random Game, Storage, the scraper, the saved library -
 * goes by the whole list, at every depth.
 *
 * No SDL and no I/O: tools/shelfdir-check.c runs it on made-up lists. */
#include "library.h"

#include <stdbool.h>
#include <stddef.h>

/* Whether any game of `all` is in a folder: the shelf has folders to show */
bool sd_any(const game_list *all);

/* Whether game `g` is in folder `dir` itself ("" is the top), not deeper */
bool sd_in(const game_entry *g, const char *dir);

/* The folder `g` is in, "" for the top */
void sd_dir_of(const game_entry *g, char *out, size_t n);

/* Folder `dir`'s own folder: "Rom Hacks" of "Rom Hacks/Pokemon", "" of a
 * folder at the top (and of the top) */
void sd_parent(const char *dir, char *out, size_t n);

/* The last part of a folder's path, its own name */
const char *sd_base(const char *dir);

/* What a shelf shows for folder `dir` of `all`, into `out` (lib_free'd
 * first): the folders in it, by name, each an entry whose `file` is its path
 * and `name` its own name, `folder` its count of games; then its games, in
 * `all`'s order. How many of the entries are folders, or -1 with `out` empty
 * when memory ran out. A folder with no game in it at any depth is none: the
 * scan never lists one. */
int sd_build(const game_list *all, const char *dir, game_list *out);

#endif
