CHECKS += check-dslang
#| the Nintendo DS's language: each language MENU > Language has maps, in Auto, to the DS's own or
#| to English (Português (Brasil) and 简体中文 have none), the Game Language row's eight choices round
#| trip through the database and go round both ways, the core's option and its values are the
#| patch's, and the launcher sends it first with every DS game and with no other; the row is on the
#| DS shelf's menu in every language with nothing cut, and Left, Right and A go round it, saved
#| (builds the host launcher for the second half; skips without SDL2)
