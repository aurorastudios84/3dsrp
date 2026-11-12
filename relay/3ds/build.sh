#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
: "${DEVKITPRO:?Set DEVKITPRO to your devkitPro installation}"
cmake -S ../vendor/libjpeg-turbo-3.1.3 -B build/jpeg \
 -DCMAKE_SYSTEM_NAME=Generic -DCMAKE_SYSTEM_PROCESSOR=armv6k \
 -DCMAKE_C_COMPILER="$DEVKITPRO/devkitARM/bin/arm-none-eabi-gcc" \
 -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
 '-DCMAKE_C_FLAGS=-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft -O3 -D__3DS__ -ffunction-sections -fdata-sections' \
 -DENABLE_SHARED=OFF -DENABLE_STATIC=ON -DWITH_TURBOJPEG=OFF -DWITH_TOOLS=OFF -DWITH_TESTS=OFF -DWITH_SIMD=OFF
cmake --build build/jpeg --target jpeg-static -j4
make -j4
