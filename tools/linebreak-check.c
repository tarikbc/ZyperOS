/* A name broken into lines, without a font or a window.
 *
 *     make check-linebreak
 *
 * The port loading screen sets the game's name in a column 456 units wide,
 * in up to three lines (src/main.c, port_loading_screen), and every rule of
 * where those lines break fails where nobody is looking for it:
 *
 *   a name that fits, broken anyway, is two short lines where one would do;
 *   a greedy break leaves one short word alone under full lines, which reads
 *   as a mistake: "The Legend of Zelda: Ocarina of" over "Time";
 *   more lines than fewer, because three are narrower, is a block taller than
 *   it needs to be, and the column is centered on its height;
 *   a fourth line runs into Zyper's frame under the column, and a line wider
 *   than the column runs off the screen;
 *   a cut through a UTF-8 character draws a box where a letter was.
 *
 * So: one line when it fits; else the fewest lines that fit, up to the cap,
 * and of those the break whose widest line is narrowest, held to a search of
 * every break; never more than the cap and never a line over the box; a word
 * wider than the box cut with an ellipsis as ui_fit_text cuts it (lb_fit,
 * which ui_fit_text now is), never through a character.
 *
 * Widths come from a table of advances, so the rule is held apart from any
 * one font: narrow and wide letters, capitals, a space, punctuation and the
 * two bytes of an accented letter.
 *
 * Links src/linebreak.c, and NOT SDL.
 */
#include "../src/linebreak.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;

static void ck(int cond, const char *what)
{
	if (!cond) { printf("  FAIL %s\n", what); fails++; }
}

/* ---- widths --------------------------------------------------------------- */

static int adv(unsigned char c)
{
	if (c >= 0x80) return c >= 0xC0 ? 12 : 0;  /* an accented letter: 12 */
	if (c == ' ') return 5;
	if (strchr("iljtf", c)) return 5;
	if (strchr("mw", c)) return 15;
	if (c >= 'a' && c <= 'z') return 10;
	if (c >= 'A' && c <= 'Z') return 14;
	if (c >= '0' && c <= '9') return 11;
	if (strchr(".,:;", c)) return 4;
	return 8;
}

static int calls;

static int width(void *ctx, const char *s)
{
	int w = 0;

	(void)ctx;
	calls++;
	for (; *s; s++) w += adv((unsigned char)*s);
	return w;
}

static int widest(char lines[][LB_LINE], int n)
{
	int i, m = 0;

	for (i = 0; i < n; i++)
		if (width(NULL, lines[i]) > m) m = width(NULL, lines[i]);
	return m;
}

static int valid_utf8(const char *s)
{
	const unsigned char *p = (const unsigned char *)s;

	while (*p) {
		int more = *p < 0x80 ? 0 : (*p & 0xE0) == 0xC0 ? 1 : (*p & 0xF0) == 0xE0 ? 2
		         : (*p & 0xF8) == 0xF0 ? 3 : -1;

		if (more < 0) return 0;
		for (p++; more > 0; more--, p++)
			if ((*p & 0xC0) != 0x80) return 0;
	}
	return 1;
}

static int ends_with(const char *s, const char *tail)
{
	size_t n = strlen(s), t = strlen(tail);

	return n >= t && !strcmp(s + n - t, tail);
}

/* ---- the oracle: every break of a text's words ------------------------------ */

typedef struct {
	char w[24][64];
	int n;
} words;

static void split(const char *text, words *ws)
{
	char buf[1024], *t, *save = NULL;

	ws->n = 0;
	snprintf(buf, sizeof buf, "%s", text);
	for (t = strtok_r(buf, " ", &save); t && ws->n < 24; t = strtok_r(NULL, " ", &save))
		snprintf(ws->w[ws->n++], sizeof ws->w[0], "%s", t);
}

static int span_w(const words *ws, int i, int j)
{
	char line[1024] = "";
	int k;

	for (k = i; k < j; k++) {
		if (k > i) strcat(line, " ");
		strcat(line, ws->w[k]);
	}
	return width(NULL, line);
}

