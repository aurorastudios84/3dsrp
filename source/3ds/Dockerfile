# Build image for the n3ds-chiaki port.
#
# devkitpro/devkitarm has the cross toolchain, libctru and the 3DS portlibs,
# but no protobuf compiler, which chiaki-ng's lib needs to turn takion.proto
# into nanopb C code. This layer adds it so the cross build needs no network
# beyond the initial pull.
#
# Build:  docker build -t n3ds-chiaki-build:latest 3ds/
# Use:    docker run --rm -v "$PWD/chiaki-ng:/work" -w /work n3ds-chiaki-build:latest ...
FROM devkitpro/devkitarm:latest

RUN apt-get update \
 && apt-get install -y --no-install-recommends protobuf-compiler python3-protobuf \
 && rm -rf /var/lib/apt/lists/*

RUN protoc --version && python3 -c "import google.protobuf; print('python protobuf', google.protobuf.__version__)"
