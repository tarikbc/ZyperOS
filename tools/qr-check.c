/* SPDX-License-Identifier: GPL-3.0-only */
/* make check-qr
 *
 * The hotspot's QR code (src/qr.c). A code that looks like a QR code and does
 * not scan is the one failure nobody standing at the handheld can explain,
 * so every version the encoder can choose is read back by a real decoder:
 * zbarimg (zbar), when the host has it, from a picture drawn the way the
 * panel draws the code. Without zbarimg the shape is still checked - the
 * finders, the timing, the size for the length - and the check says what it
 * could not do. */
#include "../src/qr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;

#define CHECK(cond, what) do { \
	if (!(cond)) { printf("  FAIL: %s (line %d)\n", what, __LINE__); failures++; } \
} while (0)

static unsigned char m[QR_SIZE_MAX * QR_SIZE_MAX];
static int zbar = -1;   /* -1 not asked yet, 0 none, 1 there */
static int read_back;   /* codes zbarimg read correctly */

static int dark(int size, int x, int y) { return m[y * size + x]; }

/* A finder's 7x7 at (x, y): dark ring, light ring, dark 3x3. */
static int finder_at(int size, int x0, int y0)
{
	int x, y;

	for (y = 0; y < 7; y++)
		for (x = 0; x < 7; x++) {
			int d = abs(x - 3) > abs(y - 3) ? abs(x - 3) : abs(y - 3);

			if (dark(size, x0 + x, y0 + y) != (d != 2)) return 0;
		}
	return 1;
}

/* The code as a picture, a quiet zone of four modules round it, `unit`
 * pixels a module before the picture is scaled by `k`: each pixel the share
 * of it that is light, as a linear downscale gives it (4x4 samples). The
 * hotspot screen draws 12 units a module and the Pixel 2 shows the launcher
 * at 0.625, so a module there is 7.5 pixels and every other edge is grey. */
static int decode_at(int size, const char *want, int unit, double k)
{
	char pgm[64], cmd[160], got[512] = "";
	int q = 4, canvas = (size + 2 * q) * unit, side = (int)(canvas * k), x, y, sx, sy;
	FILE *f;
	size_t n;

	if (zbar < 0) zbar = system("command -v zbarimg > /dev/null 2>&1") == 0;
	if (!zbar) return -1;
	snprintf(pgm, sizeof pgm, "/tmp/zyperos-qr-%d.pgm", (int)getpid());
	if (!(f = fopen(pgm, "wb"))) return 0;
	fprintf(f, "P5\n%d %d\n255\n", side, side);
	for (y = 0; y < side; y++)
		for (x = 0; x < side; x++) {
			int light = 0;

			for (sy = 0; sy < 4; sy++)
				for (sx = 0; sx < 4; sx++) {
					int cx = (int)((x + (sx + 0.5) / 4) / k), cy = (int)((y + (sy + 0.5) / 4) / k);
					int mx = cx / unit - q, my = cy / unit - q;

					light += !(mx >= 0 && my >= 0 && mx < size && my < size && dark(size, mx, my));
				}
			fputc(light * 255 / 16, f);
		}
	fclose(f);
	snprintf(cmd, sizeof cmd, "zbarimg --quiet --raw -Sdisable -Sqrcode.enable %s 2>/dev/null", pgm);
	if ((f = popen(cmd, "r"))) {
		n = fread(got, 1, sizeof got - 1, f);
		got[n] = '\0';
		pclose(f);
	}
	unlink(pgm);
	n = strlen(got);
	while (n && (got[n - 1] == '\n' || got[n - 1] == '\r')) got[--n] = '\0';
	return strcmp(got, want) == 0;
}

static int decode(int size, const char *want) { return decode_at(size, want, 4, 1.0); }

static void one(const char *text, int want_size)
{
	int size = qr_encode(text, m), k, ok;
	char what[160];

	snprintf(what, sizeof what, "%zu bytes: size %d, wanted %d", strlen(text), size, want_size);
	CHECK(size == want_size, what);
	if (size != want_size || size == 0) return;
	CHECK(finder_at(size, 0, 0) && finder_at(size, size - 7, 0) && finder_at(size, 0, size - 7),
	      "the three finders");
	for (k = 8, ok = 1; k < size - 8; k++)
		if (dark(size, k, 6) != (k % 2 == 0) || dark(size, 6, k) != (k % 2 == 0)) ok = 0;
	CHECK(ok, "the timing lines");
	CHECK(dark(size, 8, size - 8), "the dark module");
	ok = decode(size, text);
	snprintf(what, sizeof what, "zbarimg read %zu bytes (version %d) back", strlen(text),
	         (size - 17) / 4);
	if (ok >= 0) CHECK(ok == 1, what);
	if (ok == 1) read_back++;
}

int main(void)
{
	char t[256];
	/* The most each version holds at level M, in bytes */
	static const int fits[QR_VERSION_MAX + 1] = { 0, 14, 26, 42, 62, 84, 106, 122, 152, 180, 213 };
	int v;

	puts("qr");
	one("WIFI:T:WPA;S:ZyperOS-7Q2K;P:q7mx-3kpa;;", 29);
	/* As the hotspot screen shows it on the Pixel 2: 12 units, at 0.625 */
	{
		const char *w = "WIFI:T:WPA;S:ZyperOS-7Q2K;P:q7mx-3kpa;;";
		int size = qr_encode(w, m), ok = decode_at(size, w, 12, 0.625);

		if (ok >= 0) CHECK(ok == 1, "zbarimg read the code at the Pixel 2's 7.5 pixels a module");
		if (ok == 1) read_back++;
	}
	one("WIFI:T:WPA;S:Zyper's Pixel 2;P:kite-moss-48;;", 33);
	one("http://10.42.1.1/", 25);
	one("", 21);
	for (v = 1; v <= QR_VERSION_MAX; v++) {
		memset(t, 0, sizeof t);
		/* Every byte value somewhere across the versions, and full */
		for (int i = 0; i < fits[v]; i++) t[i] = (char)(33 + (i * 7 + v) % 94);
		one(t, 17 + 4 * v);
		if (v < QR_VERSION_MAX) {
			t[fits[v]] = 'x';
			one(t, 17 + 4 * (v + 1));
		}
	}
	memset(t, 'z', 214);
	t[214] = '\0';
	CHECK(qr_encode(t, m) == 0, "past version 10: nothing");
	CHECK(qr_encode(NULL, m) == 0, "no text");
	if (zbar == 0) puts("  (no zbarimg here: the codes were not read back)");
	else printf("  zbarimg read %d codes back, every version 1 to %d\n", read_back, QR_VERSION_MAX);
	if (failures) { printf("qr: %d FAILED\n", failures); return 1; }
	puts("qr: ok");
	return 0;
}
