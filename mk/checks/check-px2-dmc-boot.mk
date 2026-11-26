CHECKS += check-px2-dmc-boot
#| the GKD Pixel 2's DDR from U-Boot to init (plastron's `9-px2-px30-dmc-boot-rate.patch`): its DMC
#| driver rebuilt from the kernel patches in Buildroot's order starts on performance when U-Boot's
#| rate is the top step and on userspace otherwise, never simple_ondemand, whose first look moved the
#| DDR from 1056 to 328 MHz 0.46 s into the kernel, where some Pixel 2s crashed on cold starts; both
#| governors built in; the device tree's DMC steps all ones U-Boot's FSP_INIT trains for LPDDR4, its
#| top the 1056 MHz FSP_INIT leaves the DDR at; vcc_ddr with no voltage for Linux to set; init's
#| datapart still on performance. Host Python and patch, no device

# The GKD Pixel 2's DDR from U-Boot to init (plastron/scripts/px2-dmc-boot-check.py):
# the DMC driver as the kernel patches make it, the device tree's DDR steps
# against what U-Boot trains, and the kernel's config. Host Python only.
check-px2-dmc-boot:
	@python3 -I plastron/scripts/px2-dmc-boot-check.py
