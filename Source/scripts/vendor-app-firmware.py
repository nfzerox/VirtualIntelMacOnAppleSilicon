#!/usr/bin/env python3
"""Capture qualified firmware build inputs, never a VM's writable NVRAM.

Maintenance utility, not needed by fast builds. Uses QEMU's pristine upstream
variable template and the qualified OpenCore FAT images from local bringup.
"""
import argparse
import bz2
import hashlib
import io
from pathlib import Path
import subprocess
import tarfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--qemu', required=True, type=Path)
parser.add_argument('--output', required=True, type=Path)
args = parser.parse_args()
source = Path(__file__).resolve().parents[1]
if args.output.exists():
    parser.error('output already exists')
code = bz2.decompress((args.qemu/'pc-bios/edk2-x86_64-code.fd.bz2').read_bytes())
variables = bz2.decompress(subprocess.check_output([
    'git', '-C', str(args.qemu), 'show',
    '403337f1e661af405a7f0ce4d0a95f3017ceff93^:pc-bios/edk2-i386-vars.fd.bz2']))
if (len(code), len(variables)) != (3653632, 540672):
    parser.error('unqualified firmware geometry')
entries = {'CODE.fd': code, 'VARS.fd': variables}
templates = {'mojave': 'opencore-mojave-arm-autoboot',
             'catalina': 'opencore-catalina-autoboot',
             'bigsur': 'opencore-bigsur-arm-autoboot'}
for guest, template in templates.items():
    entries[guest+'/BOOT.img'] = (source/'artifacts/qemu'/template/'cd/BOOT.img').read_bytes()
with tarfile.open(args.output, 'x:gz') as archive:
    for name, data in entries.items():
        entry = tarfile.TarInfo(name)
        entry.size = len(data)
        entry.mode = 0o644
        entry.mtime = 0
        archive.addfile(entry, io.BytesIO(data))
print(hashlib.sha256(args.output.read_bytes()).hexdigest(), args.output.name)
