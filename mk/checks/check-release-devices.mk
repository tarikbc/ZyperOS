CHECKS += check-release-devices
#| the devices a release carries (`config/devices`, `tools/release-devices.sh`): the release notes'
#| `Tested on <id>: <who, on what>` lines read on sample notes (every device tested passes; a
#| device missing, a misspelled or unlisted id, or a Tested line in another form fails) and sample
#| lists; each listed device with its `src/device/<id>_profile.c` and its
#| `res/update/min-system-<id>`, which `tools/update-pack.py` puts in that device's lists
