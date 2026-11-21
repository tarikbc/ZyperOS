CHECKS += check-spawn
CHECKS_LINUX_SKIP += check-spawn
#| `spawn_detached` (`src/spawn.h`), the start of a program that outlives the launcher (MP4, a
#| resident Diatom started again), with real processes: its own pid back (alive, in a session of
#| its own, not this process's child), a program that cannot be exec'd refused with the exec's
#| errno rather than a pid that names nothing, and the environment, folder and descriptors it gets
