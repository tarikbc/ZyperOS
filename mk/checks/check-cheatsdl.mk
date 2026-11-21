CHECKS += check-cheatsdl
#| The cheats download (src/cheatsdl.h) on a zip the check makes: its end and index are read, only
#| the card's collections kept, a name with a folder in it, a hidden one and one bigger than 4 MB
#| refused; a ROM matches its own name, its name with a code type, then its title and region in
#| libretro's loose rule (`(U)` is `(USA)`), never another game that starts the same; a deflated
#| and a stored entry are inflated, checked against their CRC-32 and written byte for byte where
#| the in-game list reads them, a damaged one is refused; `.none`, files and no folder are told
#| apart; a system is counted from the card; deleting the downloaded ones keeps your own file. No
#| network
