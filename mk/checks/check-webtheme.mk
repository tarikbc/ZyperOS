CHECKS += check-webtheme
#| the handheld's theme as the play page's `theme.css` (`src/webtheme.c`): with nothing customised
#| the file gives exactly the values of `play.css`'s own token blocks, for the properties
#| `play.css` lets it set and no others; a custom look moves the accent, Square makes `--corner` 0,
#| dark says `color-scheme: dark`, a buffer too small is refused, and the write leaves a whole file
