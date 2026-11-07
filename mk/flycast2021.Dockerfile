# The image mk/build-flycast2021.sh builds the Dreamcast core in. Debian's own
# g++ on arm64, run as a linux/arm64 container, so the core is built natively
# for the devices' architecture (an Apple Silicon Mac natively, an x86 host
# through binfmt/QEMU, slower).
#
# fake08.Dockerfile's base, the same digest and snapshot: bullseye's glibc and
# libstdc++ sit below plastron's (Buildroot 2026.02, GCC 15), and its GCC 10
# is of the core's own year, so the 2021 code builds as it was written. Plus
# libgles-dev, whose libGLESv2.so the core links, and zlib1g-dev; on the
# device both are plastron's libraries (Mesa's libGLESv2, zlib), loaded at
# dlopen.
#
#   docker build --platform linux/arm64 -f mk/flycast2021.Dockerfile -t zyperos-flycast2021 mk
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
        libgles-dev \
        zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work
