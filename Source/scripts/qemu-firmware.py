#!/usr/bin/env python3
"""Prepare independent x86_64 firmware/variables from upstream QEMU assets.

The current upstream descriptor still references edk2-i386-vars.fd after the
template was deleted. Recover that same template from its last upstream version;
never overwrite a VM's existing variable store.
"""
import argparse
import bz2
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('output', type=Path)
parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[1]/'vendor/qemu')
args = parser.parse_args()
code = bz2.decompress((args.source / 'pc-bios/edk2-x86_64-code.fd.bz2').read_bytes())
template = args.source / 'pc-bios/edk2-i386-vars.fd.bz2'
if template.exists():
    packed = template.read_bytes()
else:
    packed = subprocess.check_output(['git', '-C', str(args.source), 'show',
        '403337f1e661af405a7f0ce4d0a95f3017ceff93^:pc-bios/edk2-i386-vars.fd.bz2'])
variables = bz2.decompress(packed)
if len(code) != 3653632 or len(variables) != 540672:
    parser.error('Unqualified firmware layout; inspect before combining code and variables')
args.output.mkdir(parents=True, exist_ok=False)
(args.output / 'CODE.fd').write_bytes(code)
(args.output / 'VARS.fd').write_bytes(variables)
print('Prepared 4 MiB pflash layout (code + private writable variables). Boot not yet tested.')
