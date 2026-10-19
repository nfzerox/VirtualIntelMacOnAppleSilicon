#!/usr/bin/env python3
"""Compare compiled framework staging with the preserved Python reference."""
import runpy
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

reference = runpy.run_path(str(Path(__file__).with_name('stage-opengl-universal.py')))
tool = Path(sys.argv[1]).resolve()
stock = reference['native32']['STOCK']

def image(cpu, identity=None, reexport=False):
    command = b''
    if identity:
        length = (24+len(identity)+1+3)&~3
        command = (struct.pack('<6I', 0x8000001f if reexport else 13,
                               length, 24, 0, 0, 0)+identity).ljust(length, b'\0')
    wide = cpu == 0x1000007
    header = struct.pack('<'+'I'*(8 if wide else 7),
                         0xfeedfacf if wide else 0xfeedface, cpu, 3, 6,
                         int(bool(command)), len(command), 0, *([0] if wide else []))
    return header+command+bytes(256)+b'code must be preserved'

def write(root, path, data):
    target = root/path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(data)
    target.chmod(0o755)

with tempfile.TemporaryDirectory(prefix='vim-native-gl-') as temporary:
    base = Path(temporary)
    for facade in (False, True):
        root = base/str(facade)
        guest, staged, expected = (root/p for p in ('guest', 'staged', 'expected'))
        native32 = image(7, stock)
        native64 = image(0x1000007)
        native = reference['combine']([(7, 3, 12, native32), (0x1000007, 3, 12, native64)])
        entry = 'Versions/A/OpenGL'
        original = image(7, reference['native32']['PRIVATE'], True) if facade else native
        write(guest, entry, original)
        write(staged, entry, native64)
        # A Catalina-supplied helper gains this guest's native i386 slice.
        write(guest, 'Versions/A/Libraries/helper', native)
        write(staged, 'Versions/A/Libraries/helper', native64)
        # An older compiler absent from the donor manifest stays wholly native.
        write(guest, 'Versions/A/Libraries/old-compiler', native)
        write(staged, 'Versions/A/Libraries/old-compiler', native64)
        write(staged, 'Versions/A/Libraries/new-only', native64)
        manifest = 'Versions/A/Resources/PVGDonorFiles.txt'
        write(staged, manifest, b'Versions/A/OpenGL\nVersions/A/Libraries/helper\n')
        (staged/'OpenGL').symlink_to('Versions/A/OpenGL')
        backup = root/'backup'
        backup.write_bytes(native32)
        shutil.copytree(staged, expected, symlinks=True)
        before = {p.relative_to(guest): p.read_bytes() for p in guest.rglob('*') if p.is_file()}
        reference['merge'](str(guest), str(expected), [str(backup)])
        result = subprocess.run([str(tool), 'stage-opengl', str(guest), str(staged), str(backup)],
                                capture_output=True, text=True)
        assert result.returncode == 0, result.stderr
        for path in expected.rglob('*'):
            if path.is_file():
                assert path.read_bytes() == (staged/path.relative_to(expected)).read_bytes(), path
        assert (staged/'OpenGL').is_symlink()
        assert all((guest/p).read_bytes() == data for p, data in before.items())
        assert len(result.stdout.splitlines()) == 3
        # A malformed manifest must leave every staged byte untouched.
        (staged/manifest).write_text('../escape\nVersions/A/OpenGL\n')
        snapshot = {p: p.read_bytes() for p in staged.rglob('*') if p.is_file()}
        result = subprocess.run([str(tool), 'stage-opengl', str(guest), str(staged)], capture_output=True)
        assert result.returncode != 0
        assert all(p.read_bytes() == data for p, data in snapshot.items())
print('PASS: compiled framework matches reference; native-only libraries, backup recovery, symlinks, input preservation and refusal')
