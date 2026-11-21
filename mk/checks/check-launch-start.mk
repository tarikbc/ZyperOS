CHECKS += check-launch-start
CHECKS_LINUX_SKIP += check-launch-start
#| launch.sh's start on a card a power cut broke, on each device (`sd/zyperos/launch.sh` with its
#| `sd/DEVICE/device.sh` beside it) with the card's paths moved to a scratch folder and the
#| device's commands stood in for: a junk boot.env is read as off and the script goes on to the
#| launcher (sourced, a syntax error in it ended launch.sh before ZyperOS); a launcher that stops
#| at once five times sets launch.sh aside, so the next start copies ZyperOS again, says so and
#| powers off; a second time it is left as it is; a power-off asked for takes the marker away; and
#| launch.sh with no device.sh beside it is set aside the same way, the launcher never started, and
#| the handheld powers off; and on the Pixel 2 the kernel's line about the battery at the start is
#| copied into the start's log once
