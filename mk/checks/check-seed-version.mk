CHECKS += check-seed-version
#| the boot's ZyperOS version compare (plastron's `zyperos.init`, `version_of` and `newer_seed`, as
#| each board's post-build.sh takes it for `SEED_V`): 1.1.0-rc.N, -kit.N and -test.N are versions
#| below 1.1.0 and in order (test below kit below rc), the image's ZyperOS goes over an older
#| card's and never over a newer one, and what is not a version (none, "", 1..2, x, 1.08.0,
#| 1.2.3.4) is false without stopping the shell
