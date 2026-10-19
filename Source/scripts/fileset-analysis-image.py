#!/usr/bin/env python3
"""Expose one Intel kernel-fileset entry to Mach-O disassemblers, read-only.

This is NOT a loadable kext extractor: it retains collection-relative offsets,
shared linkedit and unresolved inter-image references. Only a newly created
analysis copy's top-level header is replaced. Never use its output for booting.
"""
import argparse
from pathlib import Path
import struct

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('collection', type=Path)
parser.add_argument('identifier')
parser.add_argument('output', type=Path)
args = parser.parse_args()
data = args.collection.read_bytes()
if struct.unpack_from('<IIII', data) != (0xfeedfacf, 0x1000007, 3, 12):
    parser.error('expected an x86_64 kernel fileset')
offset = 32
matches = []
for _ in range(struct.unpack_from('<I', data, 16)[0]):
    command, length = struct.unpack_from('<II', data, offset)
    if length < 8 or offset + length > len(data):
        parser.error('invalid load command bounds')
    if command == 0x80000035:
        address, image, name_offset = struct.unpack_from('<QQI', data, offset + 8)
        if name_offset >= length:
            parser.error('invalid fileset name')
        name = data[offset + name_offset:offset + length].split(b'\0', 1)[0].decode()
        if name == args.identifier:
            matches.append((address, image))
    offset += length
if len(matches) != 1:
    parser.error('expected one matching entry')
address, image = matches[0]
if struct.unpack_from('<I', data, image)[0] != 0xfeedfacf:
    parser.error('entry is not a Mach-O header')
size = 32 + struct.unpack_from('<I', data, image + 20)[0]
if image + size > len(data) or size > image:
    parser.error('invalid entry header bounds')
output = bytearray(data)
output[:size] = data[image:image + size]
with args.output.open('xb') as stream:
    stream.write(output)
print(f'{args.identifier}: original header {image:#x}, VM {address:#x}; analysis only')