/* The narrowest widest line of any break of words [i, n) into exactly k
 * lines, each no wider than maxw, by trying every one; INT_MAX for none. */
static int every_break(const words *ws, int i, int k, int maxw)
{
	int best = INT_MAX, j;

	if (k == 1) {
		int w = span_w(ws, i, ws->n);
		return i < ws->n && w <= maxw ? w : INT_MAX;
	}
	for (j = i + 1; j <= ws->n - (k - 1); j++) {
		int w = span_w(ws, i, j), r;

		if (w > maxw) continue;
		r = every_break(ws, j, k - 1, maxw);
		if (r == INT_MAX) continue;
		if ((w > r ? w : r) < best) best = w > r ? w : r;
	}
	return best;
}

/* What a greedy break gives: each line as far as it goes. */
static int greedy(const words *ws, int maxw, char lines[][LB_LINE])
{
	int n = 0, k;

	for (k = 0; k < ws->n; k++) {
		char tryl[LB_LINE];

		if (n > 0) {
			snprintf(tryl, sizeof tryl, "%s %s", lines[n - 1], ws->w[k]);
			if (width(NULL, tryl) <= maxw) {
				snprintf(lines[n - 1], LB_LINE, "%s", tryl);
				continue;
			}
		}
		snprintf(lines[n++], LB_LINE, "%s", ws->w[k]);
	}
	return n;
}

static void joined(char lines[][LB_LINE], int n, char *out, size_t on)
{
	int i;

	out[0] = '\0';
	for (i = 0; i < n; i++) {
		if (i) strncat(out, " ", on - strlen(out) - 1);
		strncat(out, lines[i], on - strlen(out) - 1);
	}
}

/* ---- the rules --------------------------------------------------------------- */

static void one_line(void)
{
	char lines[LB_MAX_LINES][LB_LINE];
	int n, w = width(NULL, "Cave Story");

	printf("a name that fits is one line\n");
	n = lb_balance("Cave Story", w, 3, width, NULL, lines);
	ck(n == 1 && !strcmp(lines[0], "Cave Story"), "\"Cave Story\" at its own width is one line");
	n = lb_balance("Cave Story", w + 200, 3, width, NULL, lines);
	ck(n == 1 && !strcmp(lines[0], "Cave Story"), "\"Cave Story\" in a wide box is one line");
	n = lb_balance("  Cave   Story  ", w, 3, width, NULL, lines);
	ck(n == 1 && !strcmp(lines[0], "Cave Story"),
	   "spaces at the ends are dropped and a run of them is one");
	n = lb_balance("Cave Story", w - 1, 3, width, NULL, lines);
	ck(n == 2, "\"Cave Story\" a unit narrower than itself is two lines");
}

static void balanced(void)
{
	const char *name = "The Legend of Zelda: Ocarina of Time";
	/* Boxes where the greedy break leaves "Time" alone (275) and "of Time"
	 * under a full second line (160), in this check's widths. */
	static const int box[] = { 275, 160 };
	char lines[LB_MAX_LINES][LB_LINE], g[LB_MAX_LINES][LB_LINE], msg[256];
	words ws;
	int n, gn, b;

	printf("the widest line is the narrowest of every break\n");
	split(name, &ws);
	for (b = 0; b < 2; b++) {
		int maxw = box[b];

		n = lb_balance(name, maxw, 3, width, NULL, lines);
		gn = greedy(&ws, maxw, g);
		snprintf(msg, sizeof msg, "\"%s\" in %d: %d lines, as the greedy break's %d",
		         name, maxw, n, gn);
		ck(n == gn, msg);
		snprintf(msg, sizeof msg, "\"%s\" in %d: widest %d, the narrowest any break has %d",
		         name, maxw, widest(lines, n), every_break(&ws, 0, n, maxw));
		ck(n >= 1 && widest(lines, n) == every_break(&ws, 0, n, maxw), msg);
		snprintf(msg, sizeof msg, "\"%s\" in %d: narrower than the greedy break (%d against %d)",
		         name, maxw, widest(lines, n), widest(g, gn));
		ck(widest(lines, n) < widest(g, gn), msg);
	}

	/* Two lines fit, and three would be narrower: two. */
	n = lb_balance("aaaa bbbb cccc", width(NULL, "aaaa bbbb"), 3, width, NULL, lines);
	ck(n == 2, "the fewest lines that fit, not the narrowest: \"aaaa bbbb cccc\" is two");
}

