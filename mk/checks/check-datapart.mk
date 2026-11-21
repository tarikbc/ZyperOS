CHECKS += check-datapart
#| where the Brick's first start puts the games partition (its `datapart`), over a made-up `/sys`:
#| right after root B on a 4 MiB boundary as partition 5, nothing on a card without root B, and a
#| games partition already there found by its name
