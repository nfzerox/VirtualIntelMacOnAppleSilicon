#!/usr/bin/env python3
"""Experimental Mojave Core.pkg finalization on an isolated mounted scratch image.

Run only after successful root-owned pkgutil --expand-full to .VIMAssembly.
Does not execute package scripts, create users, or modify the host OS.
"""
import argparse
import os
import pathlib
import plistlib
import shutil
import subprocess
import xml.etree.ElementTree as ET

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--volume', required=True, type=pathlib.Path)
p.add_argument('--image', required=True, type=pathlib.Path)
a = p.parse_args()
root, image = a.volume.resolve(), a.image.resolve()
assert os.geteuid() == 0, 'Root is required to preserve guest metadata'
assert root != pathlib.Path('/') and os.path.ismount(root), 'Expected a separate mounted volume'
scratch = os.environ.get('VIM_BUILD_SCRATCH')
assert scratch and image.is_relative_to(pathlib.Path(scratch).resolve()), \
    'Set VIM_BUILD_SCRATCH; only generated images inside that scratch folder are supported'
images = plistlib.loads(subprocess.check_output(['/usr/bin/hdiutil', 'info', '-plist']))['images']
assert any(pathlib.Path(i.get('image-path', '/')).resolve() == image and
           any(e.get('mount-point') == str(root) for e in i.get('system-entities', []))
           for i in images), 'Volume does not belong to the specified scratch image'
stage = root / '.VIMAssembly'
payload = stage / 'Payload'
assert not stage.is_symlink() and not payload.is_symlink()
info = ET.parse(stage / 'PackageInfo').getroot()
assert info.attrib['identifier'] == 'com.apple.pkg.Core'
version = plistlib.loads((payload / 'System/Library/CoreServices/SystemVersion.plist').read_bytes())
assert version['ProductVersion'].startswith('10.14.'), 'Mojave only'
for path in ['usr/standalone/i386/boot.efi', 'System/Library/PrelinkedKernels/prelinkedkernel',
             'System/Library/Kernels/kernel', 'sbin/launchd']:
    assert (payload / path).is_file(), f'Missing boot input: {path}'
assert not (payload / 'private/var/db/.AppleSetupDone').exists(), 'Must be a fresh installation'
entries = list(payload.iterdir())
assert all(not (root / e.name).exists() and not (root / e.name).is_symlink() for e in entries), 'Destination conflict'
for entry in entries:
    entry.rename(root / entry.name)
os.chown(root, 0, 0)
os.chmod(root, 0o755)
boot = root / 'System/Library/CoreServices/boot.efi'
if not boot.exists():
    shutil.copy2(root / 'usr/standalone/i386/boot.efi', boot)
    os.chown(boot, 0, 0)

# The fresh-install subset of Core.pkg postinstall_actions. No upgrades or
# firmware scripts are run. Every destination stays within the generated root.
local = root / 'usr/local'
local.mkdir(exist_ok=True)
(local / '.com.apple.installer.keep').touch()
manifest = root / '.AppleBundledSoftware.plist'
if manifest.exists():
    target = root / 'private/var/db/.MASManifest'
    manifest.rename(target)
    os.chown(target, 0, 0)
    os.chmod(target, 0o664)
apache = root / 'private/etc/apache2/httpd.conf'
if apache.is_file() and (root / 'usr/libexec/apache2/mod_hfs_apple.so').is_file():
    apache.write_text(apache.read_text().replace('#LoadModule hfs_apple_module', 'LoadModule hfs_apple_module'))
for name in ['bin', 'cores', 'dev', 'etc', 'private', 'sbin', 'tmp', 'usr', 'var',
             'Volumes', '.vol', 'Network']:
    path = root / name
    if path.exists() or path.is_symlink():
        subprocess.run(['/usr/bin/chflags', '-h', 'hidden', str(path)], check=True)
receipt = {'Stage': 'host-populated', 'Guest': 'mojave', 'Version': version,
           'SetupAssistantValidated': False, 'SourcePackage': info.attrib['identifier'],
           'Deferred': ['user-template and deny-delete ACLs', 'package receipts',
                        'dyld shared-cache creation', 'graphics payload', 'boot qualification']}
(stage / 'HostInstall.plist').write_bytes(plistlib.dumps(receipt))
print('Populated Mojave root; boot qualification is still required:', root, flush=True)
