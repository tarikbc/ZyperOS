# The image mk/build-openbor.sh builds OpenBOR in: flycast2021's
# (mk/flycast2021.Dockerfile), the same digest and snapshot, so Debian
# bullseye's gcc 10 on arm64 and a program that asks the device for no newer
# glibc than the cores do. Plus the development files of what OpenBOR links:
# SDL2, libpng, zlib, libogg and libvorbis, which plastron has and loads at
# run time (the same sonames), and libvpx, which it does not and which is
# therefore linked in whole (libvpx-dev's static library).
#
#   docker build --platform linux/arm64 -f mk/openbor.Dockerfile -t zyperos-openbor mk
FROM debian:bullseye-slim@sha256:f313b4bd62667092a59b3a664d7d3ab8b5e65f41675f48e81455a15dc5abe792

ARG DEBIAN_SNAPSHOT=20260901T000000Z
RUN printf '%s\n' \
        "deb http://snapshot.debian.org/archive/debian/${DEBIAN_SNAPSHOT} bullseye main" \
        "deb http://snapshot.debian.org/archive/debian-security/${DEBIAN_SNAPSHOT} bullseye-security main" \
        "deb http://snapshot.debian.org/archive/debian/${DEBIAN_SNAPSHOT} bullseye-updates main" \
        > /etc/apt/sources.list

# Retries: snapshot.debian.org answers 503 and 504 under load (seen
# 2026-11-05), and one package refused fails apt's whole install. Retried in
# the same step, apt keeps what it already fetched in its cache and asks
# again only for what failed.
RUN apt-get -o Acquire::Check-Valid-Until=false -o Acquire::Retries=10 update \
    && for try in 1 2 3 4 5 6 7 8; do \
           apt-get -o Acquire::Retries=10 install -y --no-install-recommends \
               gcc libc6-dev make binutils pkg-config libsdl2-dev libpng-dev zlib1g-dev libogg-dev libvorbis-dev libvpx-dev && break; \
           [ $try = 8 ] && exit 1; sleep 10; \
       done \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work
