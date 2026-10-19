#!/usr/bin/env python3
"""Retarget unused DriverKit vtable slots to Mojave's reserved predecessor.

Input must be a thin unsigned donor kext executable. No instructions change.
Reject code references: this transformation is only valid for inherited
vtable entries, not a driver that actually invokes DriverKit RPC.
"""
import argparse
import struct
from pathlib import Path

NAMES = {f'__ZN{len(c)}{c}8DispatchE5IORPC' for c in
         ('OSMetaClassBase', 'OSObject', 'IOService', 'IOUserClient')}
RESERVED = '__ZN15OSMetaClassBase25_RESERVEDOSMetaClassBase3Ev'


def patch(source, destination):
    data = bytearray(Path(source).read_bytes())
    assert struct.unpack_from('<II', data) == (0xfeedfacf, 0x01000007)
    cursor = 32
    sections = []
    symcmd = linkcmd = dysym = None
    for _ in range(struct.unpack_from('<I', data, 16)[0]):
        command, size = struct.unpack_from('<II', data, cursor)
        assert size >= 8 and cursor + size <= len(data)
        if command == 0x1d:
            raise ValueError('remove the code signature before patching')
        if command == 2:
            symcmd = cursor
        if command == 0xb:
            dysym = cursor
        if command == 0x19:
            name = bytes(data[cursor+8:cursor+24]).split(b'\0')[0]
            if name == b'__LINKEDIT':
                linkcmd = cursor
            for i in range(struct.unpack_from('<I', data, cursor+64)[0]):
                s = cursor + 72 + 80*i
                address, length = struct.unpack_from('<QQ', data, s+32)
                sections.append((address, address+length, name,
                                 bytes(data[s:s+16]).split(b'\0')[0]))
        cursor += size
    assert symcmd is not None and linkcmd is not None and dysym is not None
    symoff, count, stroff, strsize = struct.unpack_from('<IIII', data, symcmd+8)
    assert symoff + count*16 <= stroff
    assert stroff + strsize <= len(data) <= stroff + strsize + 16
    strings = data[stroff:stroff+strsize]
    matches = {}
    for i in range(count):
        offset, kind = struct.unpack_from('<IB', data, symoff+16*i)
        name = bytes(strings[offset:strings.index(0, offset)]).decode()
        if name in NAMES:
            assert kind & 0xe == 0, 'cannot replace defined Dispatch implementation'
            matches[i] = name
    assert matches, 'no supported Dispatch imports'
    reloff, nreloc = struct.unpack_from('<II', data, dysym+64)
    references = {i: 0 for i in matches}
    for i in range(nreloc):
        address, info = struct.unpack_from('<II', data, reloff+8*i)
        index = info & 0xffffff
        if index not in matches or not (info & (1 << 27)):
            continue
        section = next(s for s in sections if s[0] <= address < s[1])
        assert section[2:] == (b'__DATA', b'__const'), (matches[index], section)
        assert info >> 28 == 0 and (info >> 25) & 3 == 3
        references[index] += 1
    assert all(references.values()), 'import without verified data references'
    replacement_offset = len(strings)
    strings += RESERVED.encode() + b'\0'
    for i, name in matches.items():
        struct.pack_into('<I', data, symoff+16*i, replacement_offset)
        print(f'{name}: {references[i]} data references -> {RESERVED}')
    data[stroff:] = strings
    struct.pack_into('<I', data, symcmd+20, len(strings))
    fileoff, = struct.unpack_from('<Q', data, linkcmd+40)
    filesize = len(data)-fileoff
    struct.pack_into('<Q', data, linkcmd+48, filesize)
    struct.pack_into('<Q', data, linkcmd+32, (filesize+4095) & ~4095)
    Path(destination).write_bytes(data)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input')
    parser.add_argument('output')
    args = parser.parse_args()
    patch(args.input, args.output)
