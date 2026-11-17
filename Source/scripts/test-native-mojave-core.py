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
    # Earlier extracted loose libraries can have a signature but no split
    # command. They must stay loose-only, not acquire an incomplete cache table.
    data = bytearray(reference['fixture'](kind, 0, native_metadata=True))
    count, size = struct.unpack_from('<II', data, 16)
    at = 32
    for _ in range(count):
        command, length = struct.unpack_from('<II', data, at)
        if command == 0x1e:
            data[at:32+size-length] = data[at+length:32+size]
            data[32+size-length:32+size] = bytes(length)
            struct.pack_into('<II', data, 16, count-1, size-length)
            break
        at += length
    else:
        raise AssertionError('fixture has no split command')
    source.write_bytes(data)
    run(source, actual); run(actual, twice)
    assert actual.read_bytes() == twice.read_bytes()
    assert source.read_bytes() == data
    output = actual.read_bytes()
    at = 32
    for _ in range(struct.unpack_from('<I', output, 16)[0]):
        command, length = struct.unpack_from('<II', output, at)
        assert command != 0x1e
        at += length
    actual.unlink(); twice.unlink()
    for table in (b'\x01\0\0', b'\x7f\0'):
        data = bytearray(reference['fixture'](kind, 0, native_metadata=True))
        at = 32
        for _ in range(struct.unpack_from('<I', data, 16)[0]):
            command, size = struct.unpack_from('<II', data, at)
            if command == 0x1e:
                struct.pack_into('<I', data, at+12, len(table))
                data[0x4700:0x4700+len(table)] = table
                break
            at += size
        source.write_bytes(data); run(source, actual)
        output = actual.read_bytes()
        at = 32
        for _ in range(struct.unpack_from('<I', output, 16)[0]):
            command, size = struct.unpack_from('<II', output, at)
            if command == 0x1e:
                offset, length = struct.unpack_from('<II', output, at+8)
                assert length > len(table)  # Injected references were retained.
                assert output[offset] == table[0]
                break
            at += size
        else:
            raise AssertionError('cache-capable image lost its split table')
        # Production signs the prepared file before another preparation pass.
        subprocess.run(['/usr/bin/codesign', '--force', '--sign', '-', '--timestamp=none',
                        str(actual)], check=True, capture_output=True)
        run(actual, twice)
        assert source.read_bytes() == data
        actual.unlink(); twice.unlink()
    # Absence is not permission to waive validation of an existing table.
    for offset, length, contents in ((0x4700, 0, b''), (0x4700, 1, b'\0'),
                                     (0xfffffff0, 64, b''), (0x4700, 3, b'\x03\0\0'),
                                     (0x4700, 3, b'\x01\x80\x80')):
        data = bytearray(reference['fixture'](kind, 0, native_metadata=True))
        at = 32
        for _ in range(struct.unpack_from('<I', data, 16)[0]):
            command, size = struct.unpack_from('<II', data, at)
            if command == 0x1e:
                struct.pack_into('<II', data, at+8, offset, length)
                if contents: data[offset:offset+len(contents)] = contents
                break
            at += size
        source.write_bytes(data); run(source, actual, False)
        assert not actual.exists()
    for data in (b'', b'\xcf\xfa\xed\xfe', struct.pack('>II', 0xcafebabe, 100)):
        source.write_bytes(data); run(source, actual, False)
        assert not actual.exists()
print('PASS native Mojave', kind, ': reference bytes, FAT32/FAT64, loose-only metadata absence, idempotence and malformed-table refusal')
