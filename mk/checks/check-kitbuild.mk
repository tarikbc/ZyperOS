CHECKS += check-kitbuild
#| a test kit's code reaches a kit alone (`src/kit.h`): `mk/cross.mk` adds `ZYPEROS_KIT_BUILD` and
#| the device's kit file only for a VERSION with `-kit`, and nothing else; the files with a kit's
#| hooks have none of it compiled without the define and all of it with it; the host launcher,
#| never a kit, has none; no start script seeds a kit's files
