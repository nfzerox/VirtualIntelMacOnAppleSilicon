#!/usr/bin/env python3
"""Build a separate bounded Darwin9 lock-deadline research EFI, never edit input.

Research only: real panic traces must qualify any eventual production policy.
This changes 250ms to 1s, not lock ownership, TLB acknowledgements or panic code.
The original Apple kernel on disk remains unchanged. Use the real app's
--guest-efi argument and verify actual LockTimeOut/LockTimeOutTSC after boot.
"""
import argparse
import hashlib
from pathlib import Path
import plistlib
import runpy
import struct
import subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--input',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--kernel',type=Path,required=True)
a=p.parse_args()
if a.output.exists():p.error('Use a new output; no existing EFI is overwritten')
kernel=a.kernel.read_bytes()
qualified={
    '905f1d5e3e12f34f3e1c1a8d7c587ca066495841071c1b7f6f03f815e439ee19',
    '05fd5bf7ad410473d3590e4748ce228bff1297b6b3d182eb20b4c9a3e6adf766',
}
before=bytes.fromhex('c7042480b2e60ec744240400000000')
after=bytes.fromhex('c7042400ca9a3bc744240400000000')
if hashlib.sha256(kernel).hexdigest() not in qualified:
    p.error('Unqualified actual native kernel/instruction guard')
symbols=subprocess.check_output(['nm','-n',str(a.kernel)],text=True).splitlines()
matches=[int(row.split()[0],16) for row in symbols if row.split()[-1:] == ['_ml_init_lock_timeout']]
if len(matches)!=1 or kernel[:4]!=bytes.fromhex('cefaedfe'):p.error('Missing actual i386 symbol')
offset=28;regions=[]
for _ in range(struct.unpack_from('<I',kernel,16)[0]):
    command,size=struct.unpack_from('<II',kernel,offset)
    if size<8 or offset+size>len(kernel):p.error('Invalid Mach-O load command')
    if command==1:
        address,_,file_offset,file_size=struct.unpack_from('<IIII',kernel,offset+24)
        if address<=matches[0]<address+file_size:regions.append(file_offset+matches[0]-address)
    offset+=size
if len(regions)!=1 or kernel[regions[0]:regions[0]+48].count(before)!=1:
    p.error('Actual symbol-local instruction guard did not match')
disk=bytearray(a.input.read_bytes())
if disk[512:520]!=b'EFI PART':p.error('Expected a GPT EFI image')
table=struct.unpack_from('<Q',disk,512+72)[0]*512
first,last=struct.unpack_from('<QQ',disk,table+32)
if not 2048<=first<=last or (last+1)*512>len(disk):p.error('Invalid EFI partition bounds')
data=bytearray(disk[first*512:(last+1)*512])
fat=runpy.run_path(str(Path(__file__).resolve().parents[1]/'scripts/efi-fat32.py'))['FAT32'](data)
config=plistlib.loads(fat.read('EFI/OC/config.plist'))
if config['Kernel']['Scheme']['KernelArch']!='i386':p.error('Not the actual Leopard kernel profile')
if any(x.get('Base')=='_ml_init_lock_timeout' and x.get('Enabled') for x in config['Kernel']['Patch']):
    p.error('Deadline experiment already present')
config['Kernel']['Patch'].append(dict(Arch='i386',Base='_ml_init_lock_timeout',
    Comment='Research bounded 1s virtual CPU lock deadline',Count=1,Enabled=True,
    Find=before,Replace=after,Identifier='kernel',Limit=48,Mask=b'',ReplaceMask=b'',
    MinKernel='9.0.0',MaxKernel='9.99.99',Skip=0))
fat.write('EFI/OC/config.plist',plistlib.dumps(config,sort_keys=False),replace=True)
disk[first*512:(last+1)*512]=data
with a.output.open('xb') as stream:stream.write(disk)
print(a.output)
