#!/usr/bin/env python3
"""Read a bounded, symbol-identified guest panic buffer through the app owner.

Usage: PROGRAM DIAGNOSTICS PHYSICAL_ADDRESS [BYTES<=16384]
Requires actual matching kernel symbols; never scans arbitrary guest memory.
"""
import json
from pathlib import Path
import re
import subprocess
import sys

if len(sys.argv) not in (3,4):
    raise SystemExit(__doc__)
start=int(sys.argv[2],0)
size=int(sys.argv[3],0) if len(sys.argv)==4 else 4096
if start<0 or not 1<=size<=16384:
    raise SystemExit('Choose a symbol-derived address and at most 16 KiB.')
owner=Path(__file__).resolve().parents[1]/'scripts/app-guest-command.py'
data=bytearray()
for offset in range(0,size,512):
    count=min(64,(size-offset+7)//8)
    text=subprocess.check_output([sys.executable,str(owner),sys.argv[1],
        '--physical-memory',hex(start+offset),str(count)],text=True)
    report=json.loads(text)
    if 'Error' in report:raise SystemExit(report['Error'])
    block=bytearray()
    for line in report.get('Report','').splitlines():
        if ':' not in line:continue
        for value in re.findall(r'0x([0-9a-fA-F]{16})',line.split(':',1)[1]):
            block.extend(int(value,16).to_bytes(8,'little'))
    if len(block)!=count*8:raise SystemExit('Incomplete guest memory read.')
    data.extend(block)
    if b'\0' in block:break
print(bytes(data[:size]).split(b'\0',1)[0].decode('utf-8',errors='replace'))
