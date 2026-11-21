CHECKS += check-play
CHECKS_LINUX_SKIP += check-play
#| NES player 2's play page (`res/web/play`) in headless Chrome over the host's p2d and a feed of
#| the codec check's fixture frames, in French: the canvas is the frame it says it drew, past the
#| fixture going round and a periodic KEY; every key and touch reaches the share's pad (X and Right
#| 0x0180, A and Left 0x0140, a thumb rolled from A to B, the d-pad's diagonal), held past p2d's
#| second of silence and 0 let go; the pad drawn from the game's buttons (the NES's 0x01FD until
#| the status names others) and no bit sent outside them; the pad on the screen and clear of the
#| picture, and the picture whole device pixels on common phones on their side (667x375 at 2x,
#| 731x411 at 2.625x, 844x390 at 2x and 3x) and on a laptop (1280x800 at 1x and 2x, 768x720);
#| Picture off holds the canvas still while the acks go on; a second tab is told the game is full
#| and keeps trying, and is player 2 once the first closes; a browser without `DecompressionStream`
#| says it is too old and opens no socket; and the share removed, the page says the game ended and
#| stays so, and a tab told the game is full says it is connecting once p2d has gone (needs Chrome)
