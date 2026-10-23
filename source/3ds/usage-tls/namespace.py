#!/usr/bin/env python3
"""Keep the restartable statistics TLS library separate from Chiaki's crypto ABI."""
from pathlib import Path
import subprocess,sys,re
build=Path(sys.argv[1]);out=Path(sys.argv[2]);include=Path(sys.argv[3]);prefix=sys.argv[4] if len(sys.argv)>4 else ''
archives=[build/'library'/('lib'+name+'.a') for name in ('mbedtls','mbedx509','mbedcrypto')]
symbols=set()
for p in archives:
 text=subprocess.check_output([prefix+'nm','-g','--defined-only',str(p)],text=True)
 for line in text.splitlines():
  parts=line.split()
  if len(parts)==3 and re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*',parts[2]):symbols.add(parts[2])
assert 'mbedtls_ssl_handshake_step' in symbols and 'mbedtls_ecp_set_max_ops' in symbols
out.mkdir(parents=True,exist_ok=True);mapping=out/'symbols.map'
mapping.write_text(''.join(name+' usage_private_'+name+'\n' for name in sorted(symbols)))
for p in archives:
 target=out/('libusage_'+p.name[3:])
 subprocess.run([prefix+'objcopy','--redefine-syms='+str(mapping),str(p),str(target)],check=True)
 subprocess.run([prefix+'ranlib',str(target)],check=True)
include.parent.mkdir(parents=True,exist_ok=True)
header=('/* Generated private TLS symbol namespace. */\n#ifndef THREEDSRP_PRIVATE_MBEDTLS_SYMBOLS_H\n#define THREEDSRP_PRIVATE_MBEDTLS_SYMBOLS_H\n'+''.join('#define '+name+' usage_private_'+name+'\n' for name in sorted(symbols))+'#endif\n')
if not include.exists() or include.read_text()!=header:include.write_text(header)
