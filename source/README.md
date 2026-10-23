# Build the 3DSRP CIA

Requires Linux, Docker with permission to run containers, Python 3, Pillow,
DejaVu fonts, [makerom 0.19.0](https://github.com/3DSGuy/Project_CTR/releases/tag/makerom-v0.19.0)
and [bannertool 1.2.3](https://github.com/carstene1ns/3ds-bannertool/releases/tag/1.2.3).
Place makerom and bannertool on `PATH`, or set `MAKEROM` and `BANNERTOOL`.
On Debian/Ubuntu, install the Python/font prerequisites with:

```sh
sudo apt-get install python3 python3-pil fonts-dejavu-core
```

From this `source/` directory:

```sh
docker build -t n3ds-chiaki-build:latest 3ds/
DOCKER=docker bash 3ds/build.sh
DOCKER=docker bash 3ds/app/build.sh
python3 3ds/app/package_cia.py
```

Output: `3ds/app/3DSRP.cia`. Install using FBI on a homebrew-enabled New 3DS.
The same build also produces `3ds/app/3DSRP.3dsx`.

The image supplies devkitARM and the 3DS libraries; the Dockerfile adds protobuf
tools. Required core/dependency sources and generated font/icon inputs are
included. No console credentials or external service deployment are needed.

If needed, set `DOCKER="sudo docker"` or choose another image through
`CHIAKI_3DS_IMAGE`. The default image tag is unpinned.

See `COPYING`, `NOTICE.md` and `docs/THREEDSRP-FONT-LICENSE.txt`.
