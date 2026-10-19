#!/usr/bin/env python3
"""Compare native CoreDisplay/SkyLight transformations with their references."""
import runpy
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

reference = runpy.run_path(str(Path(__file__).with_name('test-mojave-patches.py')))
tool = Path(sys.argv[1]).resolve()
kind = sys.argv[2] if len(sys.argv) > 2 else 'core'
assert kind in ('core', 'sky')
operation = 'mojave-coredisplay' if kind == 'core' else 'mojave-skylight'
with tempfile.TemporaryDirectory(prefix='native-mojave-core-') as temporary:
    folder = Path(temporary)
    source, expected, actual, twice = [folder/n for n in ('source', 'expected', 'actual', 'twice')]
    def run(input_path, output_path, success=True):
        result = subprocess.run([str(tool), operation, str(input_path), str(output_path)], capture_output=True)
        assert (result.returncode == 0) == success, result.stderr
    for base, shift in ((0, 0), (0x7fff20000000, 256), (0x100000000, 512)):
        raw = reference['fixture'](kind, base, shift)
        other = b'original non-x86_64 content'.ljust(4096, b'x')
        for wide in (None, False, True):
            data = raw
            if wide is not None:
                header = struct.pack('>II', 0xcafebabf if wide else 0xcafebabe, 2)
                for cpu, offset, size in ((0x1000007, 4096, len(raw)), (7, 4096+len(raw), len(other))):
                    header += (struct.pack('>IIQQII', cpu, 3, offset, size, 12, 0) if wide else
                               struct.pack('>IIIII', cpu, 3, offset, size, 12))
                data = header.ljust(4096, b'\0')+raw+other
            source.write_bytes(data)
            if kind == 'core': reference['core'].patch(str(source), str(expected))
            else: reference['sky'].patch(str(source), str(expected), True)
            run(source, actual)
            assert actual.read_bytes() == expected.read_bytes()
            run(actual, twice)
            assert twice.read_bytes() == expected.read_bytes()
            assert source.read_bytes() == data
            run(source, actual, False)  # Never overwrite an existing output.
            actual.unlink(); twice.unlink()
    for bad in ('unknown', 'ambiguous', 'padding'):
        source.write_bytes(reference['fixture'](kind, 0, bad=bad))
        run(source, actual, False)
        assert not actual.exists()
    for data in (b'', b'\xcf\xfa\xed\xfe', struct.pack('>II', 0xcafebabe, 100)):
        source.write_bytes(data); run(source, actual, False)
        assert not actual.exists()
print('PASS native Mojave', kind, ': exact reference bytes, relocated layouts, FAT32/FAT64, idempotence and refusal')
