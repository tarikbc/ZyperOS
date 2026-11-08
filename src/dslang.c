/* SPDX-License-Identifier: GPL-3.0-only */
/* The DS's language for ZyperOS's. See src/dslang.h. */
#include "dslang.h"

const char *dslang_value(lang l)
{
	switch (l) {
	case LANG_JA: return "japanese";
	case LANG_FR: return "french";
	case LANG_DE: return "german";
	case LANG_IT: return "italian";
	case LANG_ES: return "spanish";
	/* The DS has no Portuguese, and no Chinese a DS game here carries */
	case LANG_PT_BR:
	case LANG_ZH_HANS:
	case LANG_EN:
	default:
		return "english";
	}
}
