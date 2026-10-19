"""Edit only OpenCore's config inside an in-memory FAT32 image; no mounting.

Grows the generated config's FAT chain when needed. Never alters EFI executable code.
"""
import plistlib
from pathlib import Path
import runpy
import struct


def quiet_config(data, configure=True, signature_only_amfi=False):
    sector = struct.unpack_from('<H', data, 11)[0]
    cluster_size = data[13]*sector
    reserved = struct.unpack_from('<H', data, 14)[0]
    fat_size = struct.unpack_from('<I', data, 36)[0]*sector
    fat = reserved*sector
    start = fat+data[16]*fat_size

    def chain(first):
        seen = set()
        while first < 0x0ffffff8:
            if first < 2 or first in seen or first*4+4 > fat_size:
                raise ValueError('invalid FAT chain')
            seen.add(first)
            offset = start+(first-2)*cluster_size
            if offset+cluster_size > len(data):
                raise ValueError('FAT cluster outside image')
            yield offset
            first = struct.unpack_from('<I', data, fat+first*4)[0] & 0x0fffffff

    def find(directory, wanted):
        matches = []
        long_name = {}
        for block in chain(directory):
            for offset in range(block, block+cluster_size, 32):
                item = data[offset:offset+32]
                if item[0] == 0:
                    break
                if item[0] == 0xe5:
                    long_name = {}; continue
                if item[11] == 15:
                    long_name[item[0] & 31] = bytes(item[1:11]+item[14:26]+item[28:32]).decode('utf-16le')
                    continue
                if item[11] & 8:  # A volume label is not the EFI directory.
                    long_name = {}; continue
                name = ''.join(long_name[k] for k in sorted(long_name)).split('\0')[0].rstrip('\uffff')
                if not name:
                    name = bytes(item[:8]).decode('ascii').rstrip()
                    extension = bytes(item[8:11]).decode('ascii').rstrip()
                    if extension: name += '.'+extension
                long_name = {}
                if name.lower() == wanted.lower():
                    first = (struct.unpack_from('<H', item, 20)[0]<<16)|struct.unpack_from('<H', item, 26)[0]
                    matches.append((offset, first, struct.unpack_from('<I', item, 28)[0], item[11]))
        if len(matches) != 1:
            raise ValueError('expected unique EFI path component: '+wanted)
        return matches[0]

    directory = struct.unpack_from('<I', data, 44)[0]
    for name in ('EFI', 'OC'):
        _, directory, _, attributes = find(directory, name)
        if not attributes & 16: raise ValueError('expected EFI directory')
    entry, first, size, attributes = find(directory, 'config.plist')
    if attributes & 24: raise ValueError('expected regular OpenCore config')
    blocks = list(chain(first))
    original = b''.join(data[p:p+cluster_size] for p in blocks)[:size]
    config = plistlib.loads(original)
    if not configure:
        return config
    guid = '7C436110-AB2A-4BBB-A880-FE41995C9F82'
    nvram = config['NVRAM']['Add'][guid]
    flags = nvram.get('boot-args', '').split()
    if signature_only_amfi:
        # Selected only after the guest compatibility preference is verified.
        # Keep AMFI's normal platform classification/library validation instead
        # of claiming every third-party executable is an Apple platform binary.
        flags = [f for f in flags if not f.startswith(('amfi_get_out_of_my_way=', 'amfi=', 'amfi_allow_any_signature='))]
        flags.append('amfi_allow_any_signature=1')
    nvram['boot-args'] = ' '.join(f for f in flags if f != '-v' and not f.startswith(('debug=', 'serial=', 'keepsyms=')))
    # Existing template VARS must not restore the earlier diagnostic arguments.
    delete = config['NVRAM'].setdefault('Delete', {}).setdefault(guid, [])
    if 'boot-args' not in delete: delete.append('boot-args')
    # Add alone does not replace a value written by csrutil inside the guest.
    # Reassert this appliance's shipped guest-only SIP policy on every boot,
    # just as for boot-args. Never reset the whole VARS store/boot target.
    if 'csr-active-config' not in delete: delete.append('csr-active-config')
    config['Misc']['Boot']['ShowPicker'] = False
    # ShowPicker=False skips the menu on normal boots. Zero does NOT mean
    # immediate boot when a fallback picker appears: it disables its timeout.
    config['Misc']['Boot']['Timeout'] = 3
    config['Misc']['Boot']['PollAppleHotKeys'] = False
    config['Misc']['Debug']['Target'] = 0
    config['Misc']['Debug']['DisplayLevel'] = 0
    # Firmware/software-rendered boot uses a comfortable 1x mode. PVG advertises
    # and selects its independent HiDPI modes once the guest driver starts.
    config['UEFI']['Output']['Resolution'] = '1280x800'
    config['UEFI']['Output']['UIScale'] = 1
    # AppleBochVGAFB::start does not preserve the GOP/EDID mode: it walks its
    # six-entry mode table, then programs the last VRAM-fitting entry. The
    # stock last entry is 1920x1440, making a 1x installer tiny in a smaller
    # Retina-host window. Replace only that entry's geometry at EFI load time.
    # Match the complete table, not a private address or a bare resolution
    # constant. Cirrus has a separate two-entry table and is not matched.
    # No std VGA PCI device exists on accelerated boots, so PVG is unaffected.
    modes = ((640,480),(800,600),(1024,768),(1280,1024),(1600,1200),(1920,1440))
    original_modes = b''.join(struct.pack('<IIII', w, h, 85, 32) for w,h in modes)
    scaled_modes = original_modes[:-16] + struct.pack('<IIII',1280,800,85,32)
    comment = 'Virtual Intel Mac software display 1280x800'
    patches = config['Kernel'].setdefault('Patch', [])
    patches[:] = [patch for patch in patches if patch.get('Comment') != comment]
    patches.append({'Arch':'x86_64','Base':'','Comment':comment,'Count':1,
        'Enabled':True,'Find':original_modes,'Identifier':'com.apple.driver.AppleVirtualGraphics',
        'Limit':0,'Mask':b'','MaxKernel':'20.99.99','MinKernel':'18.0.0',
        'Replace':scaled_modes,'ReplaceMask':b'','Skip':0})
    updated = plistlib.dumps(config, sort_keys=False)
    if len(updated) > len(blocks)*cluster_size:
        # Runtime driver/patch configuration can cross a cluster boundary.
        # Reuse the checked editor to extend only this generated file, updating
        # every FAT mirror and FSInfo hint rather than spilling into its neighbor.
        runpy.run_path(str(Path(__file__).with_name('efi-fat32.py')))['FAT32'](data).write(
            'EFI/OC/config.plist', updated, replace=True)
        return config
    padded = updated.ljust(len(blocks)*cluster_size, b'\0')
    for index, offset in enumerate(blocks):
        data[offset:offset+cluster_size] = padded[index*cluster_size:(index+1)*cluster_size]
    struct.pack_into('<I', data, entry+28, len(updated))
    return config
