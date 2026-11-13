# The image mk/build-ppsspp.sh and mk/build-tic80.sh build their cores in:
# flycast2021's (mk/flycast2021.Dockerfile), the same digest and snapshot, so
# Debian bullseye's g++ 10 on arm64 and cores that ask the device for no newer
# glibc or libstdc++ than the other cores do. Plus CMake, which both projects
# build with (bullseye's 3.18), libgles-dev and libegl-dev, whose headers
# PPSSPP's OpenGL ES build reads (on the device the libraries are plastron's,
# Mesa's libGLESv2, loaded at dlopen), and Python 3, which glslang's CMake
# runs to generate its sources.
#
#   docker build --platform linux/arm64 -f mk/cmake-cores.Dockerfile -t zyperos-cmake-cores mk
FROM debian:bullseye-slim@sha256:f313b4bd62667092a59b3a664d7d3ab8b5e65f41675f48e81455a15dc5abe792

ARG DEBIAN_SNAPSHOT=20260901T000000Z
RUN printf '%s\n' \
        "deb http://snapshot.debian.org/archive/debian/${DEBIAN_SNAPSHOT} bullseye main" \
        "deb http://snapshot.debian.org/archive/debian-security/${DEBIAN_SNAPSHOT} bullseye-security main" \
        "deb http://snapshot.debian.org/archive/debian/${DEBIAN_SNAPSHOT} bullseye-updates main" \
        > /etc/apt/sources.list

# Retries, as mk/scummvm.Dockerfile has them: snapshot.debian.org answers 503
# and 504 under load, and one package refused fails apt's whole install.
RUN apt-get -o Acquire::Check-Valid-Until=false -o Acquire::Retries=10 update \
    && for try in 1 2 3 4 5 6 7 8; do \
           apt-get -o Acquire::Retries=10 install -y --no-install-recommends \
               g++ make cmake binutils pkg-config libgles-dev libegl-dev zlib1g-dev \
               python3 && break; \
           [ $try = 8 ] && exit 1; sleep 10; \
       done \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work
