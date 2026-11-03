#!/usr/bin/env python3
"""Read-only qualification of a generated shared EFI; no VM/app mutation."""
import argparse
import copy
from pathlib import Path
import plistlib
import runpy
import tempfile

source = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--image', type=Path, required=True)
args = parser.parse_args()
recipe = runpy.run_path(str(source/'scripts/build-unified-efi.py'))
verify = runpy.run_path(str(source/'scripts/verify-release-build.py'))['verify_unified_firmware']
image = args.image.resolve()
fat = recipe['FAT32'](recipe['volume'](image))
for guest in recipe['EARLY_XHCI_PROFILES']:
    config = plistlib.loads(fat.read('EFI/VIM/'+guest+'/config.plist'))
    boot_args = config['NVRAM']['Add'][recipe['APPLE_BOOT_GUID']]['boot-args'].split()
    assert boot_args.count('usb=0x3000') == 1, 'Missing xHCI/BOT compatibility flags: '+guest
nvram = config['NVRAM']['Add'][recipe['APPLE_BOOT_GUID']]
nvram['boot-args'] = 'keepsyms=1 usb=0x20'
updated = plistlib.loads(recipe['early_xhci_config'](plistlib.dumps(config)))
assert updated['NVRAM']['Add'][recipe['APPLE_BOOT_GUID']]['boot-args'] == 'keepsyms=1 usb=0x3020'
total = sum(sum(map(len, recipe['files'](fat, fat.directory(['EFI', 'VIM', guest])).values()))
            for guest in recipe['PROFILES'])
receipt = {'UnifiedEFI': {'FormatVersion': 1, 'RuntimeProfileSelection': True,
                         'Profiles': list(recipe['PROFILES']), 'ImageBytes': image.stat().st_size,
                         'ProfileBytes': total}}
with tempfile.TemporaryDirectory(prefix='vim-unified-efi-test-') as temporary:
    runtime = Path(temporary)
    (runtime/'Firmware/Common').mkdir(parents=True)
    (runtime/'Firmware/Common/EFI.img').symlink_to(image)
    for guest in recipe['PROFILES']:
        directory = runtime/'Firmware'/guest
        directory.mkdir()
    verify(runtime, source, receipt)
    for key, value in (('Profiles', ['bigsur']), ('ImageBytes', 1)):
        bad = copy.deepcopy(receipt)
        bad['UnifiedEFI'][key] = value
        try:
            verify(runtime, source, bad)
        except RuntimeError:
            pass
        else:
            raise AssertionError('Accepted incomplete common firmware: '+key)
print('PASS: thirteen quiet profile roots, direct common launcher, no per-OS aliases/receipts')
