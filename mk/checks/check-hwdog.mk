CHECKS += check-hwdog
#| plastron's hwdog against stand-in files: armed at once from init's first line, stopped at a
#| shutdown's TERM (the magic close), nothing with no device, and its two tests (zytest=hang holds
#| init, zytest=stall never pets); Linux only
