CHECKS += check-smartpro-board
#| the TrimUI Smart Pro's plastron board is the Brick's but for its own lines:
#| `smartpro_defconfig` is `brick_defconfig` with its device, name and post-script argument; its
#| `genimage.cfg` the Brick's with `SMARTBOOT` and `smartpro.dtb`; its slot boot the Brick's with
#| `smartpro.dtb` and the Brick's prelude; its boot loader's defconfig has the memory values
#| TrimUI's boot0 carries for it (LPDDR4 at 792 MHz, DCDC3 at 1.1 V); and the start script runs
#| sppad, never brick-pad, on a Smart Pro
