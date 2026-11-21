CHECKS += check-raoffline
CHECKS_LINUX_SKIP += check-raoffline
#| achievements earned offline (src/raqueue.h) through the store, the queue's worker and the
#| request, with a stand-in for curl: nothing sent with no network and nothing lost; a network back
#| on the shelf sends what waited, dated (`o`, at most 14 days) and with its ROM's hash; the notice
#| counts only what waited; RetroAchievements down keeps it all, tried again after 5 minutes. No
#| request is made (needs SDL2; skips without it)
