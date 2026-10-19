#!/usr/bin/env python3
"""Read tiny, independently created DMGs through the production block driver.

No user image is converted, mounted on the host, or expanded to another file.
Uses an existing runtime and an explicit scratch directory; never boots a VM.
"""
import argparse
import json
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--runtime', required=True, type=Path)
p.add_argument('--scratch', required=True, type=Path)
a = p.parse_args()
source = Path(__file__).resolve().parents[1]
a.scratch.mkdir(parents=True, exist_ok=True)
image_tool = a.runtime.resolve() / 'qemu-img'
for fmt in ('UDRO', 'UDZO', 'UDBZ', 'ULFO', 'ULMO'):
    image = a.scratch.resolve() / (fmt+'.dmg')
    if not image.exists():
        subprocess.run(['/usr/bin/hdiutil', 'create', '-srcfolder',
                        str(source/'probes/fixtures/direct-dmg'), '-size', '8m',
                        '-fs', 'HFS+', '-volname', 'VIMDirectDMG', '-format', fmt,
                        str(image)], check=True, timeout=45)
    before = image.stat()
    if fmt == 'ULMO':
        unsupported = subprocess.run([str(image_tool), 'info', '--output=json',
                                     '-f', 'dmg', str(image)], capture_output=True,
                                    text=True, timeout=15)
        assert unsupported.returncode != 0 and 'not supported' in unsupported.stderr
        print('PASS: unsupported XZ/LZMA is rejected at open, not exposed with unreadable sectors', flush=True)
        continue
    info = json.loads(subprocess.check_output([str(image_tool), 'info',
                      '--output=json', '-f', 'dmg', str(image)], timeout=15))
    assert info['format'] == 'dmg' and 0 < info['virtual-size'] <= 32*1024*1024
    # bench defaults to reads. Read every guest-visible sector without writing
    # a raw image, and exercise decompression rather than only metadata parsing.
    subprocess.run([str(image_tool), 'bench', '-f', 'dmg', '-c',
                    str(info['virtual-size']//512), '-s', '512', str(image)],
                   check=True, timeout=20)
    after = image.stat()
    assert (before.st_ino, before.st_size, before.st_mtime_ns) == (
            after.st_ino, after.st_size, after.st_mtime_ns)
    print(f'PASS: {fmt}, direct read of {info["virtual-size"]} bytes; original unchanged', flush=True)
