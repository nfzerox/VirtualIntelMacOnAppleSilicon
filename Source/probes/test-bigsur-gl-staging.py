"""Bounded native renderer dependency preparation checks; no system writes."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

tool = str(Path(sys.argv[1]).resolve())
metal = b'/System/Library/Frameworks/Metal.framework/Versions/A/Metal'
surface = b'/System/Library/Frameworks/IOSurface.framework/Versions/A/IOSurface'

def dependency(name):
    size = (24 + len(name) + 1 + 7) & ~7
    return struct.pack('<6I', 12, size, 24, 123, 0x10000, 0x10000) + name + bytes(size-24-len(name))

def image(names):
    commands = b''.join(map(dependency, names))
    # Opaque native data must remain untouched by dependency preparation.
    return struct.pack('<8I', 0xfeedfacf, 0x1000007, 3, 8, len(names), len(commands), 0, 0) + commands + b'untouched native data'

def check(command, helper):
  with tempfile.TemporaryDirectory(prefix='vim-bigsur-gpu-') as directory:
    root = Path(directory)
    original = root/'original'; original.write_bytes(image([metal, surface]))
    prepared = root/'prepared'
    subprocess.run([tool, command, original, prepared], check=True)
    expected = bytearray(original.read_bytes())
    for name in (metal, surface):
        begin = expected.index(name)
        expected[begin:begin+len(name)] = helper + bytes(len(name)-len(helper))
    assert prepared.read_bytes() == expected
    again = root/'again'
    subprocess.run([tool, command, prepared, again], check=True)
    assert again.read_bytes() == prepared.read_bytes()
    for index, data in enumerate((image([metal, metal]), image([metal, helper]),
                                  original.read_bytes()[:40], image([metal]))):
        bad, output = root/f'bad-{index}', root/f'output-{index}'
        bad.write_bytes(data)
        result = subprocess.run([tool, command, bad, output], capture_output=True)
        assert result.returncode and not output.exists(), result
    exists = subprocess.run([tool, command, original, prepared], capture_output=True)
    assert exists.returncode and prepared.read_bytes() == expected
check('prepare-bigsur-gl', b'@loader_path/PVGOpenGLMetal.dylib')
check('prepare-bigsur-metal', b'@loader_path/PVGBigSurMetalCompat.dylib')
print('PASS GL + Metal: exact dependency edits, idempotence, partial/truncated/duplicate refusal, no overwrite')
