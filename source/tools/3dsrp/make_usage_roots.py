#!/usr/bin/env python3
"""Encode bundled public trust roots as DER arrays; no private keys involved."""
from pathlib import Path
import re,ssl
root=Path(__file__).resolve().parents[2]
pem=(root/'3ds/app/resources/usage-roots.pem').read_text()
lines=['/* Public ISRG and GTS roots; see resources/USAGE-ROOTS.txt. */', '#ifndef THREEDSRP_USAGE_ROOTS_H','#define THREEDSRP_USAGE_ROOTS_H']
certs=re.findall(r'-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----',pem,re.S)
for n,cert in enumerate(certs):
 data=ssl.PEM_cert_to_DER_cert(cert)
 lines.append(f'static const unsigned char usage_root_{n}[]={{')
 lines += [','.join(str(x) for x in data[i:i+24])+',' for i in range(0,len(data),24)]
 lines.append('};')
lines.append('static const struct { const unsigned char *bytes; unsigned size; } usage_roots[]={')
lines += [f'{{usage_root_{n},sizeof(usage_root_{n})}},' for n in range(len(certs))]
lines += ['};',f'#define USAGE_ROOT_COUNT {len(certs)}u','#endif']
(root/'3ds/app/include/usage_roots.h').write_text('\n'.join(lines)+'\n')
