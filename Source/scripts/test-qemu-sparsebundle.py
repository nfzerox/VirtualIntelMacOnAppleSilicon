#!/usr/bin/env python3
"""Finite byte/extent/read-only checks for the detached sparsebundle input.

Development fixture only. The production reader is native QEMU C, not Python.
Both Apple-silicon and Intel tools can run this same fixture on either builder.
"""
import argparse
import json
from pathlib import Path
import plistlib
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qemu-img', type=Path, required=True)
    parser.add_argument('--qemu-io', type=Path, required=True)
    parser.add_argument('--scratch', type=Path, required=True)
    args = parser.parse_args()
    image, io = args.qemu_img.resolve(), args.qemu_io.resolve()

    def run(program, *arguments, success=True):
        result = subprocess.run([program, *map(str, arguments)],
                                capture_output=True, text=True, timeout=30)
        if success and result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        if not success and result.returncode == 0:
            raise RuntimeError('Unexpectedly accepted invalid/write input')
        return result.stdout

    with tempfile.TemporaryDirectory(prefix='sparsebundle-fixture-',
                                     dir=args.scratch) as directory:
        root = Path(directory)
        bundle = root/'Input.sparsebundle'
        bands = bundle/'bands'
        bands.mkdir(parents=True)
        size, band_size = 512 * 1024, 4096
        info = {'diskimage-bundle-type': 'com.apple.diskimage.sparsebundle',
                'bundle-backingstore-version': 1,
                'band-size': band_size, 'size': size}
        contents = {0: bytes([0x5a])*band_size, 2: bytes([0xa7])*123,
                    3: b'', 7: bytes(range(256))*16,
                    size//band_size-1: bytes([0x33])*17}
        reference = bytearray(size)
        for number, data in contents.items():
            (bands/f'{number:x}').write_bytes(data)
            start = number*band_size
            reference[start:start+len(data)] = data
        raw = root/'Reference.raw'
        raw.write_bytes(reference)

        for encoding in (plistlib.FMT_XML, plistlib.FMT_BINARY):
            (bundle/'Info.plist').write_bytes(plistlib.dumps(info, fmt=encoding))
            result = json.loads(run(image, 'info', '--output=json',
                                    '-f', 'apple-sparsebundle', bundle))
            assert result['virtual-size'] == size
            run(image, 'compare', '-f', 'apple-sparsebundle', '-F', 'raw',
                bundle, raw)
            run(io, '-r', '-f', 'apple-sparsebundle', '-c',
                'read -P 0x5a 0 4096', '-c', 'read -P 0 4096 4096', bundle)
            # Exact short-band bytes followed by DiskImages' implicit zeroes.
            run(io, '-r', '-f', 'apple-sparsebundle', '-c',
                'read -P 0xa7 8192 123', '-c', 'read -P 0 8315 3973', bundle)
            extents = json.loads(run(image, 'map', '--output=json',
                                     '-f', 'apple-sparsebundle', bundle))
            assert sum(e['length'] for e in extents) == size
            assert any(e['zero'] and e['length'] >= band_size for e in extents)
            output = root/'Output.qcow2'
            run(image, 'convert', '-f', 'apple-sparsebundle', '-O', 'qcow2',
                bundle, output)
            run(image, 'check', output)
            run(image, 'compare', '-f', 'raw', '-F', 'qcow2', raw, output)
            output.unlink()
            print(f'PASS: {encoding.name}, exact bytes/short tails/holes/QCOW2')

        original = (bands/'0').read_bytes()
        run(io, '-f', 'apple-sparsebundle', '-c', 'write 0 512',
            bundle, success=False)
        assert (bands/'0').read_bytes() == original
        print('PASS: write opening refused, original bytes preserved')

        for overrides in ({'encryption-mode': 2}, {'band-size': 0},
                          {'size': -512}, {'size': size+1}):
            (bundle/'Info.plist').write_bytes(plistlib.dumps({**info, **overrides}))
            run(image, 'info', '-f', 'apple-sparsebundle', bundle, success=False)
        (bundle/'Info.plist').write_bytes(plistlib.dumps(info))
        (bands/'ffff').write_bytes(b'x')
        run(image, 'info', '-f', 'apple-sparsebundle', bundle, success=False)
        (bands/'ffff').unlink()
        print('PASS: invalid geometry/encryption/out-of-range band rejected')

        # Same tiny allocation, 128 GiB virtual image: conversion must use the
        # zero extents, not scan 128 GiB or allocate a flat raw scratch file.
        info['size'] = 128 << 30
        (bundle/'Info.plist').write_bytes(plistlib.dumps(info))
        started = time.monotonic()
        output = root/'Large.qcow2'
        run(image, 'convert', '-f', 'apple-sparsebundle', '-O', 'qcow2',
            bundle, output)
        elapsed = time.monotonic()-started
        result = json.loads(run(image, 'info', '--output=json', output))
        assert result['virtual-size'] == 128 << 30
        assert output.stat().st_size < 2 << 20
        run(image, 'check', output)
        print(f'PASS: 128-GiB sparse conversion in {elapsed:.3f}s; '
              f'{output.stat().st_size} output bytes')


if __name__ == '__main__':
    main()
