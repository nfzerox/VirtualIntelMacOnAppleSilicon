#!/usr/bin/env python3
"""Make a NEW GPT EFI diagnostic copy with volatile OpenCore logging only.

Use the real app's --guest-efi option. No console, serial, on-disk log, NVRAM
nonvolatile logging, source-image modification or guest system patch. Read
the guest's OpenCore boot-log variable after boot, then boot normally again.
"""
import argparse
from pathlib import Path
import plistlib
import runpy
import struct


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('source',type=Path)
    p.add_argument('output',type=Path)
    a=p.parse_args()
    data=bytearray(a.source.read_bytes())
    if a.output.exists() or len(data)<1024 or data[512:520]!=b'EFI PART':p.error('Use a GPT source and a new output path')
    table=struct.unpack_from('<Q',data,512+72)[0]*512
    first,last=struct.unpack_from('<QQ',data,table+32)
    if first<34 or last<first or (last+1)*512>len(data):raise ValueError('Invalid firmware partition')
    FAT32=runpy.run_path(str(Path(__file__).with_name('efi-fat32.py')))['FAT32']
    volume=bytearray(data[first*512:(last+1)*512]);fat=FAT32(volume)
    config=plistlib.loads(fat.read('EFI/OC/config.plist'))
    config['Misc']['Debug']['Target']=0x11 # enable + volatile variable, NOT console/nonvolatile
    config['Misc']['Debug']['DisplayLevel']=0x80000042
    fat.write('EFI/OC/config.plist',plistlib.dumps(config,sort_keys=False),replace=True)
    data[first*512:(last+1)*512]=volume
    with a.output.open('xb') as f:f.write(data)
    print(a.output)


if __name__=='__main__':main()
