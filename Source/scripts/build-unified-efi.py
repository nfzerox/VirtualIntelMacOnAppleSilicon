#!/usr/bin/env python3
"""Combine OS profiles into one runtime-selected GPT/EFI image.

Build-time only. Never mounts images, changes an input, writes guest NVRAM or
merges kernel settings. Profiles keep independent ordinary FAT file chains.
"""
import argparse
from pathlib import Path
import plistlib
import runpy
import struct
import tempfile

PROFILES = ('tiger', 'leopard', 'snowleopard', 'lion', 'mountainlion', 'mavericks',
            'yosemite', 'elcapitan', 'sierra', 'highsierra', 'mojave', 'catalina', 'bigsur')
FAT32 = runpy.run_path(str(Path(__file__).with_name('efi-fat32.py')))['FAT32']
SNOW_SERIAL = Path(__file__).resolve().parents[1]/'prebuilt/firmware/VIMSerialStartup'
APPLE_BOOT_GUID = '7C436110-AB2A-4BBB-A880-FE41995C9F82'
EARLY_XHCI_PROFILES = ('lion', 'mountainlion', 'mavericks', 'yosemite')


def early_xhci_config(data):
    config = plistlib.loads(data)
    nvram = config['NVRAM']['Add'][APPLE_BOOT_GUID]
    arguments = nvram.get('boot-args', '').split()
    flags = 0
    for argument in arguments:
        if argument.startswith('usb='):
            flags |= int(argument.partition('=')[2], 0)
    # IOUSBFamily's kUSBEnableAllXHCIControllersMask admits QEMU's
    # non-Intel PCI vendor. kUSBUASControlMask keeps these guests on the
    # native BOT storage path; the driver does not recover from our
    # redirected BOT device refusing the UAS alternate.
    nvram['boot-args'] = ' '.join(
        [argument for argument in arguments if not argument.startswith('usb=')]
        + [f'usb={flags | 0x3000:#x}'])
    return plistlib.dumps(config, sort_keys=False)


def lion_namecache_config(data):
    config = plistlib.loads(data)
    # Lion 11G63 / xnu-1699.32.7 publishes nchashmask from hashinit before
    # taking the name-cache writer lock. A shared-lock lookup can index
    # the old table with the new mask during the installer's vnode resize.
    # Use the existing 8-byte local stack slot for hashinit's output. Once
    # locked, exchange the table pointer (retaining the old one in r13)
    # and publish the mask together. No change to lookups or rehashing.
    # Exact contiguous instructions, including RIP displacements, from
    # UUID FF3BB088-60A4-349C-92EA-CA649C698CE5, _resize_namecache+0x3b.
    config['Kernel']['Patch'].append(dict(
        Arch='x86_64', Base='_resize_namecache', Count=1, Enabled=True,
        Comment='Lion 11G63: publish name-cache table and mask under the writer lock',
        Find=bytes.fromhex(
            '488d15e6e65d00e8e9a426004885c0750ab80c000000e956010000'
            '4989c74c8b25c8e65d00488b3d71a65d00e8b49bfcff4c8b2dade65d00'
            '4c893da6e65d0049ffc4488b05ace65d004c8925a5e65d004885c00f84f0000000'),
        Replace=bytes.fromhex(
            '488d55d00f1f00e8e9a426004885c0750ab80c000000e956010000'
            '4989c74c8b65d0488b3d74a65d00e8b79bfcff4c873db0e65d004d89fd4c8925aee65d00'
            '49ffc4488b05ace65d004c8925a5e65d004885c00f84f0000000'),
        Identifier='kernel', Limit=0x94, Mask=b'', ReplaceMask=b'',
        MinKernel='11.4.2', MaxKernel='11.4.2', Skip=0))
    return plistlib.dumps(config, sort_keys=False)


def volume(image):
    data = Path(image).read_bytes()
    if data[512:520] != b'EFI PART':
        raise ValueError('Expected a generated GPT firmware image')
    first, last = struct.unpack_from('<QQ', data, 1056)
    if first != 2048 or last < first or (last+1)*512 > len(data):
        raise ValueError('Invalid generated ESP bounds')
    return bytearray(data[first*512:(last+1)*512])


def files(fat, directory, prefix=''):
    result = {}
    for name, _, cluster, size, attributes, _ in fat.entries(directory):
        if name in ('.', '..'):
            continue
        path = prefix+name
        if attributes & 16:
            result.update(files(fat, cluster, path+'/'))
        else:
            result[path] = (b''.join(fat.data[fat.offset(c):fat.offset(c)+fat.cluster]
                                     for c in fat.chain(cluster))[:size] if size else b'')
    return result


