#!/usr/bin/env python3
"""Use GLR's ordinary upload conversion for packed ARGB on Mojave PVG.

Qualified for Mojave upload/readback, multisampled windows and Chess.
Patch format selection, never the Metal enum in the format table: changing only
the enum would silently reinterpret channels. Input must be a thin x86_64 donor.
"""
import argparse
import pathlib
import struct
import subprocess


def candidate(data, symbols):
    if struct.unpack_from('<II', data) != (0xfeedfacf, 0x01000007):
        raise ValueError('Expected thin x86_64 Mach-O')
    symbol = '__ZN13GLDTextureRec19chooseTextureFormatEP16GLDShareGroupRecjjjibbbPbS2_'
    matches = [int(line.split()[0], 16) for line in symbols.splitlines()
               if line.split() and line.split()[-1] == symbol]
    if len(matches) != 1:
        raise ValueError('Missing or ambiguous texture-format selector')
    address = matches[0]
    offset = 32
    start = None
    for _ in range(struct.unpack_from('<I', data, 16)[0]):
        command, size = struct.unpack_from('<II', data, offset)
        if size < 8 or offset + size > len(data):
            raise ValueError('Invalid load command')
        if command == 0x19:
            vm, length, file, filesz = struct.unpack_from('<QQQQ', data, offset + 24)
            if vm <= address < vm + min(length, filesz):
                start = file + address - vm
        offset += size
    if start is None:
        raise ValueError('Selector is not file backed')
    # Both donor branches compute the ordinary GLR format in ECX before the
    # private ARGB special case. MOV preserves flags consumed by the CMOVs.
    patterns = [
        bytes.fromhex('81fa358000008d4c0024b8550000000f45c1'),
        bytes.fromhex('83c12781fa35800000b856000000eb7a'),
    ]
    result = bytearray(data)
    body = data[start:start + 0x940]
    for pattern in patterns:
        replacement = pattern.replace(pattern[pattern.index(b'\xb8'):pattern.index(b'\xb8')+5], bytes.fromhex('89c8909090'), 1)
        if body.count(pattern) + body.count(replacement) != 1:
            raise ValueError('Unknown donor selector instructions; refusing patch')
        if replacement in body:
            continue
        position = start + body.index(pattern) + pattern.index(b'\xb8')
        result[position:position + 5] = bytes.fromhex('89c8909090')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=pathlib.Path)
    parser.add_argument('output', type=pathlib.Path)
    args = parser.parse_args()
    symbols = subprocess.check_output(['nm', '-n', str(args.input)], text=True)
    result = candidate(args.input.read_bytes(), symbols)
    with args.output.open('xb') as output:
        output.write(result)
    args.output.chmod(args.input.stat().st_mode & 0o777)
    print('Packed ARGB selection validated; re-sign the output before installation.')
