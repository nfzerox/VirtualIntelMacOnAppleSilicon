#!/usr/bin/env python
"""Preserve this guest's i386 OpenGL entry library without Xcode or lipo.

Writes only the staging destination. Caller must ad-hoc sign the output.
Accept stock universal input or the previously preserved private i386 image.
"""
from __future__ import print_function
import struct
import sys

PRIVATE = b'/System/Library/Frameworks/PVGGL32.dylib'
OLD_PRIVATE = b'/Library/PVG/OpenGL32.dylib'
STOCK = b'/System/Library/Frameworks/OpenGL.framework/Versions/A/OpenGL'


def require(value, message):
    if not value:
        raise ValueError(message)


def prepare(data, identity=PRIVATE):
    if data[:4] in (b'\xca\xfe\xba\xbe', b'\xca\xfe\xba\xbf'):
        wide = data[:4] == b'\xca\xfe\xba\xbf'
        count = struct.unpack_from('>I', data, 4)[0]
        stride = 32 if wide else 20
        require(count <= 64 and 8+count*stride <= len(data), 'invalid fat header')
        slices = []
        for index in range(count):
            p = 8+index*stride
            cpu = struct.unpack_from('>I', data, p)[0]
            offset, size = struct.unpack_from('>QQ' if wide else '>II', data, p+8)
            require(offset+size <= len(data), 'truncated slice')
            if cpu == 7:
                slices.append(data[offset:offset+size])
        require(len(slices) == 1, 'expected exactly one i386 slice')
        data = slices[0]
    require(len(data) >= 28 and struct.unpack_from('<II', data) == (0xfeedface, 7),
            'expected native i386 Mach-O')
    count, size = struct.unpack_from('<II', data, 16)
    require(28+size <= len(data), 'invalid load command range')
    output = bytearray(data)
    cursor, identities = 28, 0
    commands = []
    first_section = len(data)
    for unused in range(count):
        require(cursor+8 <= 28+size, 'truncated load command')
        command, length = struct.unpack_from('<II', data, cursor)
        require(length >= 8 and cursor+length <= 28+size, 'invalid command length')
        rewritten = data[cursor:cursor+length]
        if command == 1:  # LC_SEGMENT: preserve all section/file offsets.
            require(length >= 56, 'truncated segment')
            sections = struct.unpack_from('<I', data, cursor+48)[0]
            require(56+68*sections <= length, 'truncated section table')
            for index in range(sections):
                section = cursor+56+68*index
                offset = struct.unpack_from('<I', data, section+40)[0]
                if offset:
                    first_section = min(first_section, offset)
        if command in (0xd, 0x8000001f):
            require(length >= 24, 'invalid dylib command')
            start = struct.unpack_from('<I', data, cursor+8)[0]
            require(24 <= start < length, 'invalid dylib name offset')
            end = data.find(b'\0', cursor+start, cursor+length)
            require(end != -1, 'unterminated dylib name')
            name = data[cursor+start:end]
            if command == 0x8000001f:
                require(name not in (PRIVATE, OLD_PRIVATE), 'routing facade is not a native backup')
            else:
                require(name in (STOCK, PRIVATE, OLD_PRIVATE), 'unrecognized OpenGL identity')
                new_length = max(length, (start+len(identity)+1+3) & ~3)
                rewritten = bytearray(data[cursor:cursor+start])
                struct.pack_into('<I', rewritten, 4, new_length)
                rewritten.extend(identity + b'\0'*(new_length-start-len(identity)))
                identities += 1
        commands.append(bytes(rewritten))
        cursor += length
    require(cursor == 28+size and identities == 1, 'invalid command table/identity')
    table = b''.join(commands)
    # Old releases shortened LC_ID_DYLIB. Grow back into existing header
    # padding only; never shift code, sections, relocations, or linkedit.
    end = 28+len(table)
    require(end <= first_section, 'insufficient Mach-O header padding')
    require(not any(bytearray(data[28+size:end])), 'nonzero Mach-O header padding')
    output[28:end] = table
    struct.pack_into('<I', output, 20, len(table))
    return bytes(output)


if __name__ == '__main__':
    if len(sys.argv) not in (3, 4):
        sys.exit('usage: stage-opengl-native32.py INPUT NEW_OUTPUT [EXISTING_NATIVE_BACKUP]')
    import os
    if os.path.exists(sys.argv[2]):
        sys.exit('output already exists')
    try:
        with open(sys.argv[1], 'rb') as source:
            result = prepare(source.read())
    except ValueError as error:
        if str(error) != 'routing facade is not a native backup' or len(sys.argv) != 4:
            raise
        with open(sys.argv[3], 'rb') as source:
            result = prepare(source.read())
    with open(sys.argv[2], 'wb') as destination:
        destination.write(result)
