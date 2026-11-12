# Building 3DSRP

Build from the repository root. Do not put console pairings, account credentials,
private signing keys or local deployment secrets in this tree.

## Handheld clients

Install devkitPro's devkitARM, 3DS tools, libctru, Citro2D/Citro3D and the portlibs
used by the standalone app (mbedTLS, Opus and json-c). On Linux also install
CMake, Ninja, Make and Python 3. Set `DEVKITPRO` to your SDK directory, then:

```sh
bash tools/build-3ds-native.sh
```

This builds both `source/3ds/app/3DSRP.3dsx` (New 3DS direct) and
`relay/output/old3ds-relay.3dsx` (relay). The latter is distributed as
`3DSRP-Relay.3dsx`. To build only the relay: `bash relay/3ds/build.sh`.
The existing Docker route for the standalone app remains in `source/README.md`.

CIA packaging needs Python Pillow, DejaVu fonts, makerom 0.19.0 and bannertool
1.2.3. Put the tools on PATH, or set `MAKEROM` and `BANNERTOOL` to their locations:

```sh
python3 source/3ds/app/package_cia.py
python3 relay/3ds/package_cia.py
```

Outputs are `source/3ds/app/3DSRP.cia` and `relay/output/3DSRP-Relay.cia`.
The standalone title is `000400000F0B1700`, New-3DS-only. The relay title is
`000400000F0B1900`, region-free, 64 MB/268 MHz with no New-3DS-only flag. It is
separate from both the standalone app and the old `F0B18` prototype.
Unsigned homebrew CIA tickets require custom firmware.

## Android

Install JDK 17, Android SDK 35, NDK 27.1.12297006 and SDK CMake 3.22.1.
Set `ANDROID_HOME` to the SDK. The included wrapper uses Gradle 8.9:

```sh
cd relay/android
./gradlew :app:assembleRelease
```

The unsigned APK is `app/build/outputs/apk/release/app-release-unsigned.apk`.
Use Android SDK `zipalign -p 4`, then `apksigner sign` with **your own private
keystore**, and `apksigner verify`. Keep keystores and passwords outside GitHub.
No project release key is included. `assembleDebug` remains available for local
work; debug and release signatures are different, so they cannot update each other.
Release 42 is shared by the Android and handheld protocol checks; update the
compatibility constants together if changing that protocol.

## Windows

See `relay/windows/source-build.txt`. On a compatible Linux host:

```sh
cd relay/windows
bash bootstrap-toolchain.sh
bash build.sh
```

The build uses a MinGW-w64 POSIX toolchain and produces
`dist/3DSRP-Companion.exe`. FFmpeg 7.1.5 and Opus 1.6.1 archives are included and
SHA-256-verified by `fetch-deps.py`. Build dependencies are static; the app uses
inbox Windows DLLs, an `asInvoker` manifest and no driver or service.

`package.sh` optionally builds a per-user MSI using wixl/msitools; the public
release uses a direct EXE. `sign.ps1` supports your own trusted Windows signing
certificate. No certificate or private key is provided.

## Host checks

```sh
python3 relay/windows/tests/test_editor.py
python3 relay/windows/tests/test_connection_flow.py
python3 relay/windows/tests/test_console_monitor.py
node --test source/usage-service/test/*.test.mjs
npm --prefix source/usage-dashboard test
```

Additional relay tests are under `relay/tests`; emulator tests need an Android
SDK/emulator, and codec tests need their native dependencies built first.
Host and packaging checks do not replace a real console/phone/Windows test.

## Optional statistics services

The applications are configured for the project's public HTTPS reporting endpoint.
The server source is included for review and self-hosting. Configure your own
Cloudflare account, D1 database and Access identity before deploying. Placeholder
database IDs and identities intentionally cannot target the maintainer's live
resources. Never commit `.dev.vars`, access tokens or database exports.
