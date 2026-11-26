CHECKS += check-dscache
#| the zipped Nintendo DS games DSperate unpacks into `.dsperate`: where a zip's copy goes, the cap
#| after a DS game (2 GB a folder, the least recently launched first, the game just played kept),
#| Delete Game taking a game's copy with it and a neighbour's staying, Rename moving the copy and
#| its tag and the tag naming the new zip (a copy already under the new name never refuses it),
#| and MENU > Storage counting a copy as its game's and following it, and Unpacked DS Games
#| measuring every `.dsperate` and deleting the copies and nothing else, saves and states kept
#| (src/dscache.h); the core's own cap is `mk/dsperate-zipcache-check.sh`, run by its build
