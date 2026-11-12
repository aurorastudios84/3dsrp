#!/usr/bin/env bash
# Optional Ubuntu/Debian bootstrap without sudo. Uses the host APT repositories.
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p toolchain/packages toolchain/root
WIN_ROOT="$PWD"
(cd toolchain/packages; apt-get download g++-mingw-w64-x86-64-posix gcc-mingw-w64-x86-64-posix gcc-mingw-w64-x86-64-posix-runtime mingw-w64-x86-64-dev mingw-w64-common binutils-mingw-w64-x86-64 gcc-mingw-w64-base nasm msitools wixl wixl-data libmsi-1.0-0 libgcab-1.0-0)
for package in toolchain/packages/*.deb; do dpkg-deb -x "$package" "$WIN_ROOT/toolchain/root"; done
for compiler in gcc g++ cpp; do
 if [ ! -e "toolchain/root/usr/bin/x86_64-w64-mingw32-$compiler" ]; then
  ln -s "x86_64-w64-mingw32-$compiler-posix" "toolchain/root/usr/bin/x86_64-w64-mingw32-$compiler"
 fi
done
