#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$HERE")"
${DOCKER:-docker} run --rm --network none -v "$ROOT:/work" -w /work "${CHIAKI_3DS_IMAGE:-n3ds-chiaki-build:latest}" bash -lc '
set -e
export PATH=/opt/devkitpro/devkitARM/bin:/opt/devkitpro/tools/bin:$PATH
cmake -S vendor/usage-mbedtls -B 3ds/usage-tls/build -G Ninja \
 -DCMAKE_SYSTEM_NAME=Generic -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
 -DCMAKE_C_COMPILER=/opt/devkitpro/devkitARM/bin/arm-none-eabi-gcc -DCMAKE_AR=/opt/devkitpro/devkitARM/bin/arm-none-eabi-ar -DCMAKE_RANLIB=/opt/devkitpro/devkitARM/bin/arm-none-eabi-ranlib \
 -DCMAKE_BUILD_TYPE=Release -DENABLE_PROGRAMS=OFF -DENABLE_TESTING=OFF \
 -DCMAKE_C_FLAGS="-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft -D__3DS__ -ffunction-sections -fdata-sections -DUSAGE_PRIVATE_TLS_BUILD" \
 -DCMAKE_C_STANDARD_LIBRARIES=""
cmake --build 3ds/usage-tls/build -j4
python3 3ds/usage-tls/namespace.py 3ds/usage-tls/build 3ds/usage-tls/lib 3ds/app/include/usage_mbedtls_symbols.h arm-none-eabi-
'
