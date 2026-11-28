CHECKS += check-dl-empty
#| MENU > Downloads on a card with no games, one game and PICO-8 carts alone (src/dl_screen.c,
#| src/dlcount.c), the real launcher headless (`ZYPEROS_DOWNLOADS_TEST`'s `counted` and `hub` steps):
#| the page has its count within 10 s of the launch, an empty card's hub says No games yet with
#| nothing to press but START and B (its shots in both themes in build-native/dl-empty-check/), one
#| game is counted with its cover missing, PICO-8 carts are their own covers (needs SDL2)
