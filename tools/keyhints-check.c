/* SPDX-License-Identifier: GPL-3.0-only */
/* Button hints (src/keyhints.h), the one component that says what each
 * button does: rows that keep their order, never split a hint and never run
 * past their room unless one hint alone is wider; a list packed into a
 * panel's row and read back whole in all eight languages, with the words a
 * question's hints carry. The drawing is held by check-i18n-shots (every
 * word fits, in every language) and check-np-footer. */
#include <stdio.h>
#include <string.h>

#include "../src/i18n.h"
#include "../src/keyhints.h"

static int fails;

#define CHECK(cond, ...) do { \
	if (!(cond)) { fails++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
	               fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } \
} while (0)

/* A glyph of 32 and 12 to the word, and 16 a byte of the word */
static int width(const char *key, const char *label)
{
	return 32 + 12 + 16 * (int)strlen(label) + (key && strlen(key) > 1 ? 24 : 0);
}

static void rows(void)
{
	key_hint h[7] = {
		{ "A", "Play" }, { "B", "Back" }, { "X", "Lock" }, { "Y", "Mode" },
		{ "L1/R1", "Track" }, { KEYHINT_LEFT_RIGHT, "Seek" }, { "SELECT", "MP4" },
	};
	int start[KEYHINTS_MAX + 1], n, r, i;

	/* All in one row when they fit */
	n = keyhints_rows(h, 3, 1000, KEYHINTS_GAP, width, start);
	CHECK(n == 1 && start[0] == 0 && start[1] == 3, "three short hints: %d rows", n);
	/* A narrow room: more rows, in order, none wider than the room */
	n = keyhints_rows(h, 7, 300, KEYHINTS_GAP, width, start);
	CHECK(n > 1 && start[n] == 7, "seven hints in 300: %d rows, ending %d", n, start[n]);
	for (r = 0; r < n; r++) {
		int w = 0;

		CHECK(start[r] < start[r + 1], "row %d empty", r);
		for (i = start[r]; i < start[r + 1]; i++) w += width(h[i].key, h[i].label) + (i > start[r] ? KEYHINTS_GAP : 0);
		CHECK(w <= 300 || start[r + 1] - start[r] == 1, "row %d is %d wide in 300", r, w);
	}
	/* One hint wider than the room stands alone, never dropped */
	{
		key_hint wide[2] = { { "A", "A very long word for a narrow room" }, { "B", "Back" } };

		n = keyhints_rows(wide, 2, 120, KEYHINTS_GAP, width, start);
		CHECK(n == 2 && start[1] == 1 && start[2] == 2, "a wide hint: %d rows", n);
	}
	CHECK(keyhints_rows(h, 0, 300, KEYHINTS_GAP, width, start) == 0, "no hints, a row");
}

/* The three questions' hints in each language, packed and read back */
static void packed(void)
{
	static const str_id B[] = { S_KEYHINT_CHARGE_ONLY, S_XFER_TILE_COMPUTER, S_KEYHINT_KEEP_GOING,
	                            S_KEYHINT_NOT_NOW };
	char out[256], buf[256];
	key_hint back[KEYHINTS_MAX];
	int l, b, n;

	for (l = 0; l < LANG_COUNT; l++) {
		i18n_set((lang)l);
		for (b = 0; b < (int)(sizeof B / sizeof B[0]); b++) {
			key_hint h[3] = {
				{ KEYHINT_LEFT_RIGHT, T(S_KEYHINT_MOVE) }, { "A", T(S_KEYHINT_CHOOSE) }, { "B", T(B[b]) },
			};

			CHECK(keyhints_pack(h, 3, out, sizeof out) > 0, "%s: no room to pack", lang_code((lang)l));
			n = keyhints_unpack(out, buf, sizeof buf, back, KEYHINTS_MAX);
			CHECK(n == 3, "%s: %d hints back", lang_code((lang)l), n);
			if (n != 3) continue;
			for (int i = 0; i < 3; i++)
				CHECK(!strcmp(back[i].key, h[i].key) && !strcmp(back[i].label, h[i].label),
				      "%s: hint %d came back as %s / %s", lang_code((lang)l), i, back[i].key, back[i].label);
			CHECK(strchr(T(B[b]), '\x1f') == NULL && strchr(T(B[b]), '\x1e') == NULL,
			      "%s: a word holds the packing's marks", lang_code((lang)l));
		}
	}
	i18n_set(LANG_EN);
	/* A legend written as text is no packed list */
	CHECK(!keyhints_unpack("B: close", buf, sizeof buf, back, KEYHINTS_MAX), "text read as hints");
	/* No room: nothing, never half a list */
	{
		key_hint h[2] = { { "A", "Choose" }, { "B", "Back" } };

		CHECK(keyhints_pack(h, 2, out, 12) == 0 && out[0] == '\0', "a list packed into too little");
	}
}

int main(void)
{
	rows();
	packed();
	if (fails) {
		fprintf(stderr, "keyhints-check: %d failed\n", fails);
		return 1;
	}
	printf("keyhints-check: rows in order and in their room, lists packed and read back in %d languages\n",
	       LANG_COUNT);
	return 0;
}
