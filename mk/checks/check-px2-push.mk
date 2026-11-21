CHECKS += check-px2-push
#| plastron's `make push` (scripts/px2-slot.sh): it writes the system slot the Pixel 2 is not
#| running and never the running one, a card with one system or a command line that does not add up
#| gets nothing, bootstate.txt edited as System Update does, and the device's script parses and
#| writes no partition by number
