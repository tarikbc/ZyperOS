CHECKS += check-usbpower
#| What did you plug in?, the launcher's half of usbrole (src/usbpower.c): usbrole's state lines,
#| the request and the setting written whole and never through a link, asked once per plug after 2
#| s (never on Automatic unless a computer paused it), the replug message for a plug a computer
#| showed up on or came back to while paused, Phone's messages kept while the port powers the
#| phone, and Automatic offered once, at the second USB Accessory answer, on Ask, never again once
#| answered
