#!/usr/bin/env python3
"""Tiny real ISO round trip; no guest disks, installation or donor extraction."""
import argparse
from pathlib import Path
import plistlib
import runpy
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--scratch', required=True, type=Path)
args = parser.parse_args()
source = Path(__file__).resolve().parents[1]
build = runpy.run_path(str(source/'scripts/package-guest-support-media.py'))['build']
with tempfile.TemporaryDirectory(prefix='support-media-fixture-', dir=args.scratch) as name, \
        tempfile.TemporaryDirectory(prefix='vim-support-mount-') as mount_name:
    root = Path(name)
    tools, payloads = root/'Tools', root/'Payloads'
    tools.mkdir()
    payloads.mkdir()
    guests = ('mojave', 'catalina', 'bigsur')
    for tool in ('guest-macho', 'guest-image-patch', 'guest-prepare', 'guest-install', 'guest-progress', 'codesign_allocate'):
        (tools/tool).write_bytes(b'test-only tool\n')
    records = {}
    for guest in guests:
        archive = guest+('.tar.gz' if guest == 'catalina' else '.tar.xz')
        records[guest] = {'Archive': archive}
        (payloads/archive).write_bytes(b'test-only archive\n')
        (tools/(guest+'-support-icon.png')).write_bytes(b'test-only icon\n')
    (payloads/'Payloads.plist').write_bytes(plistlib.dumps({'Guests': records}))
    receipt = build(payloads, tools)
    assert {p.name for p in payloads.iterdir()} == {guest+'.iso' for guest in guests}
    mount = Path(mount_name)  # macOS mount points cannot live on exFAT.
    for guest in guests:
        subprocess.run(['hdiutil', 'attach', '-readonly', '-nobrowse', '-noautoopen', '-mountpoint', str(mount), str(payloads/(guest+'.iso'))], check=True, stdout=subprocess.DEVNULL)
        try:
            marker = plistlib.loads((mount/'Support.plist').read_bytes())
            assert marker == {'FormatVersion': 1, 'Kind': 'VirtualIntelMacSupport', 'Guest': guest}
            assert {p.name for p in mount.iterdir() if not p.name.startswith('.')} == set(receipt[guest]['Files']) | {'Support.plist'}
            for filename in receipt[guest]['Files']:
                assert (mount/filename).stat().st_size > 0
        finally:
            subprocess.run(['hdiutil', 'detach', str(mount)], check=True, stdout=subprocess.DEVNULL)
print('PASS: three real support ISOs, OS-only marker, gzip/xz, no redundant outer manifests/archives')
