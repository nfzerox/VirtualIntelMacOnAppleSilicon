#!/usr/bin/env python3
"""Replace bulk payload archives with guest-readable, immutable support discs."""
import hashlib
from pathlib import Path
import plistlib
import shutil
import subprocess
import tempfile


def build(payloads, tools):
    payloads, tools = Path(payloads), Path(tools)
    manifest = plistlib.loads((payloads/'Payloads.plist').read_bytes())
    receipts = {}
    for guest in ('mojave', 'catalina', 'bigsur'):
        record = manifest['Guests'][guest]
        archive = payloads/record['Archive']
        if hashlib.sha256(archive.read_bytes()).hexdigest() != record['SHA256']:
            raise RuntimeError(f'{guest}: support payload checksum mismatch')
        if record['Archive'] not in (guest+'.tar.xz', guest+'.tar.gz'):
            raise RuntimeError('Invalid guest payload archive name')
        sources = {'payload.tar.xz' if archive.suffix == '.xz' else 'payload.tar.gz': archive}
        sources.update({name: tools/name for name in
                        ('guest-macho', 'guest-image-patch', 'guest-prepare', 'guest-install', 'guest-progress', 'codesign_allocate')})
        sources['SupportIcon.png'] = tools/f'{guest}-support-icon.png'
        entries = {name: {'Size': path.stat().st_size,
                          'SHA256': hashlib.sha256(path.read_bytes()).hexdigest()}
                   for name, path in sources.items()}
        data = plistlib.dumps({'FormatVersion': 1, 'Guest': guest, 'Files': entries},
                              fmt=plistlib.FMT_BINARY, sort_keys=True)
        # Build beside the final output so large inputs stay on the configured
        # external scratch/library drive, not in the startup disk's /tmp.
        with tempfile.TemporaryDirectory(prefix='.support-', dir=payloads) as temporary:
            stage = Path(temporary)
            for name, source in sources.items():
                shutil.copyfile(source, stage/name)
            (stage/'Support.plist').write_bytes(data)
            image = payloads/f'{guest}.iso'
            if image.exists():
                raise RuntimeError(f'Support media already exists: {image}')
            subprocess.run(['/usr/bin/hdiutil', 'makehybrid', '-hfs', '-iso', '-joliet', '-hfs-volume-name',
                            'Virtual Intel Mac Support', '-o', str(image), str(stage), '-quiet'], check=True)
        (payloads/f'{guest}-support.plist').write_bytes(data)
        receipts[guest] = {'Image': image.name, 'SHA256': hashlib.sha256(image.read_bytes()).hexdigest(),
                            'Manifest': f'{guest}-support.plist', 'Files': entries}
        # The archive is inside the ISO. Keep provenance in Payloads.plist, but
        # do not ship a second 70–100 MB copy or copy it into every VM bundle.
        archive.unlink()
    return receipts
