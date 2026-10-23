#!/usr/bin/env bash
# Cross-build chiaki's core (libchiaki.a) for the New 3DS.
#
# Needs docker (sudo if the socket is not in the caller's group) and the build
# image from 3ds/Dockerfile:
#     docker build -t n3ds-chiaki-build:latest 3ds/
#
#   ./3ds/build.sh                 # configure + build libchiaki.a
#   ./3ds/build.sh clean           # wipe the build dir first
#   ./3ds/build.sh chiaki-lib      # build one ninja target
#
# Everything happens inside the container; chiaki-ng/build-3ds/ is written by
# root, so remove it with sudo if you need to.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$HERE")"
IMAGE="${CHIAKI_3DS_IMAGE:-n3ds-chiaki-build:latest}"
# Set DOCKER="sudo docker" if your Docker setup requires sudo.
docker_() { ${DOCKER:-docker} "$@"; }

if [ "${1:-}" = "clean" ]; then
	shift
	docker_ run --rm -v "$ROOT/chiaki-ng:/work" -w /work "$IMAGE" rm -rf /work/build-3ds
fi

TARGET="${1:-chiaki-lib}"

docker_ run --rm -v "$ROOT/chiaki-ng:/work" -w /work "$IMAGE" bash -c "
set -e
cmake -B build-3ds -G Ninja \
	-DCMAKE_TOOLCHAIN_FILE=cmake/3ds.cmake \
	-DCHIAKI_ENABLE_CLI=OFF -DCHIAKI_ENABLE_GUI=OFF -DCHIAKI_ENABLE_TESTS=OFF \
	-DCHIAKI_LIB_ENABLE_MBEDTLS=ON -DCHIAKI_LIB_ENABLE_OPUS=ON \
	-DCHIAKI_USE_SYSTEM_NANOPB=OFF -DCHIAKI_USE_SYSTEM_JERASURE=OFF \
	-DCHIAKI_LIB_ENABLE_FFMPEG_DECODER=OFF -DCHIAKI_ENABLE_PI_DECODER=OFF
ninja -C build-3ds $TARGET
"
