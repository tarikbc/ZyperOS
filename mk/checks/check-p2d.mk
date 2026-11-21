CHECKS += check-p2d
#| p2d, player 2's server, on the host against a feed of frames over real sockets: the page, `/`
#| and a phone's internet check sent to it, `/i18n.js` in the handheld's language or English;
#| anything but a WebSocket request on `/p2ws` refused, and any Origin but the page's own; frames
#| 30 a second and more while acked, never more than 2 unacked; the first guest told it is player 2
#| with the NES's buttons (509) or the share's (`p2d-feed --buttons`), its pad those buttons only,
#| let go after a second of silence and when it leaves; a second told it is taken while the first
#| plays on, and every newcomer told so even in a burst with no room to be told from; with
#| `--players 3` two browsers as players 2 and 3, each its own pad, bit in `guests` and frames
#| (unpacked to the feed's picture), a third taken, a freed port the next newcomer's; a guest
#| silent 5 s with a frame unacked dropped and one that acks kept; pings answered, a message the
#| page never sends closing it; the share removed, its magic cleared or a SIGTERM, the guest told
#| the game ended and p2d gone within a second, and gone with 1 after 5 s with no share; the DNS
#| answer; `p2d-feed`'s buttons, players and `--rgba` frames
