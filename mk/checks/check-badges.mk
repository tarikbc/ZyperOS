CHECKS += check-badges
CHECKS_LINUX_SKIP += check-badges
#| achievement badges (src/rabadge.h) through the real fetch with a stand-in for curl: kept beside
#| the set; one curl for what is missing and nothing else; starting it and drawing 4,000 icons
#| while it runs never wait for it; a badge that has not come is a plain one of the same 64 pixels,
#| never a hole; a 404 page is never kept as a badge; then the plain badge and Zyper as pixels
#| (src/noticeart.h), in both themes, his shadow on Beard only. No network (needs SDL2 and
#| SDL2_image; skips without them)
