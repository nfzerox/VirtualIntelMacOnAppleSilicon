#!/usr/bin/env python3
"""Read the exact kernel method/UUID behind the private Mavericks lab profile.

No extraction, patching, output files or third-party parser dependency. The
runtime header is deliberately reviewed against actual disassembly, not blindly
regenerated from any kernel with the same symbol name.
"""
import argparse
import json
from pathlib import Path
import struct
import uuid

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('kernel', type=Path)
parser.add_argument('--symbol', default='__ZN11IOMemoryMap8redirectEP18IOMemoryDescriptorjy',
                    help='exact Mach-O text symbol to inspect (default: IOMemoryMap::redirect)')
args = parser.parse_args()
raw = args.kernel.read_bytes()
if len(raw) < 32 or struct.unpack_from('<I', raw)[0] != 0xfeedfacf:
    parser.error('requires an uncompressed thin little-endian 64-bit Mach-O kernel')
ncmds, cmdbytes = struct.unpack_from('<II', raw, 16)
limit = 32 + cmdbytes
if limit > len(raw) or ncmds > cmdbytes // 8:
    parser.error('invalid load commands')
segments, symbols, kernel_uuid = [], None, None
at = 32
for _ in range(ncmds):
    command, size = struct.unpack_from('<II', raw, at)
    if size < 8 or size > limit - at:
        parser.error('invalid command size')
    if command == 0x19 and size >= 72:
        name, address, vmsize, offset, length = struct.unpack_from('<16sQQQQ', raw, at + 8)
        segments.append((address, offset, length))
    elif command == 2 and size == 24:
        symbols = struct.unpack_from('<IIII', raw, at + 8)
    elif command == 0x1b and size == 24:
        kernel_uuid = str(uuid.UUID(bytes=raw[at + 8:at + 24])).upper()
    at += size
if not symbols or not kernel_uuid:
    parser.error('missing symbol table or UUID')
symoff, count, stroff, strbytes = symbols
if symoff + count * 16 > len(raw) or stroff + strbytes > len(raw):
    parser.error('invalid symbols')
entries = []
for index in range(count):
    string, kind, section, description, address = struct.unpack_from('<IBBHQ', raw, symoff + index * 16)
    if kind & 0xe0 or kind & 0x0e != 0x0e or not section or string >= strbytes:
        continue
    end = raw.find(b'\0', stroff + string, stroff + strbytes)
    if end < 0:
        parser.error('unterminated symbol')
    entries.append((raw[stroff + string:end], address, section))
name = args.symbol.encode('ascii')
matches = [entry for entry in entries if entry[0] == name]
if len(matches) != 1:
    parser.error('requires exactly one concrete matching text symbol')
_, start, section = matches[0]
end = min(address for _, address, sect in entries if sect == section and address > start)
matching = [(offset + start - address, end - start) for address, offset, length in segments
            if address <= start < end <= address + length]
if len(matching) != 1:
    parser.error('method is not contained in exactly one file-backed segment')
offset, length = matching[0]
code = raw[offset:offset + length]
fingerprint = 14695981039346656037
for byte in code:
    fingerprint = ((fingerprint ^ byte) * 1099511628211) & ((1 << 64) - 1)
print(json.dumps({'KernelUUID': kernel_uuid, 'Method': name.decode(), 'Address': hex(start),
                  'FileOffset': hex(offset), 'BytesIncludingAlignment': length,
                  'FNV1a64': hex(fingerprint), 'Prefix': code[:16].hex()}, indent=2))
