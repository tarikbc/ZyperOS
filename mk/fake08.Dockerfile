# The image mk/build-fake08.sh builds the PICO-8 core in. Debian's own g++ on
# arm64, run as a linux/arm64 container, so the core is built natively for the
# devices' architecture on any host Docker can run arm64 on (an Apple Silicon
# Mac natively, an x86 host through binfmt/QEMU, slower).
#
# Bullseye for its glibc and libstdc++, the floor of what the core asks of the
# device: GLIBC_2.17 to 2.29 and GLIBCXX up to 3.4.28, below plastron's
# (Buildroot 2026.02, GCC 15). The same digest and snapshot as
# mgba-bridge.Dockerfile, for the reasons given there; this one is arm64-only
# by its --platform, which is what makes the build byte-for-byte repeatable.
#
#   docker build --platform linux/arm64 -f mk/fake08.Dockerfile -t zyperos-fake08 mk
FROM debian:bullseye-slim@sha256:f313b4bd62667092a59b3a664d7d3ab8b5e65f41675f48e81455a15dc5abe792

ARG DEBIAN_SNAPSHOT=20260901T000000Z
RUN printf '%s\n' \
        "deb http://snapshot.debian.org/archive/debian/${DEBIAN_SNAPSHOT} bullseye main" \
        "deb http://snapshot.debian.org/archive/debian-security/${DEBIAN_SNAPSHOT} bullseye-security main" \
        "deb http://snapshot.debian.org/archive/debian/${DEBIAN_SNAPSHOT} bullseye-updates main" \
        > /etc/apt/sources.list

RUN apt-get -o Acquire::Check-Valid-Until=false update && apt-get install -y --no-install-recommends \
        g++ \
        make \
        binutils \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work
