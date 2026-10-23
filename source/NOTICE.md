# 3DSRP acknowledgements

3DSRP is a native New Nintendo 3DS port built on Chiaki-ng, itself derived from
Chiaki. It does not claim original authorship of the Remote Play protocol engine.

- Chiaki / chiaki-ng contributors: discovery, registration, session protocol,
  encryption, transport, stream reception and controller feedback.
- AuroraStudios84: maintenance of the 3DS port and interface.
- devkitPro contributors: devkitARM, libctru and Citro2D/Citro3D.
- Opus, mbedTLS, nanopb, Jerasure/gf-complete, json-c and other dependency
  authors: their respective components retain their own copyright notices
  and licenses in the upstream source tree and development environment.
- Mbed TLS 2.28.8: private restartable TLS build for optional statistics; Apache 2.0
  license and modified configuration in `vendor/usage-mbedtls/`.
- DejaVu / Bitstream font contributors: generated interface glyphs; see
  `docs/THREEDSRP-FONT-LICENSE.txt` for the complete font license.

The Chiaki-derived combined application is covered by GNU AGPLv3 with the
applicable OpenSSL linking exception, reproduced in `COPYING`. Original notices
in `chiaki-ng/` and third-party components must remain intact. No warranty is
provided. See the license for redistribution, modification and source obligations.
