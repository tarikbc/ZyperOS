CHECKS += check-wifi-watch
CHECKS_LINUX_SKIP += check-wifi-watch
#| the Pixel 2's Wi-Fi watcher (plastron's `/usr/lib/zyperos/wifi-watch`) against a stand-in
#| wpa_cli and log: a signal line soon after a join, at once on a jump of 8 dB and every minute,
#| none while not joined or for the drivers' -9999; a line with the last look when the link drops;
#| after a drop that cut a handshake on a network that joined with its password, one retry
#| (disable_network then enable_network) per join, never for a network that never joined or another
#| reason; no network name in its lines; it ends with the supplicant
