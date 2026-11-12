# 3DSRP

PlayStation Remote Play for the Nintendo 3DS family, based on Chiaki / Chiaki-ng.

**[Downloads](https://github.com/aurorastudios84/3dsrp/releases/latest)** ·
[Setup and builds](docs/BUILDING.md) · [Privacy](PRIVACY.md) · [Credits](NOTICE.md)

| Your handheld | What to run | Companion required? |
| --- | --- | --- |
| New 3DS, New 3DS XL/LL, New 2DS XL | 3DSRP New3DS | No: connects directly to PS4/PS5 |
| Original 3DS, 3DS XL/LL, original 2DS | 3DSRP Relay | Yes: Android **or** Windows |

The relay client can also run on New 3DS hardware. You only need one companion.
A homebrew-enabled handheld and your own PS4 or PS5 are required. Start with
all devices on the same trusted local network. Performance depends on your
handheld, companion hardware and Wi-Fi; this project makes no fixed FPS promise.

## Download

These links retain the same filenames across releases:

| Download | Use |
| --- | --- |
| [New 3DS CIA](https://github.com/aurorastudios84/3dsrp/releases/latest/download/3DSRP-New3DS.cia) | Home Menu installation on **New** models only |
| [New 3DS 3DSX](https://github.com/aurorastudios84/3dsrp/releases/latest/download/3DSRP-New3DS.3dsx) | Homebrew Launcher on New models |
| [Relay CIA](https://github.com/aurorastudios84/3dsrp/releases/latest/download/3DSRP-Relay.cia) | Home Menu relay client, including original models |
| [Relay 3DSX](https://github.com/aurorastudios84/3dsrp/releases/latest/download/3DSRP-Relay.3dsx) | Homebrew Launcher relay client |
| [Android APK](https://github.com/aurorastudios84/3dsrp/releases/latest/download/3DSRP-Companion.apk) | Android 8.0+ companion |
| [Windows EXE](https://github.com/aurorastudios84/3dsrp/releases/latest/download/3DSRP-Companion.exe) | Windows x64 companion; download and run directly |
| [Checksums](https://github.com/aurorastudios84/3dsrp/releases/latest/download/SHA256SUMS.txt) | SHA-256 hashes of release assets |

The Windows app runs as your current user, without an admin installer or extra
runtime. It is unsigned; managed computers may block it under their policies.
The direct EXE is the supported public download for this release.

**First public relay CIA:** package integrity, title metadata and executable
contents have been checked, but installation and launch on a physical original
3DS still need confirmation. The CIA and 3DSX use the same built relay executable.
The standalone New 3DS artifacts are also fresh builds, not a new hardware test.
See each release's validation notes before updating.

## Start playing

1. Enable Remote Play on your PlayStation. Allow network wake from rest mode if
   you want the app to wake it. Use the app's setup instructions to pair your console.
2. **New 3DS direct:** install the New3DS CIA with your existing CIA installer,
   or put the 3DSX under `/3ds/` on the SD card and open it in Homebrew Launcher.
3. **Original 3DS relay:** install the Relay CIA or launch its 3DSX. Install/open
   3DSRP Companion on Android or Windows and pair your PlayStation there.
4. Enter the **phone/PC Wi-Fi IPv4 address shown in the companion** on the 3DS.
   This is the relay address, not the PlayStation address. Keep the companion running.

Use a trusted local network. The handheld-to-companion UDP transport is not
end-to-end encrypted or authenticated; do not expose its port to the Internet.
Guest Wi-Fi/client isolation can prevent devices reaching each other. Network
or firewall policies can still restrict connectivity even though the Windows app
itself does not require elevation.

## Updating and saved pairings

Handheld settings remain under `/3ds/n3ds-remoteplay/` (standalone) or
`/3ds/3dsrp-relay/` (relay). CIA and 3DSX of the same app share those folders.
Replacing the Windows EXE preserves its existing per-user app data and pairings.

The public Android APK uses a new private release signing key. Existing debug
builds cannot be updated in place with this APK: uninstall the debug build,
install the release APK, and pair again. Later releases using this same release
key can update normally. Never share pairing files or upload them to an issue.

Optional usage statistics require consent. Windows startup diagnostics remain
local; automatic startup-log uploads have been removed. See [PRIVACY.md](PRIVACY.md).

## Source layout

- `source/3ds/app`: standalone New 3DS application.
- `source/chiaki-ng`: its modified Chiaki core and shared dependencies.
- `relay/3ds`: original-model relay client and CIA packaging.
- `relay/android`: Android companion.
- `relay/windows`: Windows companion.
- `relay/common`, `relay/vendor`: shared relay protocol, processing and dependencies.
- `source/usage-service`, `source/usage-dashboard`: optional statistics backend
  source; deployment configuration contains placeholders, not live credentials.
- `promo`: Reddit video edit script and suggested post text.

The relay's shared handheld helpers are preserved in `relay/3ds/shared` so its
working revision stays independent of the standalone application's revision.
Build instructions and validation details are in [docs/BUILDING.md](docs/BUILDING.md)
and [docs/RELEASE-CHECKS.md](docs/RELEASE-CHECKS.md).

Community homebrew, not affiliated with Sony or Nintendo. See
[AGPLv3 with the OpenSSL exception](COPYING), [credits](NOTICE.md) and the
[font license](FONT-LICENSE.txt). Third-party components retain their own licenses.
