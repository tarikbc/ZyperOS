CHECKS += check-crashlog
#| the Pixel 2's crash report (plastron's `/usr/sbin/crashlog`) against a stand-in pstore folder:
#| nothing kept says nothing; a panic's dump becomes one crash-DATE.txt with the dump and the end
#| of the console, plain line ends, and the dump is let go, so the next start adds nothing; a
#| console that ends in the kernel's own restart or power-off says nothing, one that ends otherwise
#| is a report; two reports in one second keep a file each; only the newest CRASH_KEEP stay; the
#| RCU stall panic is set at every start; the launcher's own crash (`--launcher RECORD`) is one
#| report with its signal and the record, the record let go, said once, and nothing of the
#| kernel's touched; and the summary the launcher says (`src/lastboot.h`): the
#| reason (a panic, hung_task's, rootwait's, a watchdog's: the launcher's sysrq restart or a reset
#| before the home; a reset after it with nothing to say), the stage (the kernel before the card, after it, the last `zyboot: stage=`, after the home)
#| and the kernel's time at its last line; the report's name with it; U-Boot's `zyreset=chip` a W; and
#| the start's marker on the card (made at every start, C1, a line for each step): one left by a start
#| whose RAM was lost is a report with its last step and no time, one the home removed or a console
#| that ends in a restart or power-off says nothing
