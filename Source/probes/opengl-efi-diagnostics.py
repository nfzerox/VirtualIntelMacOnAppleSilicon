#!/usr/bin/env python3
"""Make a new, bounded NVRAM-log EFI copy for --guest-efi boot diagnosis.

By default the production RELEASE loader and all drivers remain byte-identical.
Only logging configuration in the copied GPT/ESP is changed; no console or
file logging is enabled and the app opens this image as a disposable snapshot.
--loader explicitly opts into a source-built diagnostic RELEASE loader.
Inspect the test VM's
own NVRAM boot-log after stopping it, then boot without the override. Never
use this copy for release packaging or a performance measurement.
"""
import argparse
from pathlib import Path
import plistlib
import runpy
import re
import struct
import uuid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--profile', choices=('tiger', 'leopard', 'snowleopard', 'lion', 'mountainlion',
        'mavericks', 'yosemite', 'elcapitan', 'sierra', 'highsierra', 'mojave', 'catalina', 'bigsur'),
        help='select the guest profile when editing a copy of the unified firmware')
    parser.add_argument('--verbose', action='store_true', help='Show native Apple booter/kernel messages for one diagnostic boot')
    parser.add_argument('--boot-arg', action='append', default=[],
                        help='Explicit native diagnostic boot token; repeated tokens allowed, never changes host NVRAM')
    parser.add_argument('--stock-display', action='store_true',
                        help='Recovery isolation only: omit injected graphics/audio and their forced native dependencies; keep serial control')
    parser.add_argument('--no-injected-kexts', action='store_true',
                        help='Recovery isolation only: disable all Add/Force entries; native automatic loading is unchanged')
    parser.add_argument('--loader', type=Path, help='Explicit source-built diagnostic RELEASE OpenCore (never a production package input)')
    args = parser.parse_args()
    if any(len(value)>128 or not re.fullmatch(r'-?[A-Za-z0-9_]+(?:=[A-Za-z0-9_.+-]+)?',value) for value in args.boot_arg):
        parser.error('Use bounded individual native boot tokens, not a shell command or whitespace')
    if args.output.exists():
        parser.error('Use a new output path; existing data is never replaced')
    data = bytearray(args.source.read_bytes())
    if data[512:520] != b'EFI PART':
        raise ValueError('Expected a GPT firmware disk')
    table, count, size = struct.unpack_from('<QII', data, 512 + 72)
    if count > 128 or size != 128:
        raise ValueError('Unsupported GPT geometry')
    matches = []
    for index in range(count):
        entry = table * 512 + index * size
        if data[entry:entry + 16] == uuid.UUID('c12a7328-f81f-11d2-ba4b-00a0c93ec93b').bytes_le:
            first, last = struct.unpack_from('<QQ', data, entry + 32)
            if first > last or (last + 1) * 512 > len(data):
                raise ValueError('Invalid ESP range')
            matches.append((first * 512, (last + 1) * 512))
    if len(matches) != 1:
        raise ValueError('Expected exactly one ESP')
    start, end = matches[0]
    volume = bytearray(data[start:end])
    fat_type = runpy.run_path(str(Path(__file__).resolve().parents[1] / 'scripts/efi-fat32.py'))['FAT32']
    fat = fat_type(volume)
    directory = 'EFI/VIM/'+args.profile if args.profile else 'EFI/OC'
    path = directory+'/config.plist'
    config = plistlib.loads(fat.read(path))
    debug = config['Misc']['Debug']
    # OpenCore Configuration.tex: 0x01 enable, 0x10 variable, 0x20 NV.
    # Its native 32-KiB log bound remains intact. No host NVRAM is touched.
    debug.update(Target=0x31, DisplayLevel=0xffffffff, AppleDebug=True,
                 SysReport=False, LogModules='*')
    config['Misc']['Security']['HaltLevel'] = 0
    if args.no_injected_kexts:
        for section in ('Add','Force'):
            for item in config['Kernel'].get(section,[]):item['Enabled']=False
    elif args.stock_display:
        # Use Apple's normal runtime dependency loading, not a hand-modified
        # cache. This is an isolation copy, never a production firmware input.
        disabled={'VMsvga2.kext','VMsvga2Accel.kext','VoodooHDA.kext'}
        found=set()
        for item in config['Kernel']['Add']:
            if item['BundlePath'] in disabled:
                item['Enabled']=False;found.add(item['BundlePath'])
        if found!=disabled:
            raise ValueError('Expected all three owned graphics/audio bundles')
        native={'com.apple.iokit.IOGraphicsFamily','com.apple.kext.OSvKernDSPLib','com.apple.iokit.IOAudioFamily'}
        for item in config['Kernel'].get('Force',[]):
            if item['Identifier'] in native:item['Enabled']=False
    if args.verbose or args.boot_arg or args.stock_display or args.no_injected_kexts:
        guid = '7C436110-AB2A-4BBB-A880-FE41995C9F82'
        values = config['NVRAM']['Add'][guid]
        flags = values.get('boot-args', '').split()
        if args.verbose and '-v' not in flags:
            flags.append('-v')
        for value in args.boot_arg:
            if value not in flags:
                flags.append(value)
        values['boot-args'] = ' '.join(flags)
        deleted = config['NVRAM'].setdefault('Delete', {}).setdefault(guid, [])
        if 'boot-args' not in deleted:
            deleted.append('boot-args')
    fat.write(path, plistlib.dumps(config, sort_keys=False), replace=True)
    if args.loader:
        loader = args.loader.read_bytes()
        if loader[:2] != b'MZ':
            raise ValueError('Expected a PE firmware application')
        fat.write(directory+'/OpenCore.efi', loader, replace=True)
    data[start:end] = volume
    with args.output.open('xb') as stream:
        stream.write(data)
    print(args.output)


if __name__ == '__main__':
    main()
