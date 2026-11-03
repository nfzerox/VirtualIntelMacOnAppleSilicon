#!/usr/bin/env python3
"""Generate an explicit, disposable cache-selection experiment from an EFI.

Never edits the app, source image, VM disk or NVRAM. Use --guest-efi with the
real app; this is not a production firmware/package recipe.
"""
import argparse
from pathlib import Path
import plistlib
import runpy
import struct

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('source',type=Path)
p.add_argument('output',type=Path)
p.add_argument('--cache',choices=('Auto','Cacheless','Mkext'),required=True)
p.add_argument('--rebuild-apple-memory-map',action='store_true')
p.add_argument('--native-drivers-only',action='store_true',help='Isolate the installed OS without added/forced extensions')
p.add_argument('--disable-audio',action='store_true',help='Isolate graphics boot without the added VoodooHDA driver')
p.add_argument('--leopard-lock-budget',action='store_true',help='Test a bounded one-second Darwin9 spinlock budget, not disabled watchdogs')
p.add_argument('--verbose',action='store_true',help='Show only the native kernel log; keep quiet firmware')
p.add_argument('--single-user',action='store_true',help='Explicit native installed-system console diagnosis; defer app setup separately')
p.add_argument('--show-picker',action='store_true',help='Expose the loader only for explicit recovery selection')
p.add_argument('--picker-timeout',type=int,default=2,choices=range(1,31),metavar='1..30',help='Bounded diagnostic selection time; never an unlimited wait')
a=p.parse_args()
if a.output.exists():p.error('Choose a new disposable output file')
fat_class=runpy.run_path(str(Path(__file__).resolve().parents[1]/'scripts/efi-fat32.py'))['FAT32']
data=bytearray(a.source.read_bytes())
if data[512:520]!=b'EFI PART':p.error('Choose the app-owned GPT EFI disk')
entry_lba=struct.unpack_from('<Q',data,512+72)[0]
entry=entry_lba*512
if data[entry:entry+16]!=bytes.fromhex('28732ac11ff8d211ba4b00a0c93ec93b'):p.error('First partition is not an EFI System Partition')
first,last=struct.unpack_from('<QQ',data,entry+32)
start,end=first*512,(last+1)*512
if not 1024<=start<end<=len(data):p.error('Invalid EFI partition bounds')
volume=data[start:end];fat=fat_class(volume)
config=plistlib.loads(fat.read('EFI/OC/config.plist'))
config['Kernel']['Scheme']['KernelCache']=a.cache
if a.rebuild_apple_memory_map:config['Booter']['Quirks']['RebuildAppleMemoryMap']=True
if a.native_drivers_only:
    for kind in ('Add','Force'):
        for extension in config['Kernel'].get(kind,[]):extension['Enabled']=False
if a.disable_audio:
    for extension in config['Kernel']['Add']:
        if extension['BundlePath']=='VoodooHDA.kext':extension['Enabled']=False
if a.leopard_lock_budget:
    # Exact 9J3050 ml_init_lock_timeout sets 250,000,000 ns unconditionally;
    # unlike Darwin10 it has no slto_us argument. Symbol-local, byte-guarded
    # one-second experiment; no disk/kernel edit or fixed function address.
    config['Kernel']['Patch'].append(dict(Arch='i386',Base='_ml_init_lock_timeout',
        Comment='Diagnostic Darwin9 bounded one-second virtual scheduling budget',
        Count=1,Enabled=True,Find=bytes.fromhex('c7042480b2e60e'),
        Identifier='kernel',Limit=128,Mask=b'',MaxKernel='9.99.99',MinKernel='9.0.0',
        Replace=bytes.fromhex('c7042400ca9a3b'),ReplaceMask=b'',Skip=0))
if a.show_picker:
    # OpenCore interprets zero as an unlimited interactive wait, not an
    # immediate default boot. Even explicit diagnostic menus must progress.
    config['Misc']['Boot'].update(ShowPicker=True,Timeout=a.picker_timeout,PickerMode='Builtin',HideAuxiliary=False)
if a.verbose or a.single_user:
    guid='7C436110-AB2A-4BBB-A880-FE41995C9F82'
    variables=config['NVRAM']['Add'].setdefault(guid,{})
    arguments=variables.get('boot-args','').split()
    if a.verbose and '-v' not in arguments:arguments.append('-v')
    if a.single_user and '-s' not in arguments:arguments.append('-s')
    variables['boot-args']=' '.join(arguments)
    removed=config['NVRAM'].setdefault('Delete',{}).setdefault(guid,[])
    if 'boot-args' not in removed:removed.append('boot-args')
fat.write('EFI/OC/config.plist',plistlib.dumps(config,sort_keys=False),replace=True)
data[start:end]=volume
with a.output.open('xb') as output:output.write(data)
print(a.output)
