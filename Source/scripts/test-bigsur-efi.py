#!/usr/bin/env python3
"""Inspect a generated prepared-guest EFI without mounting or booting a VM."""
from pathlib import Path
import plistlib
import runpy
import sys

scripts=Path(__file__).resolve().parent
FAT32=runpy.run_path(str(scripts/'efi-fat32.py'))['FAT32']
source,kext=Path(sys.argv[1]),Path(sys.argv[2])
original=source.read_bytes();data=bytearray(original);before=FAT32(data)
def files(fat,directory=None,prefix=''):
    found={}
    for name,_,cluster,_,attributes,_ in fat.entries(directory or fat.root):
        if name in ('.','..'):continue
        path=prefix+name
        if attributes&16:found.update(files(fat,cluster,path+'/'))
        else:found[path]=fat.read(path)
    return found
previous=files(before);config=plistlib.loads(previous['EFI/OC/config.plist'])
assert not any(e['BundlePath']=='PVGOpenGLProbe.kext' for e in config['Kernel']['Add'])
runpy.run_path(str(scripts/'bigsur-efi-registration.py'))['add_registration'](data,kext)
after=FAT32(data);new=files(after)
assert all(new[path]==content for path,content in previous.items() if path!='EFI/OC/config.plist')
updated=plistlib.loads(new['EFI/OC/config.plist'])
entry=updated['Kernel']['Add'].pop()
assert updated==config
assert entry['Enabled'] and entry['MinKernel']=='20.0.0' and entry['MaxKernel']=='20.99.99'
for file in kext.rglob('*'):
    if file.is_file():assert after.read('EFI/OC/Kexts/PVGOpenGLProbe.kext/'+file.relative_to(kext).as_posix())==file.read_bytes()
for mirror in range(1,after.fats):
    assert data[after.fat:after.fat+after.fat_size]==data[after.fat+mirror*after.fat_size:after.fat+(mirror+1)*after.fat_size]
# Exercise growth/replacement and multiple directory clusters in memory only.
for index in range(40):after.write(f'Fixture/Long Directory Name/Long File Name {index}.bin',bytes([index])*700)
after.write('Fixture/Long Directory Name/Long File Name 0.bin',b'expanded'*3000,replace=True)
check=FAT32(data)
assert check.read('Fixture/Long Directory Name/Long File Name 0.bin')==b'expanded'*3000
assert all(check.read(f'Fixture/Long Directory Name/Long File Name {i}.bin')==bytes([i])*700 for i in range(1,40))
assert source.read_bytes()==original
print('PASS: deferred registration, exact kext readback, original EFI programs unchanged, mirrored FAT/LFN/directory growth')
