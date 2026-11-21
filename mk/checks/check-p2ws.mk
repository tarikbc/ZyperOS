CHECKS += check-p2ws
#| p2d's WebSocket with no socket (`src/p2d/ws.c`, `sha1.c`): SHA-1's vectors, RFC 6455's own
#| handshake key, a server frame's head at 125, 126, 65535 and 65536 bytes, a masked client frame
#| unmasked and one cut short waited for, and an unmasked frame, one over 125 bytes, one without
#| FIN, with RSV1 or of an unknown opcode refused; a close and a ping
