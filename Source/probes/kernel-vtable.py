#!/usr/bin/env python3
"""Read a thin, uncompressed kernel's named vtable (no runtime access)."""
import argparse
import struct
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('kernel')
p.add_argument('symbol')
p.add_argument('--slots', type=int, default=80)
a = p.parse_args()
data = Path(a.kernel).read_bytes()
assert struct.unpack_from('<I', data)[0] == 0xfeedfacf
segments = []
cursor = 32
for _ in range(struct.unpack_from('<I', data, 16)[0]):
    cmd, size = struct.unpack_from('<II', data, cursor)
    if cmd == 0x19:
        vm, length, offset, filelength = struct.unpack_from('<QQQQ', data, cursor+24)
        segments.append((vm, filelength, offset))
    if cmd == 2:
        symoff, count, stroff, strsize = struct.unpack_from('<IIII', data, cursor+8)
    cursor += size
symbols = {}
names = {}
for i in range(count):
    nameoffset, kind, section, desc, value = struct.unpack_from('<IBBHQ', data, symoff+16*i)
    name = data[stroff+nameoffset:data.index(0, stroff+nameoffset)].decode()
    if value:
        symbols[value] = name
        names[name] = value
address = names[a.symbol]+16  # skip offset-to-top and RTTI
vm, length, offset = next(s for s in segments if s[0] <= address < s[0]+s[1])
for slot in range(a.slots):
    value, = struct.unpack_from('<Q', data, offset+address-vm+slot*8)
    print(f'{slot*8:#05x} {value:#018x} {symbols.get(value, "?")}')
