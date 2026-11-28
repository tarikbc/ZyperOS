CHECKS += check-udcpeer
#| A computer from the USB gadget controller's state alone (src/udcpeer.c), the R36S's way to see one:
#| addressed or configured is a computer, an idle bus for 10 s after one (an unplug reads as idle), a reset or power only for 3 s after one
#| (a charger's shorted data lines never), unplugged or unreadable nothing; a log line at each change
