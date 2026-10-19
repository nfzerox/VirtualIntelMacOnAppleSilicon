#!/usr/bin/env python3
"""Wrap an existing OpenCore FAT32 boot image in a GPT EFI System Partition.

Creates a new raw disk; never mounts or edits the source. EFI partition type is
deliberate: macOS should not automount it like the old optical OpenCore volume.
"""
import argparse
import struct
import uuid
import zlib
import runpy
import hashlib
import plistlib
from pathlib import Path


def crc(data):
    return zlib.crc32(data) & 0xffffffff


def create(source, destination, quiet=False, registration=None, signature_only_amfi=False, runtime_policy=None):
    data = bytearray(source.read_bytes())
    if len(data) < 512 or data[510:512] != b'\x55\xaa':
        raise ValueError('source is not a FAT boot image')
    sector = struct.unpack_from('<H', data, 11)[0]
    cluster = data[13]
    reserved = struct.unpack_from('<H', data, 14)[0]
    fats = data[16]
    fat_size = struct.unpack_from('<I', data, 36)[0]
    root = struct.unpack_from('<I', data, 44)[0]
    count = struct.unpack_from('<I', data, 32)[0]
    if sector != 512 or not cluster or cluster & (cluster-1) or not reserved or not fats or not fat_size or root < 2 or count*512 > len(data) or len(data)-count*512 > 4096 or any(data[count*512:]):
        raise ValueError('unsupported FAT32 geometry; refusing to guess')
    # hdiutil's raw master pads this FAT volume by two zero sectors.
    data = data[:count*512]
    if runtime_policy:
        if signature_only_amfi:
            raise ValueError('Boot policy is selected through fw_cfg, not a separate image')
        runtime_policy=Path(runtime_policy)
        receipt=plistlib.loads((runtime_policy/'BuildReceipt.plist').read_bytes())
        current=runpy.run_path(str(Path(__file__).with_name('build-boot-policy.py')))['inputs']()
        if (receipt.get('Configuration')!='RELEASE' or receipt.get('RuntimePolicyProtocol')!=1 or
                receipt.get('SourceFiles')!=current):
            raise ValueError('Boot policy components must match current source and RELEASE protocol')
        for name,expected in receipt['Hashes'].items():
            if hashlib.sha256((runtime_policy/name).read_bytes()).hexdigest()!=expected:
                raise ValueError('Boot policy component checksum mismatch: '+name)
        fat=runpy.run_path(str(Path(__file__).with_name('efi-fat32.py')))['FAT32'](data)
        fat.write('EFI/OC/Drivers/VIMBootPolicy.efi',(runtime_policy/'VIMBootPolicy.efi').read_bytes())
        config=plistlib.loads(fat.read('EFI/OC/config.plist'))
        config['UEFI']['Drivers'].append({'Arguments':'','Comment':'Virtual Intel Mac runtime boot policy',
            'Enabled':True,'LoadEarly':False,'Path':'VIMBootPolicy.efi'})
        fat.write('EFI/OC/config.plist',plistlib.dumps(config,sort_keys=False),replace=True)
        if registration:
            registration=runtime_policy/'PVGOpenGLProbe.kext'
    if registration:
        runpy.run_path(str(Path(__file__).with_name('bigsur-efi-registration.py')))['add_registration'](data,registration)
    if quiet or signature_only_amfi:
        runpy.run_path(str(Path(__file__).with_name('quiet-efi-config.py')))['quiet_config'](data,signature_only_amfi=signature_only_amfi)
    data_start = (reserved + fats*fat_size)*512
    offset = data_start + (root-2)*cluster*512
    label_found = False
    for entry in range(offset, min(offset+cluster*512, len(data)), 32):
        if data[entry] == 0:
            break
        if data[entry] != 0xe5 and data[entry+11] & 0x18 == 0x08 and data[entry+11] != 0x0f:
            data[entry:entry+11] = b'EFI        '
            label_found = True
            break
    if not label_found:
        raise ValueError('expected existing FAT volume label in first root cluster')
    first = 2048
    # Update BPB and its backup, leaving filesystem data and all EFI code intact.
    backup = struct.unpack_from('<H', data, 50)[0]
    for base in [0] + ([backup*512] if 0 < backup < reserved else []):
        struct.pack_into('<I', data, base+28, first)
        data[base+71:base+82] = b'EFI        '
    last = first+count-1
    total = last+34
    # Firmware is replaced on app updates. Stable GPT identities preserve UEFI
    # NVRAM device paths rather than making every rebuild look like a new disk.
    disk_guid = uuid.uuid5(uuid.NAMESPACE_URL, 'https://github.com/nfzerox/VirtualIntelMacOnAppleSilicon/firmware/disk').bytes_le
    entries = bytearray(128*128)
    struct.pack_into('<16s16sQQQ', entries, 0,
        uuid.UUID('c12a7328-f81f-11d2-ba4b-00a0c93ec93b').bytes_le,
        uuid.uuid5(uuid.NAMESPACE_URL, 'https://github.com/nfzerox/VirtualIntelMacOnAppleSilicon/firmware/esp').bytes_le, first, last, 0)
    entries[56:62] = 'EFI'.encode('utf-16le')

    def header(current, other, table):
        block = bytearray(512)
        struct.pack_into('<8sIIIIQQQQ16sQIII', block, 0, b'EFI PART', 0x10000,
            92, 0, 0, current, other, 34, total-34, disk_guid, table, 128, 128, crc(entries))
        struct.pack_into('<I', block, 16, crc(block[:92]))
        return block

    mbr = bytearray(512)
    struct.pack_into('<B3sB3sII', mbr, 446, 0, b'\x00\x02\x00', 0xee,
        b'\xff\xff\xff', 1, total-1)
    mbr[510:512] = b'\x55\xaa'
    with destination.open('xb') as output:
        output.truncate(total*512)
        output.write(mbr);output.write(header(1,total-1,2));output.write(entries)
        # Preserve every logical byte while leaving unused FAT space sparse.
        # Read-only firmware otherwise allocates ~64 MB three times per app.
        # QEMU sees ordinary zero-filled sectors; there is no runtime adapter.
        output.seek(first*512)
        for offset in range(0,len(data),65536):
            chunk=data[offset:offset+65536]
            if any(chunk):
                output.write(chunk)
            else:
                output.seek(len(chunk),1)
        output.seek((total-33)*512);output.write(entries)
        output.write(header(total-1,1,total-33))
    print(destination)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path)
    parser.add_argument('--quiet', action='store_true', help='hide OpenCore picker and guest verbose boot in output only')
    parser.add_argument('--bigsur-gl-registration',type=Path,help='prepared-guest-only GL registration kext')
    parser.add_argument('--signature-only-amfi',action='store_true',help='identity-preserving guest boot policy; requires verified guest Library Validation preference')
    parser.add_argument('--runtime-policy',type=Path,help='RELEASE policy driver that reads QEMU fw_cfg; no EFI variants')
    options = parser.parse_args()
    create(options.source, options.destination, options.quiet, options.bigsur_gl_registration, options.signature_only_amfi, options.runtime_policy)
