#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
WIN_ROOT="$PWD"
export PATH="$WIN_ROOT/toolchain/root/usr/bin:$PATH"
export LIBRARY_PATH="$WIN_ROOT/toolchain/root/usr/x86_64-w64-mingw32/lib${LIBRARY_PATH:+:$LIBRARY_PATH}"
mkdir -p build prefix vendor dist
python3 fetch-deps.py
if [ ! -d vendor/ffmpeg-7.1.5 ]; then tar -xf vendor/ffmpeg-7.1.5.tar.xz -C vendor; fi
if [ ! -d vendor/opus-1.6.1 ]; then tar -xf vendor/opus-1.6.1.tar.gz -C vendor; fi
if [ ! -f prefix/lib/libavcodec.a ]; then
 mkdir -p build/ffmpeg-7.1.5
 (cd build/ffmpeg-7.1.5; ../../vendor/ffmpeg-7.1.5/configure --prefix="$WIN_ROOT/prefix" --target-os=mingw32 --arch=x86_64 --cross-prefix=x86_64-w64-mingw32- --enable-cross-compile --disable-everything --enable-decoder=h264 --enable-parser=h264 --disable-programs --disable-doc --disable-network --disable-autodetect --disable-shared --enable-static --disable-avdevice --disable-avformat --disable-avfilter --disable-swscale --disable-swresample --disable-x86asm --extra-cflags=-O2; make -j4
 # FFmpeg 7.1.5 h2645_sei reset references this auxiliary object even in an
 # H.264-only build. Compile it explicitly without enabling another codec.
 x86_64-w64-mingw32-gcc -O2 -I. -I../../vendor/ffmpeg-7.1.5 -c ../../vendor/ffmpeg-7.1.5/libavcodec/aom_film_grain.c -o libavcodec/aom_film_grain.o
 x86_64-w64-mingw32-ar r libavcodec/libavcodec.a libavcodec/aom_film_grain.o
 make install)
fi
if [ ! -f prefix/lib/libjpeg.a ]; then
 cmake -S ../vendor/libjpeg-turbo-3.1.3 -B build/jpeg -DCMAKE_TOOLCHAIN_FILE="$WIN_ROOT/mingw.cmake" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$WIN_ROOT/prefix" -DENABLE_SHARED=OFF -DENABLE_STATIC=ON -DWITH_TURBOJPEG=OFF -DWITH_TOOLS=OFF -DWITH_TESTS=OFF -DWITH_SIMD=OFF
 cmake --build build/jpeg -j4
 cmake --install build/jpeg
fi
cmake -S . -B build/app -DCMAKE_TOOLCHAIN_FILE="$WIN_ROOT/mingw.cmake" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$WIN_ROOT/prefix"
cmake --build build/app -j4
x86_64-w64-mingw32-strip --strip-debug build/app/3DSRP-Companion.exe -o dist/3DSRP-Companion.exe
