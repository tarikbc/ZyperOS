CHECKS += check-crashlog
#| the Pixel 2's crash report (plastron's `/usr/sbin/crashlog`) against a stand-in pstore folder:
#| nothing kept says nothing; a panic's dump becomes one crash-DATE.txt with the dump and the end
#| of the console, plain line ends, and the dump is let go, so the next start adds nothing; a
#| console that ends in the kernel's own restart or power-off says nothing, one that ends otherwise
#| is a report; two reports in one second keep a file each; only the newest CRASH_KEEP stay; the
#| RCU stall panic is set at every start; the launcher's own crash (`--launcher RECORD`) is one
#| report with its signal and the record, the record let go, said once, and nothing of the
#| kernel's touched