static void capped(void)
{
	char lines[LB_MAX_LINES][LB_LINE], msg[256];
	const char *text = "Wwwwwwww Mmmmmmm Wwwwwwww Mmmmmmm Wwwwwwww Mmmmmmm";
	int n, maxw = width(NULL, "Wwwwwwww"), i;

	printf("never more lines than the cap, never a line over the box\n");
	n = lb_balance(text, maxw, 3, width, NULL, lines);
	snprintf(msg, sizeof msg, "six words a line each, capped at three: %d lines", n);
	ck(n == 3, msg);
	for (i = 0; i < n; i++) {
		snprintf(msg, sizeof msg, "line %d, \"%s\", %d wide in %d", i, lines[i],
		         width(NULL, lines[i]), maxw);
		ck(width(NULL, lines[i]) <= maxw, msg);
	}
	ck(n == 3 && ends_with(lines[2], "..."), "the last of them is cut with an ellipsis");
	ck(n == 3 && !strcmp(lines[0], "Wwwwwwww") && !strcmp(lines[1], "Mmmmmmm"),
	   "the lines before it are filled in order");

	n = lb_balance("Stardew Valley", width(NULL, "Stardew"), 1, width, NULL, lines);
	ck(n == 1 && ends_with(lines[0], "...") &&
	   width(NULL, lines[0]) <= width(NULL, "Stardew"),
	   "a cap of one cuts a name that does not fit with an ellipsis");
	ck(lb_balance("Cave Story", 500, 0, width, NULL, lines) == 0, "a cap of 0 is no lines");
	ck(lb_balance("", 500, 3, width, NULL, lines) == 0, "an empty name is no lines");
	ck(lb_balance("    ", 500, 3, width, NULL, lines) == 0, "a name of spaces is no lines");
	ck(lb_balance(NULL, 500, 3, width, NULL, lines) == 0, "no name is no lines");
}

static void cut_word(void)
{
	const char *word = "Supercalifragilisticexpialidocious";
	char lines[LB_MAX_LINES][LB_LINE], fit[LB_LINE], longer[LB_LINE], msg[256];
	size_t k;
	int n, maxw = 200;

	printf("a word wider than the box is cut as ui_fit_text cuts it\n");
	n = lb_balance(word, maxw, 3, width, NULL, lines);
	lb_fit(word, fit, sizeof fit, maxw, width, NULL);
	snprintf(msg, sizeof msg, "\"%s\" in %d: one line, \"%s\"", word, maxw, n ? lines[0] : "");
	ck(n == 1 && ends_with(lines[0], "...") && width(NULL, lines[0]) <= maxw, msg);
	ck(n == 1 && !strcmp(lines[0], fit), "the same cut as lb_fit, which ui_fit_text is");
	/* The longest that fits: one more letter and it would not. */
	k = strlen(fit) - 3;
	ck(!strncmp(fit, word, k), "the cut keeps the start of the word");
	snprintf(longer, sizeof longer, "%.*s...", (int)k + 1, word);
	ck(width(NULL, longer) > maxw, "and keeps as much of it as fits");

	n = lb_balance("The Supercalifragilisticexpialidocious Show", maxw, 3, width, NULL, lines);
	ck(n == 3 && !strcmp(lines[0], "The") && !strcmp(lines[1], fit) && !strcmp(lines[2], "Show"),
	   "a long word among others is cut where it stands");

	lb_fit("Pok\xc3\xa9mon \xc3\x89meraude", fit, sizeof fit, width(NULL, "Pok\xc3\xa9mon") + 1,
	       width, NULL);
	ck(valid_utf8(fit) && ends_with(fit, "..."), "a cut never splits an accented letter");
	lb_fit("Cave Story", fit, sizeof fit, 5, width, NULL);
	ck(!strcmp(fit, ""), "nothing at all when not even the ellipsis fits");
	lb_fit("Cave Story", fit, sizeof fit, width(NULL, "Cave Story"), width, NULL);
	ck(!strcmp(fit, "Cave Story"), "a name that fits is not cut");
}

