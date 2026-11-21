CHECKS += check-phonelink
#| A phone's USB tethering (src/phonelink.c): its network interface told from the gadget's own usb0
#| and from a Wi-Fi adapter's, an iPhone told from a Mac on the bus (as sysfs reads them, newline
#| and all), USB File Transfer opened once a link, and a way out to the internet from
#| /proc/net/route (Wi-Fi, or a default route that is not lo; the phone's, metered; never the cable
#| to a computer)
