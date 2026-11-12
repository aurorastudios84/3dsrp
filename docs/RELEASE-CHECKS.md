# Release checks — 12 November 2026

This release brings the four maintained application components into one source
repository. Local pairing data, captures, diagnostic uploads, receiver tokens,
Android signing keys, workstation build outputs and live dashboard deployment
identities are excluded. Public reporting endpoints and license credits remain.

## Completed

- Fresh native builds of both handheld clients from this source tree.
- CIA content hashes and executable bytes checked against each built ELF.
- Original 3DS relay CIA: separate title ID, region-free, 64 MB/268 MHz, no New
  3DS restriction or Core 2 access. New 3DS CIA: existing separate title ID and
  New-3DS-only metadata retained. Unsigned homebrew tickets require CFW.
- Android release build for ARM64, ARMv7 and x86_64; non-debuggable manifest.
  APK signature verification passed with the private release key (v2/v3).
- Windows x64 release build. Application sources match the user-tested version;
  the build adds source-path mapping so Opus assertion strings do not expose
  the developer's home directory. Streaming and UI logic are unchanged.
- Windows editor, wake/retry/disconnect and console-monitor host regressions passed.
- Relay C/Java compatibility, control mapping, usage reporting, recovery and
  startup tests passed. Controls tests use the preserved relay helper revision;
  an obsolete assertion against a private development Git commit was removed.
- Statistics service: 33 tests passed. Dashboard: 9 tests passed.
- Gitleaks 8.30.1 scans of the public Git history and the complete staged source.
  Findings were reviewed: only Mbed TLS public test keys, PEM marker strings and
  a function parameter false positive. Relay copies match the already-public
  vendored test sources. These are not maintainer credentials.
- Source/artifact searches for known workstation paths, private receiver address,
  personal account names and deployment settings. Compiler path mapping removes
  embedded local paths from TLS/Opus library messages. Release binaries and
  decompressed APK contents passed the final known-private-string check.
- Dependencies retain license notices; pinned Windows codec archives are
  checksum-verified. The matching source archive includes a file hash inventory.
- Reddit promo: 30 seconds, 1080p/30 fps H.264/AAC, real New 3DS footage at normal
  speed, no account/pairing screens. Original project soundtrack; game audio
  omitted. The video identifies the hardware shown and explains both app routes.

## Still needed on hardware

- Install and launch the new relay CIA on a physical original 3DS/2DS; test
  streaming, controls, sound, HOME/sleep/resume and a subsequent launch.
- Smoke-test the freshly rebuilt standalone CIA/3DSX and signed Android APK.
- Smoke-test the path-sanitized Windows EXE. The preceding build worked on the
  user's personal PC, but this exact rebuild has only host/build validation.

Windows signing has not been performed. This release does not claim that every
antivirus or managed-device policy will allow the EXE. It does not install a
service, driver or elevated component. No runtime security policy is bypassed.

## Update notes

- The Android release key is new: debug installs need uninstall/reinstall and
  fresh pairing. Keep the private release key and password backed up securely;
  future APK updates must retain its signing identity.
- Android certificate SHA-256:
  `766d556bf29760ba5c38222d72c6f10f4500245903c5f3e87d7265a41fd35e33`.
- Windows and handheld settings paths are unchanged. Do not delete application
  data or SD settings when replacing the executable or changing CIA/3DSX format.
- The new relay CIA is not the earlier original-model prototype. Its separate
  title ID avoids replacing the standalone New 3DS app or that prototype.
