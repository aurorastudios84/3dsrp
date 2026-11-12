# 3DSRP Relay

The original 3DS/2DS client uses either the Android or Windows companion to
receive PlayStation Remote Play video and convert it for the handheld.
See the [main README](../README.md) for downloads and setup, and
[build instructions](../docs/BUILDING.md).

The shared protocol release is 42. Keep the companion and handheld on matching
releases. Transport uses UDP port 29870 on a trusted LAN; it is not suitable for
exposing directly to the Internet. Each companion serves one active handheld.

`3ds/shared` retains the exact shared helper revision used by the relay, separately
from the New 3DS direct app. The CIA and 3DSX use the same relay ELF; packaging
changes the Home Menu identity, not the streaming protocol.

Vendored Chiaki, Mbed TLS and libjpeg-turbo sources retain their licenses. Codec
and protocol test fixtures are synthetic, not saved console sessions.