def build(firmware, selector, output):
    firmware, output = Path(firmware), Path(output)
    if output.exists():
        raise ValueError('Use a new unified firmware output path')
    selector = Path(selector).read_bytes()
    if selector[:2] != b'MZ':
        raise ValueError('Build the EFI selector first')
    profiles = {}
    for guest in PROFILES:
        fat = FAT32(volume(firmware/guest/'EFI.img'))
        # Also accept a previous shared build as an explicit source for a
        # selector-only iteration; it contains the same preserved profiles.
        parent = ['EFI', 'VIM', guest] if fat.find(fat.directory(['EFI']), 'VIM') else ['EFI', 'OC']
        profiles[guest] = files(fat, fat.directory(parent))
        if not {'OpenCore.efi', 'config.plist'}.issubset(profiles[guest]):
            raise ValueError('Incomplete firmware profile: '+guest)
    # Snow Leopard can publish both real PC UARTs as the same BSD device.
    # Keep the two-port protocol and the native driver; serialize only the
    # two UART startup calls. Other OS profiles are unchanged.
    serial_profile = runpy.run_path(str(Path(__file__).with_name(
        'build-snowleopard-serial-efi.py')))['serial_profile_entries']
    profiles['snowleopard'].update(serial_profile(
        profiles['snowleopard']['config.plist'], SNOW_SERIAL.read_bytes()))
    for guest in EARLY_XHCI_PROFILES:
        profiles[guest]['config.plist'] = early_xhci_config(profiles[guest]['config.plist'])
    profiles['lion']['config.plist'] = lion_namecache_config(profiles['lion']['config.plist'])
    # Retain the already-qualified BPB, geometry, boot sector and backup boot
    # sector. Empty its ordinary FAT/data allocation; never alias file chains.
    data = volume(firmware/'bigsur/EFI.img')
    old = FAT32(data)
    reserved = bytes(data[old.fat:old.fat+8])
    data[old.start:] = bytes(len(data)-old.start)
    for mirror in range(old.fats):
        start = old.fat+mirror*old.fat_size
        data[start:start+old.fat_size] = bytes(old.fat_size)
        data[start:start+8] = reserved
    old.link(old.root, 0x0fffffff)
    root = old.offset(old.root)
    data[root:root+11] = b'EFI        '
    data[root+11] = 8
    fat = FAT32(data)
    for guest, entries in profiles.items():
        for path, content in sorted(entries.items()):
            fat.write('EFI/VIM/'+guest+'/'+path, content)
    fat.write('EFI/BOOT/BOOTx64.efi', selector)
    # Saved OVMF boot entries may still point at the old common launcher path.
    fat.write('EFI/OC/OpenCore.efi', selector)
    # The shared launcher is firmware, not an OS boot candidate. OpenCore's
    # native ReadEntryVisibility explicitly honors this volume-root marker.
    # Without it a fresh VARS store can select our launcher as "EFI", recurse
    # into OpenCore and fall back to a picker instead of booting Base System.
    # Existing installed guests can mask this bug with a saved startup disk.
    fat.write('.contentVisibility', b'Disabled\n')
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='vim-unified-efi-') as temporary:
        boot = Path(temporary)/'BOOT.img'
        boot.write_bytes(data)
        runpy.run_path(str(Path(__file__).with_name('make-efi-disk.py')))['create'](boot, output)
    actual = FAT32(volume(output))
    for guest, expected in profiles.items():
        observed = files(actual, actual.directory(['EFI', 'VIM', guest]))
        if observed != expected:
            raise ValueError('Relocating firmware changed profile contents: '+guest)
    if actual.read('EFI/BOOT/BOOTx64.efi') != selector or actual.read('EFI/OC/OpenCore.efi') != selector:
        raise ValueError('The common EFI boot paths do not contain the selector')
    if actual.read('.contentVisibility') != b'Disabled\n':
        raise ValueError('The firmware volume must not be offered as a guest OS')
    return {'FormatVersion': 1, 'RuntimeProfileSelection': True, 'Profiles': list(PROFILES),
            'ProfileBytes': sum(len(data) for entry in profiles.values() for data in entry.values()),
            'ImageBytes': output.stat().st_size}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', required=True, type=Path)
    parser.add_argument('--selector', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    print(build(args.firmware, args.selector, args.output))
