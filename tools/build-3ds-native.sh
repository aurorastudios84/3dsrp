#!/usr/bin/env bash
# Build both handheld clients using an installed devkitPro SDK.
set -euo pipefail
cd "$(dirname "$0")/.."
: "${DEVKITPRO:?Set DEVKITPRO to your devkitPro installation}"
export DEVKITARM="$DEVKITPRO/devkitARM" CTRULIB="$DEVKITPRO/libctru" PORTLIBS="$DEVKITPRO/portlibs/3ds"
export PATH="$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH"
cmake -S source/chiaki-ng -B source/chiaki-ng/build-3ds -G Ninja \
 -DCMAKE_TOOLCHAIN_FILE=cmake/3ds.cmake \
 -DCHIAKI_ENABLE_CLI=OFF -DCHIAKI_ENABLE_GUI=OFF -DCHIAKI_ENABLE_TESTS=OFF \
 -DCHIAKI_LIB_ENABLE_MBEDTLS=ON -DCHIAKI_LIB_ENABLE_OPUS=ON \
 -DCHIAKI_USE_SYSTEM_NANOPB=OFF -DCHIAKI_USE_SYSTEM_JERASURE=OFF \
 -DCHIAKI_LIB_ENABLE_FFMPEG_DECODER=OFF -DCHIAKI_ENABLE_PI_DECODER=OFF
cmake --build source/chiaki-ng/build-3ds --target chiaki-lib -j4
cmake -S source/vendor/usage-mbedtls -B source/3ds/usage-tls/build -G Ninja \
 -DCMAKE_SYSTEM_NAME=Generic -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
 -DCMAKE_C_COMPILER="$DEVKITARM/bin/arm-none-eabi-gcc" \
 -DCMAKE_AR="$DEVKITARM/bin/arm-none-eabi-ar" -DCMAKE_RANLIB="$DEVKITARM/bin/arm-none-eabi-ranlib" \
 -DCMAKE_BUILD_TYPE=Release -DENABLE_PROGRAMS=OFF -DENABLE_TESTING=OFF \
 "-DCMAKE_C_FLAGS=-ffile-prefix-map=$PWD=. -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft -D__3DS__ -ffunction-sections -fdata-sections -DUSAGE_PRIVATE_TLS_BUILD" \
 -DCMAKE_C_STANDARD_LIBRARIES=
cmake --build source/3ds/usage-tls/build -j4
python3 source/3ds/usage-tls/namespace.py source/3ds/usage-tls/build source/3ds/usage-tls/lib source/3ds/app/include/usage_mbedtls_symbols.h arm-none-eabi-
printf '#define RP_BUILD_ID "%s"\n' "$(python3 source/tools/g3/source_id.py)" > source/3ds/app/include/build_id.h
make -C source/3ds/app -j4
bash relay/3ds/build.sh
