CHECKS += check-usbhang
#| the launcher never stops on the USB controller (src/hwput.h): a write that hangs costs 2 s, said
#| before and when late, its child reaped; File Transfer's fresh link only when the link needs it
#| (src/cablefresh.h); the kept log lines in the kernel's log, synced (src/klog.h); the watchdog
#| with a stuck loop (src/wdog.h)
