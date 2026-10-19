#!/usr/bin/env python3
"""Read the actual x86_64 IOSurface external-method table (no file changes)."""
import argparse
import struct
import subprocess
from pathlib import Path


def inspect(path):
    data = Path(path).read_bytes()
    if data[:4] == bytes.fromhex('cafebabe'):
        count, = struct.unpack_from('>I', data, 4)
        for index in range(count):
            cpu, _, offset, size, _ = struct.unpack_from('>IIIII', data, 8 + index * 20)
            if cpu == 0x01000007:
                data = data[offset:offset + size]
                break
    if data[:4] != bytes.fromhex('cffaedfe'):
        raise ValueError('expected x86_64 Mach-O')
    sections = []
    ncmds, = struct.unpack_from('<I', data, 16)
    cursor = 32
    for _ in range(ncmds):
        command, size = struct.unpack_from('<II', data, cursor)
        if command == 0x19:
            nsects, = struct.unpack_from('<I', data, cursor + 64)
            for index in range(nsects):
                section = cursor + 72 + index * 80
                address, length, offset = struct.unpack_from('<QQI', data, section + 32)
                sections.append((address, length, offset))
        cursor += size
    symbols = {}
    for line in subprocess.check_output(['nm', '-arch', 'x86_64', str(path)], text=True).splitlines():
        parts = line.split()
        if len(parts) == 3:
            symbols[parts[2]] = int(parts[0], 16)
    name = '__ZN23IOSurfaceRootUserClient12sMethodDescsE'
    start = symbols[name]
    end = min(value for value in symbols.values() if value > start)
    address, length, offset = next(s for s in sections if s[0] <= start < s[0] + s[1])
    reverse = {value: key for key, value in symbols.items()}
    print(path)
    print('selector scalar-in structure-in scalar-out structure-out function')
    for index in range((end - start) // 24):
        function, si, bi, so, bo = struct.unpack_from('<QIIII', data, offset + start - address + index * 24)
        print(index, si, hex(bi), so, hex(bo), reverse.get(function, hex(function)))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', nargs='+')
    for binary in parser.parse_args().binary:
        inspect(binary)
