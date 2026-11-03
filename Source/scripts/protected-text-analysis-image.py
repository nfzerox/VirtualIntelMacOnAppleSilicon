#!/usr/bin/env python3
"""Make a non-executable Mach-O analysis copy using an explicit live TEXT dump.

For old Apple SG_PROTECTED_VERSION_1 executables only. The debugger must have
read the matching process's complete __TEXT segment at its observed load
address and detached. This tool does not attach, bypass access checks or
modify/install a system binary. Output retains the original linkedit/data;
it is for disassembly, not signing, execution or redistribution.
"""
import argparse
from pathlib import Path
import struct

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('original', type=Path)
p.add_argument('text_dump', type=Path)
p.add_argument('output', type=Path)
a = p.parse_args()
disk, live = a.original.read_bytes(), a.text_dump.read_bytes()
if len(disk) < 32 or len(live) < 32:
    p.error('Missing Mach-O header')
if struct.unpack_from('<II', disk) != (0xfeedfacf, 0x1000007):
    p.error('Expected a thin x86_64 Mach-O')
ncmds, size = struct.unpack_from('<II', disk, 16)
end = 32 + size
if end > min(len(disk), len(live)) or disk[:end] != live[:end]:
    p.error('Live and file headers/load commands must match exactly, including UUID')
at, matches, uuid = 32, [], None
for _ in range(ncmds):
    if at + 8 > end:
        p.error('Truncated load command')
    command, length = struct.unpack_from('<II', disk, at)
    if length < 8 or at + length > end:
        p.error('Invalid load command bounds')
    if command == 0x1b and length == 24:
        uuid = disk[at + 8:at + 24].hex()
    if command == 0x19 and length >= 72 and disk[at + 8:at + 24].split(b'\0')[0] == b'__TEXT':
        address, virtual, offset, count = struct.unpack_from('<QQQQ', disk, at + 24)
        flags = struct.unpack_from('<I', disk, at + 68)[0]
        if offset or not flags & 8 or count > virtual or count > len(disk) or len(live) != virtual:
            p.error('Expected a complete protected, header-containing TEXT dump')
        matches.append((at, offset, count, flags, address))
    at += length
if at != end or len(matches) != 1 or not uuid:
    p.error('Expected one protected TEXT segment and a UUID')
at, offset, count, flags, address = matches[0]
output = bytearray(disk)
output[offset:offset + count] = live[:count]
struct.pack_into('<I', output, at + 68, flags & ~8)
with a.output.open('xb') as f:
    f.write(output)
a.output.chmod(0o644)
print(f'Analysis only: UUID {uuid}, preferred TEXT {address:#x}, {count} live bytes; original unchanged')
