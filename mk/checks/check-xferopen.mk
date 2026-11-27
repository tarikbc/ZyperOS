CHECKS += check-xferopen
#| USB File Transfer opens at once whatever the USB controller does (src/gadget.h): desk mode's
#| stand-in cable with each controller write 3 s slow; a link that is off shows the loading bar at
#| once and B leaves with the bind on its way, a configured link the computer answers on is not
#| bound or toggled, one it does not answer on is made fresh, no child left; XFEROPEN_BEFORE=1
#| runs 1.1.0's waits and fails
