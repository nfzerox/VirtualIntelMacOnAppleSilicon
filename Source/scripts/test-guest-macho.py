#!/usr/bin/env python3
"""Fast native slice-tool tests; only synthetic files in a private temp directory."""
import struct
import hashlib
import subprocess
import sys
import tempfile
import runpy
from pathlib import Path

tool = Path(sys.argv[1]).resolve()
def image(cpu, subtype, payload):
    wide = cpu == 0x1000007
    return struct.pack('<'+'I'*(8 if wide else 7),
                       0xfeedfacf if wide else 0xfeedface, cpu, subtype, 6, 0, 0, 0,
                       *([0] if wide else []))+payload

def fat(a, b, wide=False):
    result = bytearray(8192+len(b))
    result[:8] = struct.pack('>II', 0xcafebabf if wide else 0xcafebabe, 2)
    stride = 32 if wide else 20
    for i, (cpu, content, offset) in enumerate(((7, a, 4096), (0x1000007, b, 8192))):
        record = (struct.pack('>IIQQII', cpu, 3, offset, len(content), 12, 0) if wide else
                  struct.pack('>IIIII', cpu, 3, offset, len(content), 12))
        result[8+i*stride:8+(i+1)*stride] = record
        result[offset:offset+len(content)] = content
    return result

with tempfile.TemporaryDirectory(prefix='vim-macho-test-') as temporary:
    root = Path(temporary)
    source, replacement, output = (root/n for n in ('source', 'replacement', 'output'))
    original32 = image(7, 3, b'preserve every original i386 byte'*200)
    original64 = image(0x1000007, 3, b'old 64 bit code')
    # Use a non-overlapping first slice for the 4 KiB fixture.
    original32 = original32[:2000]
    new64 = image(0x1000007, 8, b'new x86_64 subtype and implementation'*300)
    replacement.write_bytes(new64)
    def run(*args, success=True):
        result = subprocess.run([str(tool), *map(str, args)], capture_output=True)
        assert (result.returncode == 0) == success, result.stderr
        return result
    for wide in (False, True):
        source.write_bytes(fat(original32, original64, wide))
        run('thin', source, output)
        assert output.read_bytes() == original64
        output.unlink()
        run('replace-x86_64', source, replacement, output)
        combined = output.read_bytes()
        stride = 32 if wide else 20
        for i, expected in enumerate((original32, new64)):
            p = 8+i*stride
            offset, size = struct.unpack_from('>QQ' if wide else '>II', combined, p+8)
            assert combined[offset:offset+size] == expected
            assert struct.unpack_from('>I', combined, p+4)[0] == (3 if i == 0 else 8)
        run('thin', output, root/'rethinned')
        assert (root/'rethinned').read_bytes() == new64
        (root/'rethinned').unlink()
        before = output.read_bytes()
        run('thin', source, output, success=False)
        assert output.read_bytes() == before
        output.unlink()
    malformed = fat(original32, original64)
    struct.pack_into('>I', malformed, 8+20+8, 4096)  # overlapping slices
    source.write_bytes(malformed)
    run('thin', source, output, success=False)
    assert not output.exists()
    for data in (b'bad', fat(original32, original64)[:100], original64):
        source.write_bytes(data)
        run('replace-x86_64', source, replacement, output, success=False)
        assert not output.exists()
    native = runpy.run_path(str(Path(__file__).with_name('stage-opengl-native32.py')))
    universal = runpy.run_path(str(Path(__file__).with_name('stage-opengl-universal.py')))
    for container in (original32, fat(original32, original64), fat(original32, original64, True)):
        combined = universal['native_merge'](container, new64, str(tool))
        parts = universal['slices'](combined)
        assert [(part[0], part[3]) for part in parts] == [(7, original32), (0x1000007, new64)]
    source.write_bytes(original32)
    run('thin', source, output, success=False)
    assert not output.exists()
    for identity in (native['STOCK'], native['PRIVATE'], native['OLD_PRIVATE']):
        length = (24+len(identity)+1+3)&~3
        command = struct.pack('<IIIIII', 0xd, length, 24, 0, 0, 0)+identity
        command += bytes(length-len(command))
        fixture = bytearray(image(7, 3, command+bytes(256)+b'unchanged code'))
        struct.pack_into('<II', fixture, 16, 1, length)
        expected = native['prepare'](bytes(fixture), native['STOCK'])
        for container in (fixture, fat(fixture, original64), fat(fixture, original64, True)):
            source.write_bytes(container)
            run('opengl-native32', source, output)
            assert output.read_bytes() == expected
            output.unlink()
            assert universal['native_entry'](str(source), str(tool)) == expected
        # A facade reexporting the private implementation is not the native image.
        if identity != native['STOCK']:
            struct.pack_into('<I', fixture, 28, 0x8000001f)
            source.write_bytes(fixture)
            run('opengl-native32', source, output, success=False)
            assert not output.exists()
            try:
                universal['native_entry'](str(source), str(tool))
                raise AssertionError('routing facade accepted by staging')
            except ValueError as error:
                assert str(error) == 'routing facade is not a native backup'
    for data in (b'', b'hello', bytes(range(256))*1000):
        source.write_bytes(data)
        result = run('sha256', source)
        assert result.stdout.decode() == hashlib.sha256(data).hexdigest()+'\n'
    link = root/'hash-symlink'; link.symlink_to(source)
    run('sha256', link, success=False)
    run('sha256', root, success=False)
print('PASS: FAT32/FAT64, i386 preservation, donor subtype, thinning, refusal and no overwrite')
print('PASS: native OpenGL identity matches legacy implementation; routing facades refused')
print('PASS: streaming SHA-256; symlinks and nonregular inputs refused')