/* A text of random words, some of them with an accented letter. */
static unsigned seed = 12345;
static unsigned rnd(unsigned n)
{
	seed = seed * 1103515245u + 12345u;
	return (seed >> 16) % n;
}

static void random_names(void)
{
	static const char *letters[] = { "a", "e", "i", "l", "m", "w", "S", "T", "o", "r",
	                                 "\xc3\xa9", ":", "1", "-" };
	char text[512], lines[LB_MAX_LINES][LB_LINE], msg[512], all[1024];
	int t, bad = 0;

	printf("2000 random names, in random boxes and caps\n");
	for (t = 0; t < 2000 && bad < 8; t++) {
		words ws;
		int nw = 1 + (int)rnd(10), cap = 1 + (int)rnd(4), maxw = 30 + (int)rnd(270);
		int i, n, kmin = 0, any_long = 0, w;

		text[0] = '\0';
		for (i = 0; i < nw; i++) {
			int len = 1 + (int)rnd(12), c;

			if (i) strcat(text, rnd(5) ? " " : "  ");
			for (c = 0; c < len; c++) strcat(text, letters[rnd(sizeof letters / sizeof *letters)]);
		}
		split(text, &ws);
		for (i = 0; i < ws.n; i++)
			if (width(NULL, ws.w[i]) > maxw) any_long = 1;
		n = lb_balance(text, maxw, cap, width, NULL, lines);

		if (n < 1 || n > cap) {
			snprintf(msg, sizeof msg, "\"%s\" in %d, cap %d: %d lines", text, maxw, cap, n);
			ck(0, msg);
			bad++;
			continue;
		}
		for (i = 0; i < n; i++)
			if (width(NULL, lines[i]) > maxw || !valid_utf8(lines[i])) {
				snprintf(msg, sizeof msg, "\"%s\" in %d, cap %d: line \"%s\" is %d wide%s",
				         text, maxw, cap, lines[i], width(NULL, lines[i]),
				         valid_utf8(lines[i]) ? "" : ", and cut through a letter");
				ck(0, msg);
				bad++;
			}
		if (any_long) continue;
		for (i = 1; i <= cap && !kmin; i++)
			if (every_break(&ws, 0, i, maxw) != INT_MAX) kmin = i;
		if (!kmin) {
			if (n != cap || !ends_with(lines[n - 1], "...")) {
				snprintf(msg, sizeof msg, "\"%s\" in %d, cap %d fits in no %d lines: %d lines, "
				         "the last \"%s\"", text, maxw, cap, cap, n, lines[n - 1]);
				ck(0, msg);
				bad++;
			}
			continue;
		}
		w = every_break(&ws, 0, kmin, maxw);
		joined(lines, n, all, sizeof all);
		{
			char norm[1024];
			int k;

			norm[0] = '\0';
			for (k = 0; k < ws.n; k++) {
				if (k) strcat(norm, " ");
				strcat(norm, ws.w[k]);
			}
			if (n != kmin || widest(lines, n) != w || strcmp(all, norm)) {
				snprintf(msg, sizeof msg, "\"%s\" in %d, cap %d: %d lines, widest %d; the "
				         "fewest that fit are %d, widest %d at best", text, maxw, cap, n,
				         widest(lines, n), kmin, w);
				ck(0, msg);
				bad++;
			}
		}
	}
}

