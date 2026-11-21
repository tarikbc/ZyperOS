CHECKS += check-start-soc
#| the GKD Pixel 2's battery level at a start (plastron's `9-px2-rk817-start-soc.patch`), as
#| plastron's `make start-soc-check` holds it: `rk817_rest_uv`, `rk817_start_from` and the
#| charger's start limit taken out of the patches, the OCV table, the 111 mOhm internal resistance
#| and the 300 mA charge end out of the device tree; the sample has its current's drop taken out
#| (none without a resistance, 5 A through 1 Ohm without overflow); under 30 min off the counter is
#| kept, from 30 min the voltage, a charger's start (over 300 mA in, the device tree's
#| charge-term-current; `9-px2-rk817-wear3-charger-start.patch`) keeps the level kept before the
#| power-off (`rk817_start_level`), not the counter that fell while off, and a start from POWER
#| under any current out takes the voltage; a player's ten 1.0.6 starts as the kernel logged them,
#| whose starts from POWER read +108 to +114 mA with no cable in, each from the voltage after 30
#| min off (93% where the counter said 64, 38 and 30%, and 79% where it said 0%) or from the
#| counter under 30; the 1.0.5 case (a cable's start at 1.5 A, the path twice the device tree's
#| resistance) still never from its sample; a battery at rest at 49% sampled at 250 mA out reads
#| 42% alone and 49% corrected; the full charge a start measures (`9-px2-rk817-wear.patch`'s
#| `rk817_learn_fcc`); and the empty end (`9-px2-rk817-wear2-empty.patch`'s `rk817_empty_due`, the
#| patches checked to sort in the order they need): under 3.55 V at rest, which the table reads as
#| 2% or less (ZyperOS's `BATT_EMPTY_PCT`, read from src/status.h), with the counter 10 points or
#| more over, on the third 8 s update in a row the counter is set to the voltage's level; spikes of
#| one or two updates, a sag under 1 to 1.5 A at 20 to 50% through twice the device tree's
#| resistance, a counter that is right, the cable and the charge limit set nothing; a battery that
#| holds 1200 mAh measures 1465 and then 1333 at its empty ends
