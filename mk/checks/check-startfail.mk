CHECKS += check-startfail
#| what the shelf says when a core would not load (src/startfail.h): another machine's build, a
#| library it needs, a library too old, a function nothing has, or nothing more known, in each of
#| the eight languages with the core's file name and the name dlerror gave, a name too long for the
#| panel cut with an ellipsis, an error of another code left to the log, and French's no-break
#| space before its colon (no SDL); Diatom's side is `check-corefail` (the ELF header and dlerror's
#| text as glibc, musl and macOS write it) and `check-corefail-e2e` (the real dlopen)
