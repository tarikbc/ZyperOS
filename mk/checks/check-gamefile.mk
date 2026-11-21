CHECKS += check-gamefile
#| a game's own file before its core: a 0-byte file of any kind is refused as empty (a 0-byte PSP
#| .iso crashed PPSSPP), a zip, 7z, CHD, CSO, PBP or iso under its kind's least size as incomplete,
#| while a ROM, cue or m3u is judged by being empty alone; what the shelf says, in two languages
