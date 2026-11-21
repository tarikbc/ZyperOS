CHECKS += check-corepins
#| `mk/fetch-vendor.sh` fetches the cores from ZyperOS's cores release and never from libretro's
#| buildbot, whose files change under the same name (nine of nineteen pins had stopped matching
#| it); the release's tag is no v<number>, so the installer and System Update never take it for a
#| version; each pin is a sha256, each core pinned once and with its source in
#| THIRD-PARTY-LICENSES.md (offline; no vendor/)