/* ---- a window's edge between letters ----------------------------------------
 *
 * The marquee (src/ui.c, ui_text_marquee) rests a name too long for its row
 * in a window, and the window's end cut through a letter and left a sliver
 * of it there ("Potatoes," and the first stroke of the W in Cheevos). It now
 * stops at lb_edges' answer: the end of the last whole letter, or of the
 * first one past the edge - never inside a letter, and never inside the
 * bytes of one. */

static int cut_seen;

/* width(), and whether any string measured ended inside a character */
static int width_whole(void *ctx, const char *s)
{
	if (!valid_utf8(s)) cut_seen = 1;
	return width(ctx, s);
}

static void edges(void)
{
	/* "Hold On": H 14, o 24, l 29, d 39, space 44, O 58, n 68 */
	static const char *hold = "Hold On";
	/* P 14, o 24, k 34, the two bytes of e-acute 46, m 61, o 71, n 81 */
	static const char *poke = "Pok\xc3\xa9mon";
	int in, out, t, bad = 0;

	printf("a window's edge falls between letters\n");
	lb_edges(hold, 30, width_whole, NULL, &in, &out);
	ck(in == 29 && out == 39, "an edge in the d: the l's end before it, the d's after it");
	lb_edges(hold, 29, width_whole, NULL, &in, &out);
	ck(in == 29 && out == 29, "an edge between two letters is where it is");
	lb_edges(hold, 41, width_whole, NULL, &in, &out);
	ck(in == 39 && out == 44, "a space is a letter like the rest");
	lb_edges(hold, 1, width_whole, NULL, &in, &out);
	ck(in == 0 && out == 14, "an edge in the first letter: nothing before it, its end after");
	lb_edges(hold, 100, width_whole, NULL, &in, &out);
	ck(in == 68 && out == 68, "an edge past the end is the end, both ways");
	lb_edges("", 10, width_whole, NULL, &in, &out);
	ck(in == 0 && out == 0, "no text has no letters to stop at");
	lb_edges(NULL, 10, width_whole, NULL, &in, &out);
	ck(in == 0 && out == 0, "nor has none at all");

	{
		/* Longer than a line, so copied to the heap and not the stack */
		char longer[LB_LINE + 64];

		memset(longer, 'a', sizeof longer - 1);
		longer[sizeof longer - 1] = '\0';
		lb_edges(longer, 3005, width_whole, NULL, &in, &out);
		ck(in == 3000 && out == 3010, "a text longer than a line is measured the same way");
	}

	cut_seen = 0;
	lb_edges(poke, 40, width_whole, NULL, &in, &out);
	ck(in == 34 && out == 46, "an edge in an accented letter: before it and after it, whole");
	ck(!cut_seen, "and it was never measured cut through its bytes");

	/* Random names and edges against the plain rule: `in` and `out` are
	 * the ends of letters, in <= at <= out, and no letter ends between them */
	for (t = 0; t < 2000 && bad < 8; t++) {
		static const char *letters[] = { "a", "i", "m", "W", " ", ",", "\xc3\xa9", "1" };
		char text[128], pre[128];
		int n = 1 + (int)rnd(20), k, at, total, ok_in = 0, ok_out = 0, between = 0;
		size_t i;

		text[0] = '\0';
		for (k = 0; k < n; k++) strcat(text, letters[rnd(8)]);
		total = width(NULL, text);
		at = (int)rnd((unsigned)total + 1);
		cut_seen = 0;
		lb_edges(text, at, width_whole, NULL, &in, &out);
		/* Every letter's end, by walking the characters */
		for (i = 0; text[i]; ) {
			int e;

			i++;
			while (text[i] && ((unsigned char)text[i] & 0xC0) == 0x80) i++;
			memcpy(pre, text, i);
			pre[i] = '\0';
			e = width(NULL, pre);
			if (e == in) ok_in = 1;
			if (e == out) ok_out = 1;
			if (e > in && e < out) between = 1;
		}
		if (in == 0) ok_in = 1;
		if (!(ok_in && ok_out && in <= at && at <= out && !between && !cut_seen)) {
			char msg[256];

			snprintf(msg, sizeof msg, "\"%s\" at %d: in %d, out %d", text, at, in, out);
			ck(0, msg);
			bad++;
		}
	}
}

