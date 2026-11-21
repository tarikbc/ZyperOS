CHECKS += check-strings
#| every string in `res/strings/` in all five languages with English's printf conversions, every ID
#| used and defined, every character in the font that draws it, `src/i18n_ids.h` and
#| `res/web/i18n/` what `tools/i18n.py` writes, and `src/i18n_tables.c` not committed (the build
#| writes it from `res/strings`, so no two pull requests conflict over its offsets)
