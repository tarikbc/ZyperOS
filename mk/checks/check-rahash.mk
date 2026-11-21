CHECKS += check-rahash
#| the C and Python ROM hashers agree, over the whole library; PlayStation and PSP discs in `.chd`,
#| `.cso` and `.pbp`, and Dreamcast discs in `.gdi`, `.cue`, `.chd` and `.cdi`, give upstream
#| rcheevos's hashes (`tools/fixtures/rahash`, made by `tools/rahash-fixtures.py`; a `.cdi` through
#| Flycast's own reader), and broken ones are refused
