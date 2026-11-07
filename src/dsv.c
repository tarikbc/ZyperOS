/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* See dsv.h. */
#include "dsv.h"

#include "atomic.h"
#include "fileio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define FOOTER 122
/* The largest DS save chip is 8 MB (FLASH in a few late games); a .dsv past
 * that and its footer is not one. */
#define CHIP_MAX (8 * 1024 * 1024)

bool dsv_import(const char *save_dir, const char *romfile)
{
	static const char cookie[] = "|-DESMUME SAVE-|";
	char srm[1024], dsv[1024];
	const char *dot = strrchr(romfile, '.');
	int stem = dot ? (int)(dot - romfile) : (int)strlen(romfile);
	unsigned char *buf;
	size_t n;
	bool ok = false;

	snprintf(srm, sizeof srm, "%s/%.*s.srm", save_dir, stem, romfile);
	snprintf(dsv, sizeof dsv, "%s/%.*s.dsv", save_dir, stem, romfile);
	if (access(srm, F_OK) == 0 ||
	    !(buf = (unsigned char *)file_read(dsv, CHIP_MAX + FOOTER + 1L, &n)))
		return false;

	if (n > FOOTER && !memcmp(buf + n - 16, cookie, 16)) {
		/* Written whole, flushed to the card and renamed (src/atomic.h): a
		 * power cut mid-write leaves the .dsv and no .srm, and the next run
		 * tries again. The flush is what makes that true. Renamed before
		 * its bytes reached the card, the .srm can come back from a cut
		 * empty, and an .srm that is there is never written again: the
		 * game would start with no save and the .dsv never be read. */
		FILE *o = atomic_open(srm, 0644);

		if (o) {
			if (fwrite(buf, 1, n - FOOTER, o) == n - FOOTER)
				ok = atomic_commit(o, srm);
			else
				atomic_abort(o, srm);
		}
		fprintf(stderr, "dsv: %s -> %s, %ld bytes%s\n", dsv, srm, (long)(n - FOOTER),
		        ok ? "" : " FAILED");
	}
	free(buf);
	return ok;
}
