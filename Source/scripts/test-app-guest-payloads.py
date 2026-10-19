#!/usr/bin/env python3
"""Inspect generated app archives; never extract or install system files."""
import hashlib
from pathlib import Path, PurePosixPath
import plistlib
import sys
import tarfile
import subprocess
import tempfile
from contextlib import contextmanager

@contextmanager
def payload_archive(root, guest, record):
    archive = root/record['Archive']
    assert archive.parent == root
    if archive.is_file():
        yield archive
        return
    media = plistlib.loads((root.parent/'Packaging.plist').read_bytes())['GuestSupportMedia'][guest]
    image = root/media['Image']
    assert image.parent == root
    assert hashlib.sha256(image.read_bytes()).hexdigest() == media['SHA256']
    with tempfile.TemporaryDirectory(prefix='vim-support-check-') as temporary:
        mount = Path(temporary)/'Support'
        mount.mkdir()
        subprocess.run(['/usr/bin/hdiutil', 'attach', '-readonly', '-nobrowse',
                        '-mountpoint', str(mount), str(image)], check=True, stdout=subprocess.DEVNULL)
        try:
            trusted = (root/media['Manifest']).read_bytes()
            assert (mount/'Support.plist').read_bytes() == trusted
            for name, file in media['Files'].items():
                content = (mount/name).read_bytes()
                assert len(content) == file['Size']
                assert hashlib.sha256(content).hexdigest() == file['SHA256']
            yield mount/('payload.tar.xz' if archive.suffix == '.xz' else 'payload.tar.gz')
        finally:
            subprocess.run(['/usr/bin/hdiutil', 'detach', str(mount), '-quiet'], check=True)

root = Path(sys.argv[1])
receipt = plistlib.loads((root/'Payloads.plist').read_bytes())
assert set(receipt['Guests']) == {'mojave', 'catalina', 'bigsur'}
for guest, record in receipt['Guests'].items():
    with payload_archive(root, guest, record) as archive:
        digest = hashlib.sha256()
        with archive.open('rb') as stream:
            for block in iter(lambda: stream.read(1024*1024), b''): digest.update(block)
        assert digest.hexdigest() == record['SHA256']
        with tarfile.open(archive) as stream:
            members = stream.getmembers()
            names = {'/'.join(PurePosixPath(m.name).parts[1:]) for m in members}
            assert len(members) == record['EntryCount']
            for prefix in receipt['RegeneratedFromGuest']:
                assert not any(n == prefix or n.startswith(prefix+'/') for n in names), prefix
            if guest == 'bigsur':
                assert names == {'','GuestHelpers','GuestHelpers/PVGBigSurMetalCompat.dylib','GuestHelpers/PVGOpenGLMetal.dylib'}
                assert record['NativeGPUBundlesFromGuest'] is True
                assert record['SystemVolumeChanges'] is False
                assert record['BootCacheRebuildRequired'] is False
                assert record['RequiresGuestStaging'] is True
                print('PASS Big Sur: only our adapters ship; native bundles come from the guest')
                continue
            if guest == 'mojave':
                assert 'usr/lib/PVGGLIOKit.dylib' in names
            driver_files = record['IOSurfaceABIFiles']
            assert driver_files
            for relative, expected in driver_files.items():
                assert relative.startswith(guest+'/PVGIOSurfaceABI.kext/')
                member = 'payload/Library/Extensions/'+relative.split('/',1)[1]
                assert hashlib.sha256(stream.extractfile(member).read()).hexdigest() == expected
            assert 'System/Library/Extensions/AppleParavirtGPU.kext/Contents/Info.plist' in names
            assert 'Library/Extensions/VoodooHDA.kext/Contents/MacOS/VoodooHDA' in names
            assert 'System/Library/PrivateFrameworks/MetalSerializer.framework/Versions/A/MetalSerializer' in names
            assert record['RequiresGuestStaging'] is True
    print('PASS', guest, ': archive checksum, regenerated-image omission and essential donors retained')
