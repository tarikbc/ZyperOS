CHECKS += check-crashrec
#| the launcher's own crash (src/crashrec.h), in children that crash on purpose (a bad pointer on
#| the main thread and on a named thread, abort(), a stack that ran out): each still dies of its
#| own signal, and first says the signal, its address, the thread, the version and a backtrace of
#| three frames or more, the same lines on stderr and in the record, which then waits for the next
#| start; a child with no handler says nothing
