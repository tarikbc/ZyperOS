CHECKS += check-dlq
#| The downloads' queue (src/dlq.h) and net.c's slots, the check as curl: a slow request on one
#| slot and a quick one on another each end with their own answer, and dropping one leaves the
#| other; two lanes of three slow requests finish in the time of one; while a game runs they wait
#| and go on after it; Stop is its lane's done, once, its request dropped, and POWER stops every
#| lane; a lane below the card's floor stops as full; the input wait is cut only while a lane runs;
#| the corner shows a lane whose page is closed; the step hook runs after every step of each lane,
#| on its slot, the last before its done; a lane started with none running is a new run. No network
