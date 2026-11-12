# 3DSRP corresponding source

This archive contains the application, the 3DS portion of its modified Chiaki
core, the bundled libraries needed by that build, and their license notices.
Desktop/Android/Switch frontends, experiments, captures and internal records
are omitted. The devkitPro toolchain supplies libctru, Citro2D/Citro3D, Opus,
mbedTLS and json-c; their upstream licenses remain applicable.

## Build

On a Linux host, install Docker and Python 3. From this directory:

```sh
docker build -t n3ds-chiaki-build:latest 3ds/
DOCKER=docker bash 3ds/build.sh
DOCKER=docker bash 3ds/app/build.sh
```

Output: `3ds/app/3DSRP.3dsx`. Your user must have permission to run Docker;
`DOCKER="sudo docker"` also works. No console, pairing files or private
development fixtures are needed to build.

Generated font and icon inputs are included. To regenerate them, install
Python Pillow and DejaVu fonts, then run `python3 tools/3dsrp/make_assets.py`.

See COPYING, NOTICE.md and docs/THREEDSRP-FONT-LICENSE.txt.

## Build the installable CIA

First build the core and application above. On a Linux host install Python
Pillow and DejaVu fonts. Download [makerom 0.19.0](https://github.com/3DSGuy/Project_CTR/releases/tag/makerom-v0.19.0)
and [bannertool 1.2.3](https://github.com/carstene1ns/3ds-bannertool/releases/tag/1.2.3),
then place their executables on PATH (or set MAKEROM and BANNERTOOL to their paths).
Run:

```sh
python3 3ds/app/package_cia.py
```

Output: `3ds/app/3DSRP.cia`. The banner uses the supplied 3DSRP icon,
DejaVu text and generated silence; packaging is configured by
`3ds/app/cia.rsf`. No console data is needed.
The region-free package is marked for New 3DS only and uses title ID
`000400000F0B1700`. It requires a homebrew-enabled console with custom firmware.

## Privacy

See [PRIVACY.md](PRIVACY.md). Optional statistics start OFF and require explicit
consent. Reports contain random installation/session UUIDs, an activity date and streamed
seconds. HOME/sleep checkpoints are recovered automatically on launch under the
original activity dates, with duplicate protection. See the privacy notice for
local queue limits and retention.
The deployed endpoint is configured in `3ds/app/include/usage_config.h`.
The sender uses a private mbedTLS 2.28.8 build with restartable ECC,
certificate/hostname verification and nonblocking sockets. Cryptographic work
yields in short slices between UI frames. It does not use the legacy system HTTP
TLS stack, add a sender thread or wait for reports during exit. The app build
script automatically builds and namespaces the private library; Chiaki continues
to use the existing devkitPro crypto library without ABI changes.
Diagnostic logging and developer UDP senders remain removed. Pairings and
existing app settings remain compatible. Usage preferences are stored separately.

## Usage service

`usage-service/` contains the matching Cloudflare Worker, D1 schema, tests,
retention schedule and private administrator queries. See its README for setup
and statistics access. No Cloudflare account token belongs in this source tree.

## Private usage dashboard

`usage-dashboard/` provides a separate, read-only Cloudflare dashboard for the
existing statistics database: date filters, daily charts, installation IDs,
per-installation activity and CSV export. It requires Cloudflare Access and the
maintainer's exact email; without authentication it returns no usage data.
See [its setup guide](usage-dashboard/README.md). App reporting uses the separate
public usage-service Worker and does not require dashboard sign-in.