/* ---- Chinese ------------------------------------------------------------- */

/* Whether line `s` starts with a mark that closes, or ends with one that opens */
static int bad_edge(const char *s)
{
	static const char *const closes[] = { "，", "。", "、", "！", "？", "：", "；", "）", "」", "…" };
	static const char *const opens[] = { "（", "「", "《", "“" };
	size_t i;

	for (i = 0; i < sizeof closes / sizeof closes[0]; i++)
		if (!strncmp(s, closes[i], strlen(closes[i]))) return 1;
	for (i = 0; i < sizeof opens / sizeof opens[0]; i++)
		if (ends_with(s, opens[i])) return 1;
	return 0;
}

/* Chinese has no spaces: it breaks between its characters, its lines join
 * with nothing between them, and no line starts with a closing mark or ends
 * with an opening one. A character is 12 wide in this table. */
static void chinese(void)
{
	static const char *const texts[] = {
		"连接充电器后，按住 POWER 开机。",
		"正在为此存储卡设置 ZyperOS，请勿取出存储卡。",
		"请先连接 Wi-Fi。然后在手机上打开（下面的）地址。",
		"按上下查看，按 A 确定。",
	};
	char lines[LB_MAX_LINES][LB_LINE], msg[512];
	size_t t;

	printf("Chinese: between its characters, never at a mark's wrong side\n");
	for (t = 0; t < sizeof texts / sizeof texts[0]; t++) {
		for (int maxw = 60; maxw <= 300; maxw += 24) {
			char joined[1024] = "", plain[1024] = "";
			int n = lb_balance(texts[t], maxw, LB_MAX_LINES, width, NULL, lines), i;
			const char *q;
			char *o;

			for (i = 0; i < n; i++) {
				snprintf(msg, sizeof msg, "\"%s\" in %d: line %d \"%s\" is %d wide",
				         texts[t], maxw, i, lines[i], width(NULL, lines[i]));
				ck(width(NULL, lines[i]) <= maxw, msg);
				ck(valid_utf8(lines[i]), msg);
				snprintf(msg, sizeof msg, "\"%s\" in %d: line %d \"%s\" starts or ends at a mark",
				         texts[t], maxw, i, lines[i]);
				ck(!bad_edge(lines[i]), msg);
				strcat(joined, lines[i]);
			}
			if (strstr(joined, "...")) continue;      /* a word cut to fit: not the whole */
			/* Joined without spaces, it is the text without the spaces a
			 * break took at a Latin word's edge */
			for (q = texts[t], o = plain; *q; q++) if (*q != ' ') *o++ = *q;
			*o = '\0';
			for (q = joined, o = joined; *q; q++) if (*q != ' ') *o++ = *q;
			*o = '\0';
			snprintf(msg, sizeof msg, "\"%s\" in %d lines of %d: joined \"%s\"", texts[t], n, maxw, joined);
			ck(!strcmp(joined, plain), msg);
		}
	}
	/* A Latin word inside keeps its spaces, and a line holds it whole */
	{
		int n = lb_balance("按 A 确定", 200, 2, width, NULL, lines);

		ck(n == 1 && !strcmp(lines[0], "按 A 确定"), "\"按 A 确定\" stays one line, spaces kept");
	}
}

int main(void)
{
	chinese();
	one_line();
	balanced();
	capped();
	cut_word();
	random_names();
	edges();
	if (fails) {
		printf("\n%d FAILED\n", fails);
		return 1;
	}
	printf("\nok: one line when it fits, else the fewest lines and the narrowest widest; "
	       "never over the cap or the box; cuts as ui_fit_text; a window's edge "
	       "between letters\n");
	return 0;
}
