/* SPDX-License-Identifier: GPL-3.0-only */
/* What a port's own files say about it. See src/portdoc.h. */
#include "portdoc.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "rajson.h"

/* The most of any one file that is read: a port.json is a kilobyte or two, a
 * keymap less, a README a few; a file past this is not a port's notes */
#define READ_MAX (64 * 1024)

/* A file's text, NUL terminated, or NULL; *len its length */
static char *slurp(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	char *buf;
	size_t n;

	if (!f) return NULL;
	if (!(buf = malloc(READ_MAX + 1))) {
		fclose(f);
		return NULL;
	}
	n = fread(buf, 1, READ_MAX, f);
	fclose(f);
	buf[n] = '\0';
	if (len) *len = n;
	return buf;
}

static bool is_file(const char *path)
{
	struct stat st;

	return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static bool is_dir(const char *path)
{
	struct stat st;

	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* `s` cut to `cap` bytes at most, never inside a UTF-8 character */
static void utf8_cut(char *s, size_t cap)
{
	size_t n = strlen(s);

	if (n < cap) return;
	n = cap - 1;
	while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) n--;
	s[n] = '\0';
}

static void trim(char *s)
{
	size_t n = strlen(s), i = 0;

	while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
	while (s[i] && isspace((unsigned char)s[i])) i++;
	if (i) memmove(s, s + i, n - i + 1);
}

/* ---- port.json ------------------------------------------------------------- */

bool pd_json(const char *json, size_t len, const char *script_base, port_doc *out)
{
	jsv root = js_root(json, len), items, it = { 0 }, e, attr, v;
	bool named = false;

	if (!js_member(root, "items", &items)) return false;
	while (!named && js_next(items, &it, &e)) {
		char item[256];

		if (js_str(e, item, sizeof item) && !strcasecmp(item, script_base)) named = true;
	}
	if (!named) return false;
	if (js_member(root, "attr", &attr)) {
		char title[sizeof out->title];

		if (js_member(attr, "title", &v) && js_str(v, title, sizeof title)) {
			trim(title);
			if (title[0]) snprintf(out->title, sizeof out->title, "%s", title);
		}
		if (js_member(attr, "desc", &v) && js_str(v, out->desc, sizeof out->desc)) trim(out->desc);
		if (js_member(attr, "inst", &v) && js_str(v, out->inst, sizeof out->inst)) trim(out->inst);
	}
	return true;
}

/* ---- the keymap ------------------------------------------------------------
 *
 * gptokeyb's .gptk: "button = key", a line each, the button by SDL's game
 * controller names (a, b, x, y, l1, r1, l2, r2, start, back, up, down, left,
 * right, and the sticks and their halves, which the Pixel 2 has none of).
 * A second line for the same button is a modifier ("up = repeat") and not
 * a key; \" is no key. gptokeyb2's files are sections, its buttons under
 * [controls] in the same form, with "dpad = arrow_keys" for the four. */

static int button_named(const char *s)
{
	static const struct { const char *name; pd_button b; } NAMES[] = {
		{ "a", PD_A }, { "b", PD_B }, { "x", PD_X }, { "y", PD_Y },
		{ "l1", PD_L1 }, { "r1", PD_R1 }, { "l2", PD_L2 }, { "r2", PD_R2 },
		{ "start", PD_START }, { "back", PD_SELECT }, { "select", PD_SELECT },
		{ "up", PD_UP }, { "down", PD_DOWN }, { "left", PD_LEFT }, { "right", PD_RIGHT },
	};
	size_t i;

	for (i = 0; i < sizeof NAMES / sizeof NAMES[0]; i++)
		if (!strcasecmp(s, NAMES[i].name)) return (int)NAMES[i].b;
	return -1;
}

/* The key a keymap's value names, as a page says it. False for a value that
 * is no key: a modifier of the line before, or nothing at all. */
static bool key_named(const char *v, pd_key *k)
{
	static const struct { const char *in; const char *name; pd_word w; } KEYS[] = {
		{ "space", NULL, PD_SPACE },
		{ "up", NULL, PD_UP_KEY }, { "down", NULL, PD_DOWN_KEY },
		{ "left", NULL, PD_LEFT_KEY }, { "right", NULL, PD_RIGHT_KEY },
		{ "mouse_left", NULL, PD_CLICK_LEFT }, { "mouse_right", NULL, PD_CLICK_RIGHT },
		{ "mouse_middle", NULL, PD_CLICK_MIDDLE },
		{ "enter", "Enter", PD_NAME }, { "return", "Enter", PD_NAME },
		{ "kp_enter", "Enter", PD_NAME },
		{ "esc", "Esc", PD_NAME }, { "escape", "Esc", PD_NAME },
		{ "backspace", "Backspace", PD_NAME }, { "tab", "Tab", PD_NAME },
		{ "shift", "Shift", PD_NAME }, { "leftshift", "Shift", PD_NAME },
		{ "lshift", "Shift", PD_NAME }, { "rightshift", "Shift", PD_NAME },
		{ "rshift", "Shift", PD_NAME },
		{ "ctrl", "Ctrl", PD_NAME }, { "leftctrl", "Ctrl", PD_NAME },
		{ "lctrl", "Ctrl", PD_NAME }, { "rightctrl", "Ctrl", PD_NAME },
		{ "rctrl", "Ctrl", PD_NAME }, { "control", "Ctrl", PD_NAME },
		{ "alt", "Alt", PD_NAME }, { "leftalt", "Alt", PD_NAME }, { "lalt", "Alt", PD_NAME },
		{ "rightalt", "Alt", PD_NAME }, { "ralt", "Alt", PD_NAME },
		{ "pageup", "Page Up", PD_NAME }, { "pagedown", "Page Down", PD_NAME },
		{ "home", "Home", PD_NAME }, { "end", "End", PD_NAME },
		{ "insert", "Insert", PD_NAME }, { "delete", "Delete", PD_NAME },
		{ "del", "Delete", PD_NAME },
	};
	size_t i, n = strlen(v);

	memset(k, 0, sizeof *k);
	if (!n || !strcmp(v, "\\\"") || !strcmp(v, "\"") || !strcasecmp(v, "repeat") ||
	    !strncasecmp(v, "add_", 4) || !strncasecmp(v, "hold_", 5) ||
	    !strncasecmp(v, "mouse_movement", 14) || !strcasecmp(v, "none"))
		return false;
	for (i = 0; i < sizeof KEYS / sizeof KEYS[0]; i++)
		if (!strcasecmp(v, KEYS[i].in)) {
			k->word = KEYS[i].w;
			if (KEYS[i].name) snprintf(k->name, sizeof k->name, "%s", KEYS[i].name);
			return true;
		}
	/* One character, as it is on the key: a letter in capitals */
	if (n == 1) {
		k->name[0] = (char)toupper((unsigned char)v[0]);
		return true;
	}
	/* F1 to F12, and the rest as written, a capital first and spaces for _ */
	snprintf(k->name, sizeof k->name, "%s", v);
	for (i = 0; k->name[i]; i++)
		if (k->name[i] == '_') k->name[i] = ' ';
	k->name[0] = (char)toupper((unsigned char)k->name[0]);
	return true;
}

void pd_gptk(const char *text, port_doc *out)
{
	bool seen[PD_BUTTONS] = { false };
	bool in_controls = true;     /* gptokeyb's has no sections at all */
	const char *p = text;
	int b;

	memset(out->keys, 0, sizeof out->keys);
	out->nkeys = 0;
	while (p && *p) {
		const char *e = strchr(p, '\n');
		size_t n = e ? (size_t)(e - p) : strlen(p);
		char line[160], *eq, *name, *val;
		bool fits = n < sizeof line;

		if (fits) memcpy(line, p, n);
		p = e ? e + 1 : NULL;
		if (!fits) continue;                       /* no keymap line is so long */
		line[n] = '\0';
		trim(line);
		if (!line[0] || line[0] == '#' || line[0] == ';') continue;
		if (line[0] == '[') {
			/* gptokeyb2: its buttons are [controls]'s; [controls:layer] and
			 * the rest are other layers and settings */
			in_controls = !strncasecmp(line, "[controls]", 10);
			continue;
		}
		if (!in_controls || !(eq = strchr(line, '='))) continue;
		*eq = '\0';
		name = line;
		val = eq + 1;
		trim(name);
		trim(val);
		/* gptokeyb2's four at once */
		if (!strcasecmp(name, "dpad")) {
			if (!strcasecmp(val, "arrow_keys")) {
				static const pd_word W[4] = { PD_UP_KEY, PD_DOWN_KEY, PD_LEFT_KEY, PD_RIGHT_KEY };
				int d;

				for (d = 0; d < 4; d++) {
					out->keys[PD_UP + d] = (pd_key){ W[d], "" };
					seen[PD_UP + d] = true;
				}
			}
			continue;
		}
		if ((b = button_named(name)) < 0 || seen[b]) continue;   /* a modifier line */
		seen[b] = true;
		key_named(val, &out->keys[b]);
	}
	for (b = 0; b < PD_BUTTONS; b++)
		if (out->keys[b].word != PD_NAME || out->keys[b].name[0]) out->nkeys++;
}

/* ---- the README's controls --------------------------------------------------
 *
 * Porters write them every way there is: "A = Jump", "A: Jump", "A - Jump",
 * "* A -> Jump", "| A | Jump |", under "CONTROLS" and a row of =, or
 * "## Controls", or "Controls:". What is taken is a line whose left side is
 * nothing but the names of buttons, joined by +, /, &, "and" or "or", under
 * a heading that says Controls; anything else ends nothing, but is not a
 * row. A row for a button this device does not have (a stick alone, L3,
 * Guide) is left out: there is nothing to press for it. */

static bool heading_is_controls(const char *line)
{
	char s[64];
	size_t i = 0, n;

	while (*line == '#' || *line == '*' || *line == '=' || *line == '_' || isspace((unsigned char)*line))
		line++;
	n = strlen(line);
	if (!n || n >= sizeof s) return false;
	memcpy(s, line, n + 1);
	while (n > 0 && (s[n - 1] == ':' || s[n - 1] == '*' || s[n - 1] == '=' || s[n - 1] == '#' ||
	                 isspace((unsigned char)s[n - 1])))
		s[--n] = '\0';
	for (i = 0; s[i]; i++) s[i] = (char)tolower((unsigned char)s[i]);
	if (strchr(s, '=') || strchr(s, '|') || n > 32) return false;
	return !strcmp(s, "controls") || !strcmp(s, "control") || !strcmp(s, "default controls") ||
	       !strcmp(s, "controls (default)") || !strcmp(s, "game controls") ||
	       !strcmp(s, "controls and hotkeys") || !strcmp(s, "controls & hotkeys") ||
	       !strcmp(s, "button mapping") || !strcmp(s, "controller mapping");
}

/* A heading of another section, which ends this one: "## ...", a line in
 * capitals with no = in it, or a line of = or - under a line */
static bool heading_other(const char *line)
{
	size_t i, letters = 0;

	if (line[0] == '#') return true;
	for (i = 0; line[i]; i++) {
		if (islower((unsigned char)line[i])) return false;
		if (isupper((unsigned char)line[i])) letters++;
		if (line[i] == '=' || line[i] == ':' || line[i] == '|' || line[i] == '-') return false;
	}
	return letters >= 4;
}

static bool rule_line(const char *line)
{
	size_t i;

	if (!line[0]) return false;
	for (i = 0; line[i]; i++)
		if (line[i] != '=' && line[i] != '-' && line[i] != '_' && line[i] != ' ') return false;
	return i >= 3;
}

/* The left side's buttons, as a page prints them, into r. False when it is
 * not a side of buttons at all; true with r->key and r->dirs empty when it
 * is, but of nothing this device has. */
static bool buttons_of(const char *side, pd_row *r)
{
	static const char *const PRINTED[PD_BUTTONS] = {
		"A", "B", "X", "Y", "L1", "R1", "L2", "R2", "START", "SELECT"
	};
	char s[96], *w, *save = NULL;
	bool combo = strchr(side, '+') != NULL, any = false, missing = false;
	int order[PD_BUTTONS], nb = 0, i;
	unsigned dirs = 0;
	bool dpad = false, stick_before = false;
	size_t n = strlen(side);

	if (!n || n >= sizeof s) return false;
	for (i = 0; side[i]; i++) {
		char c = (char)tolower((unsigned char)side[i]);

		s[i] = c == '+' || c == '/' || c == '&' || c == ',' || c == '(' || c == ')' ||
		       c == '[' || c == ']' || c == '*' || c == '`' || c == '\t' ? ' ' : c;
	}
	s[i] = '\0';
	for (w = strtok_r(s, " ", &save); w; w = strtok_r(NULL, " ", &save)) {
		int b = -1;

		if (!strcmp(w, "and") || !strcmp(w, "or") || !strcmp(w, "hold") || !strcmp(w, "press") ||
		    !strcmp(w, "button") || !strcmp(w, "buttons") || !strcmp(w, "the") || !strcmp(w, "-"))
			continue;
		if (!strcmp(w, "a")) b = PD_A;
		else if (!strcmp(w, "b")) b = PD_B;
		else if (!strcmp(w, "x")) b = PD_X;
		else if (!strcmp(w, "y")) b = PD_Y;
		else if (!strcmp(w, "l") || !strcmp(w, "l1") || !strcmp(w, "lb")) b = PD_L1;
		else if (!strcmp(w, "r") || !strcmp(w, "r1") || !strcmp(w, "rb")) b = PD_R1;
		else if (!strcmp(w, "l2") || !strcmp(w, "lt") || !strcmp(w, "zl")) b = PD_L2;
		else if (!strcmp(w, "r2") || !strcmp(w, "rt") || !strcmp(w, "zr")) b = PD_R2;
		else if (!strcmp(w, "start")) b = PD_START;
		else if (!strcmp(w, "select") || !strcmp(w, "back") || !strcmp(w, "hotkey")) b = PD_SELECT;
		else if (!strcmp(w, "dpad") || !strcmp(w, "d-pad") || !strcmp(w, "d")) { dpad = true; continue; }
		else if (!strcmp(w, "pad") || !strcmp(w, "directional")) { dpad = true; continue; }
		else if (!strcmp(w, "up")) { dirs |= PD_DIR_UP; continue; }
		else if (!strcmp(w, "down")) { dirs |= PD_DIR_DOWN; continue; }
		else if (!strcmp(w, "left") || !strcmp(w, "right")) {
			/* "left stick" is a stick; "left" alone, or "d-pad left", a way */
			const char *next = save ? save : "";

			while (*next == ' ') next++;
			if (!strncmp(next, "stick", 5) || !strncmp(next, "analog", 6) ||
			    !strncmp(next, "joystick", 8) || !strncmp(next, "thumb", 5)) {
				stick_before = true;
				continue;
			}
			dirs |= w[0] == 'l' ? PD_DIR_LEFT : PD_DIR_RIGHT;
			continue;
		}
		else if (!strcmp(w, "stick") || !strcmp(w, "sticks") || !strcmp(w, "analog") ||
		         !strcmp(w, "analogue") || !strcmp(w, "joystick") || !strcmp(w, "thumbstick") ||
		         !strcmp(w, "ls") || !strcmp(w, "rs") || !strcmp(w, "l-stick") || !strcmp(w, "r-stick")) {
			stick_before = true;
			continue;
		}
		else if (!strcmp(w, "l3") || !strcmp(w, "r3") || !strcmp(w, "guide") || !strcmp(w, "menu") ||
		         !strcmp(w, "home") || !strcmp(w, "mode") || !strcmp(w, "function") || !strcmp(w, "fn")) {
			missing = true;
			continue;
		}
		else return false;            /* a word that is no button: not a row */
		any = true;
		for (i = 0; i < nb && order[i] != b; i++) ;
		if (i == nb && nb < PD_BUTTONS) order[nb++] = b;
	}
	memset(r->key, 0, sizeof r->key);
	r->dirs = 0;
	if (missing) return true;                  /* a button the Pixel 2 does not have */
	if (dpad && !dirs) dirs = PD_DIR_ALL;
	if (!any && !dirs) return stick_before;    /* a stick alone: nothing here */
	if (any && dirs) return true;              /* a button and a way: not said here */
	if (!any) {
		r->dirs = dirs;
		return true;
	}
	for (i = 0; i < nb; i++) {
		size_t at = strlen(r->key);
		const char *name = PRINTED[order[i]];

		if (at + strlen(name) + (i ? 1 : 0) >= sizeof r->key) {
			r->key[0] = '\0';               /* too many to print: not said */
			return true;
		}
		snprintf(r->key + at, sizeof r->key - at, "%s%s", i ? (combo ? "+" : "/") : "", name);
	}
	return true;
}

/* What the port says the buttons do, tidied: no markdown, no closing stop */
static void said_tidy(char *s)
{
	size_t i, o = 0, n;

	for (i = 0; s[i]; i++)
		if (s[i] != '*' && s[i] != '`' && s[i] != '|') s[o++] = s[i];
	s[o] = '\0';
	trim(s);
	n = strlen(s);
	while (n > 0 && (s[n - 1] == '.' || s[n - 1] == ';' || s[n - 1] == ',')) s[--n] = '\0';
	if (s[0]) s[0] = (char)toupper((unsigned char)s[0]);
}

/* One line of the section as a row: true with *r filled, false when it is no
 * row (and *skip when it was one, but of nothing to show) */
static bool row_of(char *line, pd_row *r, bool *skip)
{
	char *left, *right = NULL, *sep;
	size_t i;

	*skip = false;
	/* A table's row: the first cell the buttons, the second what they do */
	if (line[0] == '|') {
		char *cell2, *end;

		left = line + 1;
		if (!(cell2 = strchr(left, '|'))) return false;
		*cell2++ = '\0';
		if ((end = strchr(cell2, '|'))) *end = '\0';
		right = cell2;
	} else {
		/* a bullet first */
		while (*line == '-' || *line == '*' || *line == '+' || isspace((unsigned char)*line)) line++;
		if (!strncmp(line, "\xe2\x80\xa2", 3)) line += 3;       /* • */
		left = line;
		if ((sep = strstr(line, "->")) || (sep = strstr(line, "=>"))) {
			*sep = '\0';
			right = sep + 2;
		} else if ((sep = strchr(line, '='))) {
			*sep = '\0';
			right = sep + 1;
		} else if ((sep = strchr(line, ':'))) {
			*sep = '\0';
			right = sep + 1;
		} else if ((sep = strstr(line, " - ")) || (sep = strstr(line, " \xe2\x80\x93 "))) {
			right = sep + (sep[1] == '-' ? 3 : 5);
			*sep = '\0';
		} else {
			return false;
		}
	}
	trim(left);
	trim(right);
	/* a table's header, or the line under it */
	for (i = 0; left[i] && (left[i] == '-' || left[i] == ':' || left[i] == ' '); i++) ;
	if (!left[i] || !strcasecmp(left, "button") || !strcasecmp(left, "buttons") ||
	    !strcasecmp(left, "control") || !strcasecmp(left, "controls") || !strcasecmp(left, "key") ||
	    !strcasecmp(left, "input")) {
		*skip = true;
		return true;
	}
	if (!buttons_of(left, r)) return false;
	snprintf(r->said, sizeof r->said, "%s", right);
	utf8_cut(r->said, sizeof r->said);
	said_tidy(r->said);
	if ((!r->key[0] && !r->dirs) || !r->said[0]) *skip = true;
	return true;
}

void pd_readme(const char *text, port_doc *out)
{
	const char *p = text;
	bool in = false;
	int misses = 0, lines = 0;
	char prev[160] = "";

	out->nsaid = 0;
	while (p && *p && out->nsaid < PD_ROWS) {
		const char *e = strchr(p, '\n');
		size_t n = e ? (size_t)(e - p) : strlen(p);
		char line[160];
		pd_row r;
		bool skip;

		if (n >= sizeof line) n = sizeof line - 1;
		memcpy(line, p, n);
		line[n] = '\0';
		p = e ? e + 1 : NULL;
		{
			size_t k = strlen(line);

			if (k && line[k - 1] == '\r') line[k - 1] = '\0';
		}
		trim(line);
		if (!in) {
			if (heading_is_controls(line)) in = true;
			/* "Controls" underlined: the heading is the line before */
			else if (rule_line(line) && heading_is_controls(prev)) in = true;
			snprintf(prev, sizeof prev, "%s", line);
			continue;
		}
		if (!line[0] || rule_line(line)) continue;
		if (++lines > 60) break;
		if (out->nsaid > 0 && heading_other(line)) break;
		if (row_of(line, &r, &skip)) {
			misses = 0;
			if (!skip) out->said[out->nsaid++] = r;
		} else if (out->nsaid > 0 && ++misses >= 3) {
			break;                 /* prose again: the section is over */
		}
	}
}

/* ---- the script ------------------------------------------------------------- */

/* strcasestr, which is not in every C library this builds with */
static const char *find_nocase(const char *hay, const char *needle)
{
	size_t n = strlen(needle);

	for (; *hay; hay++)
		if (!strncasecmp(hay, needle, n)) return hay;
	return NULL;
}

void pd_script(const char *text, char *dir, size_t dn, char *gptk, size_t gn, bool *gptokeyb)
{
	const char *p;

	if (dn) dir[0] = '\0';
	if (gn) gptk[0] = '\0';
	*gptokeyb = strstr(text, "$GPTOKEYB") || strstr(text, "${GPTOKEYB");
	/* The folder: what GAMEDIR is set to, its last part, when that is a
	 * name and not a variable ("$PORTDIR/lonekey", "/$directory/ports/x/") */
	for (p = text; dn && (p = strstr(p, "GAMEDIR=")); p += 8) {
		const char *s = p + 8, *e, *seg;
		size_t n;

		if (p > text && (isalnum((unsigned char)p[-1]) || p[-1] == '_')) continue;
		if (*s == '"' || *s == '\'') s++;
		for (e = s; *e && !strchr("\"' \t\r\n;", *e); e++) ;
		while (e > s && e[-1] == '/') e--;
		for (seg = e; seg > s && seg[-1] != '/'; seg--) ;
		n = (size_t)(e - seg);
		if (!n || n >= dn || memchr(seg, '$', n) || memchr(seg, '{', n)) continue;
		memcpy(dir, seg, n);
		dir[n] = '\0';
		break;
	}
	/* Else the first "ports/NAME" that is a name, not a variable and not
	 * PortMaster's own folder ($controlfolder's /roms/ports/PortMaster) */
	for (p = text; dn && !dir[0] && (p = find_nocase(p, "ports/")); p += 6) {
		const char *s = p + 6, *q = s;
		size_t n;

		while (*q && !strchr("/\"' \t\r\n$}{)(;:`", *q)) q++;
		n = (size_t)(q - s);
		if (!n || *q == '$' || n >= dn || (n == 10 && !strncasecmp(s, "PortMaster", 10))) continue;
		memcpy(dir, s, n);
		dir[n] = '\0';
		break;
	}
	/* The keymap: the first ".gptk" named in full, its file name alone */
	for (p = text; gn && (p = strstr(p, ".gptk")); p += 5) {
		const char *s = p, *q = p + 5;
		size_t n;

		if (isalnum((unsigned char)*q) || *q == '_') continue;
		while (s > text && !strchr("/\"' \t\r\n$}{=(", s[-1])) s--;
		if (s > text && (s[-1] == '$' || s[-1] == '{' || s[-1] == '}')) continue;
		n = (size_t)(q - s);
		if (n <= 5 || n >= gn) continue;
		memcpy(gptk, s, n);
		gptk[n] = '\0';
		break;
	}
}

/* ---- all of it ---------------------------------------------------------------- */

/* The port's folder's own file of this kind: the one whose name (cased any
 * way: the card is exFAT) starts with `prefix` and ends with `suffix`, the
 * one named `want` first. Into out; false for none. */
static bool find_in(const char *dir, const char *prefix, const char *suffix, const char *want,
                    char *out, size_t n)
{
	DIR *d = opendir(dir);
	struct dirent *e;
	int found = 0;

	if (!d) return false;
	while ((e = readdir(d))) {
		size_t l = strlen(e->d_name), sl = strlen(suffix), pl = strlen(prefix);
		char path[1024];

		if (e->d_name[0] == '.' || l < sl + pl) continue;
		if (strncasecmp(e->d_name, prefix, pl) || strcasecmp(e->d_name + l - sl, suffix)) continue;
		snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
		if (!is_file(path)) continue;
		if (want && !strcasecmp(e->d_name, want)) {
			snprintf(out, n, "%s", path);
			found = 2;
			break;
		}
		if (!found) {
			snprintf(out, n, "%s", path);
			found = 1;
		}
	}
	closedir(d);
	return found > 0;
}

/* The port's port.json in `dir`, if it names the script */
static bool json_in(const char *dir, const char *base, port_doc *out)
{
	char path[1024];
	size_t len;
	char *t;
	bool ok;

	snprintf(path, sizeof path, "%s/port.json", dir);
	if (!(t = slurp(path, &len))) return false;
	ok = pd_json(t, len, base, out);
	free(t);
	return ok;
}

bool port_doc_read(const char *script, port_doc *out)
{
	const char *slash = strrchr(script, '/');
	const char *base = slash ? slash + 1 : script;
	char ports[1024], dir[1024] = "", seg[256], gptk[256], path[1024];
	char *text;
	size_t bl = strlen(base);
	bool found = false;

	memset(out, 0, sizeof *out);
	snprintf(out->title, sizeof out->title, "%.*s", (int)(bl > 3 && !strcasecmp(base + bl - 3, ".sh")
	                                                       ? bl - 3 : bl), base);
	if (!strcasecmp(base, "PortMaster.sh")) {
		out->portmaster = true;
		snprintf(out->title, sizeof out->title, "PortMaster");
		return true;
	}
	snprintf(ports, sizeof ports, "%.*s", slash ? (int)(slash - script) : 1, slash ? script : ".");

	seg[0] = gptk[0] = '\0';
	if ((text = slurp(script, NULL))) {
		pd_script(text, seg, sizeof seg, gptk, sizeof gptk, &out->gptokeyb);
		free(text);
	}
	/* The folder the script names, when its port.json names the script */
	if (seg[0]) {
		snprintf(path, sizeof path, "%s/%s", ports, seg);
		if (is_dir(path) && json_in(path, base, out)) {
			snprintf(dir, sizeof dir, "%s", path);
			found = true;
		}
	}
	/* Else every folder's, for the one that does */
	if (!found) {
		DIR *d = opendir(ports);
		struct dirent *e;
		int looked = 0;

		while (d && (e = readdir(d)) && looked < 400) {
			if (e->d_name[0] == '.' || !strcasecmp(e->d_name, "PortMaster")) continue;
			snprintf(path, sizeof path, "%s/%s", ports, e->d_name);
			if (!is_dir(path)) continue;
			looked++;
			if (json_in(path, base, out)) {
				snprintf(dir, sizeof dir, "%s", path);
				found = true;
				break;
			}
		}
		if (d) closedir(d);
	}
	/* No port.json: the folder the script works in, as it says */
	if (!found && seg[0]) {
		snprintf(path, sizeof path, "%s/%s", ports, seg);
		if (is_dir(path)) snprintf(dir, sizeof dir, "%s", path);
	}
	if (!dir[0]) return false;

	/* The keymap the script hands gptokeyb, else the folder's own */
	if (find_in(dir, "", ".gptk", gptk[0] ? gptk : NULL, path, sizeof path) &&
	    (text = slurp(path, NULL))) {
		pd_gptk(text, out);
		free(text);
	}
	/* The README's controls: README.md, README.txt, readme.txt, README */
	{
		static const char *const SUFFIX[] = { ".md", ".txt", "" };
		size_t i;

		for (i = 0; i < sizeof SUFFIX / sizeof SUFFIX[0] && !out->nsaid; i++) {
			if (!find_in(dir, "readme", SUFFIX[i], NULL, path, sizeof path)) continue;
			if ((text = slurp(path, NULL))) {
				pd_readme(text, out);
				free(text);
			}
		}
	}
	return true;
}
