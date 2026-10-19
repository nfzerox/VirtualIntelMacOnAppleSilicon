#!/usr/bin/env python3
"""Create an opt-in OpenCore serial-debug disk from packaged immutable EFI.

No mounts, source writes, NVRAM edits or production build changes. Uses the
already vendored matching OpenCore 1.0.5 DEBUG loader, not another git checkout.
Run only against a stopped VM's immutable firmware, never its variable store.
The output is a new diagnostic artifact; restore ordinary firmware afterward.
"""
import argparse
from pathlib import Path
import plistlib
import runpy
import struct
import uuid
import zlib


def create(source, destination, debug_efi, keep_release=False, guest_boot_args=()):
    data = bytearray(source.read_bytes())
    if data[512:520] != b'EFI PART':
        raise ValueError('expected generated GPT EFI disk, not NVRAM or a FAT volume')
    header_size = struct.unpack_from('<I', data, 524)[0]
    if not 92 <= header_size <= 512:
        raise ValueError('invalid GPT header size')
    header = bytearray(data[512:512+header_size])
    checksum = struct.unpack_from('<I', header, 16)[0]
    struct.pack_into('<I', header, 16, 0)
    if zlib.crc32(header) & 0xffffffff != checksum:
        raise ValueError('invalid GPT header checksum')
    table_lba, count, entry_size, table_crc = struct.unpack_from('<QIII', data, 584)
    if not 1 <= count <= 128 or entry_size != 128:
        raise ValueError('unsupported GPT table')
    entries = data[table_lba*512:table_lba*512+count*entry_size]
    if len(entries) != count*entry_size or zlib.crc32(entries) & 0xffffffff != table_crc:
        raise ValueError('invalid GPT table checksum')
    esp_type = uuid.UUID('c12a7328-f81f-11d2-ba4b-00a0c93ec93b').bytes_le
    esps = [struct.unpack_from('<QQ', entries, i+32) for i in range(0, len(entries), entry_size)
            if entries[i:i+16] == esp_type]
    if len(esps) != 1:
        raise ValueError('expected exactly one EFI system partition')
    first, last = esps[0]
    if not 2 <= first <= last < len(data)//512-1:
        raise ValueError('EFI partition outside disk')
    original = data[first*512:(last+1)*512]
    fat_type = runpy.run_path(str(Path(__file__).with_name('efi-fat32.py')))['FAT32']
    fat = fat_type(bytearray(original))
    release = fat.read('EFI/OC/OpenCore.efi')
    debug = (debug_efi/'OC/OpenCore.efi').read_bytes()
    # Do not silently mix a newer DEBUG binary with another config/driver ABI.
    counterpart = debug_efi.parent.parent/'EFI_RELEASE/EFI/OC/OpenCore.efi'
    if release != counterpart.read_bytes() or b'DBG-XXX-YYYY-MM-DD' not in debug:
        raise ValueError('expected the matching vendored RELEASE/DEBUG pair')
    config = plistlib.loads(fat.read('EFI/OC/config.plist'))
    if guest_boot_args:
        # Isolated guest-driver logging, keeping the production RELEASE loader,
        # quiet firmware and privacy policy. Never edits the VM's NVRAM/source.
        guid = '7C436110-AB2A-4BBB-A880-FE41995C9F82'
        flags = config['NVRAM']['Add'][guid].get('boot-args', '').split()
        for flag in guest_boot_args:
            if not flag or any(c.isspace() for c in flag) or '=' not in flag:
                raise ValueError('each guest boot argument must be one key=value token')
            key = flag.split('=', 1)[0]
            flags = [f for f in flags if f.split('=', 1)[0] != key]
            flags.append(flag)
        config['NVRAM']['Add'][guid]['boot-args'] = ' '.join(flags)
        keep_release = True
    else:
        config['Misc']['Debug'].update(Target=0x09,
                                      DisplayLevel=0x80000002 if keep_release else 0x80000042,
                                      LogModules='*')
        config['Misc']['Serial']['Init'] = True
    # Preserve picker/default-boot settings: changing them would hide the race.
    fat.write('EFI/OC/config.plist', plistlib.dumps(config, sort_keys=False), replace=True)
    if not keep_release:
        fat.write('EFI/OC/OpenCore.efi', debug, replace=True)
        fat.write('EFI/BOOT/BOOTx64.efi', (debug_efi/'BOOT/BOOTx64.efi').read_bytes(), replace=True)
    data[first*512:(last+1)*512] = fat.data
    with destination.open('xb') as output:
        output.write(data)
    print(destination)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path)
    parser.add_argument('--debug-efi', type=Path, default=Path(__file__).resolve().parents[1]/
                        'vendor/source/OpenCore-ISO/EFI_DEBUG/EFI')
    parser.add_argument('--keep-release-loader', action='store_true',
                        help='retain RELEASE binaries; log warnings/errors only to reduce timing changes')
    parser.add_argument('--guest-boot-arg', action='append', default=[],
                        help='isolated guest key=value argument; retain all quiet production EFI binaries/settings')
    args = parser.parse_args()
    create(args.source, args.destination, args.debug_efi, args.keep_release_loader, args.guest_boot_arg)
