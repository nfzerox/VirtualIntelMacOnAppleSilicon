#!/usr/bin/env python3
"""Compile native firmware build tools with the app's persistent identity.

Only linked Mach-O executables/libraries are signed, never C objects, archives
or the guest's PE firmware. No entitlements or host security changes are used.
"""
import os
from pathlib import Path
import struct
import subprocess
import sys

def main():
    if len(sys.argv)<3 or sys.argv[1] not in ('clang','clang++'):
        raise SystemExit('Expected clang or clang++ and compiler arguments')
    args=sys.argv[2:]
    subprocess.run(['/usr/bin/xcrun','--sdk','macosx',sys.argv[1],*args],check=True)
    if any(mode in args for mode in ('-c','-E','-S','-fsyntax-only')):return
    output=Path('a.out')
    for index,arg in enumerate(args):
        if arg=='-o' and index+1<len(args):output=Path(args[index+1])
        elif arg.startswith('-o') and len(arg)>2:output=Path(arg[2:])
    if not output.is_file():return
    with output.open('rb') as stream:header=stream.read(16)
    if len(header)<16:return
    magic=header[:4]
    if magic in (b'\xcf\xfa\xed\xfe',b'\xce\xfa\xed\xfe'):
        if struct.unpack_from('<I',header,12)[0] not in (2,6,8):return
    elif magic not in (b'\xca\xfe\xba\xbe',b'\xca\xfe\xba\xbf'):return
    identity=os.environ.get('VIM_FIRMWARE_TOOL_IDENTITY')
    if not identity or identity=='-':raise SystemExit('A persistent native-tool signing identity is required')
    subprocess.run(['/usr/bin/codesign','--force','--sign',identity,str(output)],check=True)
    subprocess.run(['/usr/bin/codesign','--verify','--strict',str(output)],check=True)

if __name__=='__main__':main()
