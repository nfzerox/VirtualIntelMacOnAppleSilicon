#!/usr/bin/env python3
"""Build an isolated PVG EFI CD from the supplied OpenCore release tree.

Never edits the source checkout. The generated FAT El Torito image contains the
actual release EFI tree and patched configuration, not the source's older BOOT.img.
"""
import argparse
from pathlib import Path
import plistlib
import shutil
import subprocess
import uuid

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('output', type=Path, help='new internal-SSD output directory')
parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[1]/'vendor/source/OpenCore-ISO')
parser.add_argument('--boot-arg', action='append', default=[], help='additional boot argument for an isolated development ISO')
parser.add_argument('--resolution', help='firmware resolution for isolated bringup, e.g. 1280x800')
parser.add_argument('--builtin-picker', action='store_true', help='avoid the graphical external picker during bringup')
parser.add_argument('--quiet-boot', action='store_true', help='omit verbose/debug boot output for app firmware')
args = parser.parse_args()
source = args.source.resolve() / 'EFI_RELEASE' / 'EFI'
if not (source / 'OC/config.plist').is_file():
    parser.error('Missing release EFI/config.plist')
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=False)
stage = output / 'cd'
stage.mkdir()
image = stage / 'BOOT.img'
subprocess.run(['hdiutil', 'create', '-size', '64m', '-fs', 'MS-DOS FAT32',
                '-volname', 'PVG-EFI', '-layout', 'NONE', '-type', 'UDTO', str(image)], check=True)
# DVD/CD master is raw data, without a UDIF trailer. hdiutil appends .cdr.
if not image.exists() and image.with_suffix('.img.cdr').exists():
    image.with_suffix('.img.cdr').rename(image)
attached = plistlib.loads(subprocess.check_output(
    ['hdiutil', 'attach', '-nobrowse', '-plist', str(image)]))
entities = attached['system-entities']
device = entities[0]['dev-entry']
try:
    mount = Path(next(e['mount-point'] for e in entities if 'mount-point' in e))
    shutil.copytree(source, mount / 'EFI')
    config_path = mount / 'EFI/OC/config.plist'
    with config_path.open('rb') as stream:
        config = plistlib.load(stream)
    apple_guid = '7C436110-AB2A-4BBB-A880-FE41995C9F82'
    nvram = config['NVRAM']['Add'][apple_guid]
    flags = [f for f in nvram.get('boot-args', '').split()
             if not f.startswith(('debug=', 'serial='))
             and not (args.quiet_boot and (f == '-v' or f.startswith('keepsyms=')))]
    additions = ['amfi_get_out_of_my_way=0x1']
    if not args.quiet_boot:
        additions += ['-v', 'keepsyms=1', 'debug=0x108', 'serial=1']
    for flag in additions + args.boot_arg:
        if flag not in flags:
            flags.append(flag)
    nvram['boot-args'] = ' '.join(flags)
    nvram['csr-active-config'] = (0x67).to_bytes(4, 'little')
    config['Misc']['Security']['SecureBootModel'] = 'Disabled'
    # Zero timeout with a visible picker waits indefinitely: hide it too.
    config['Misc']['Boot']['ShowPicker'] = False
    config['Misc']['Boot']['Timeout'] = 0
    if args.quiet_boot:
        config['Misc']['Debug']['Target'] = 0
        config['Misc']['Debug']['DisplayLevel'] = 0
    if args.resolution:
        config['UEFI']['Output']['Resolution'] = args.resolution
    if args.builtin_picker:
        config['Misc']['Boot']['PickerMode'] = 'Builtin'
    # The installed backport owns its Lilu/plugin ABI. Do not replace Lilu with
    # the bootloader bundle's version or inject real-hardware GPU patches.
    # The launcher supplies an Apple SMC device, so VirtualSMC is unnecessary.
    for kext in config['Kernel']['Add']:
        if kext['BundlePath'] in ('Lilu.kext', 'WhateverGreen.kext', 'VirtualSMC.kext'):
            kext['Enabled'] = False
    config['PlatformInfo']['Generic']['SystemUUID'] = str(uuid.uuid4()).upper()
    with config_path.open('wb') as stream:
        plistlib.dump(config, stream, sort_keys=False)
    shutil.copy2(config_path, output / 'config.plist')
    subprocess.run(['plutil', '-lint', str(config_path)], check=True)
finally:
    subprocess.run(['hdiutil', 'detach', device], check=True)
subprocess.run(['xorriso', '-as', 'mkisofs', '-iso-level', '3', '-V', 'PVG-OpenCore',
                '-e', 'BOOT.img', '-no-emul-boot', '-o', str(output / 'OpenCore.iso'),
                str(stage)], check=True)
print(output / 'OpenCore.iso')
