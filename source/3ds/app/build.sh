#!/usr/bin/env bash
# Build the Remote Play app (.3dsx) with devkitARM inside the build container.
#
#   ./3ds/app/build.sh          # make
#   ./3ds/app/build.sh clean    # make clean
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
IMAGE="${CHIAKI_3DS_IMAGE:-n3ds-chiaki-build:latest}"

# Set DOCKER="sudo docker" if your Docker setup requires sudo.
docker_() { ${DOCKER:-docker} "$@"; }

bash "$ROOT/3ds/build_usage_tls.sh"

BUILD_ID=$(python3 "$ROOT/tools/g3/source_id.py")
python3 - "$HERE/include/build_id.h" "$BUILD_ID" <<'PYID'
import pathlib,sys
p=pathlib.Path(sys.argv[1]); text='#define RP_BUILD_ID "'+sys.argv[2]+'"\n'
if not p.exists() or p.read_text()!=text: p.write_text(text)
PYID
docker_ run --rm --network none -v "$ROOT:/work" -w /work/3ds/app "$IMAGE" bash -lc "make ${1:-}"
