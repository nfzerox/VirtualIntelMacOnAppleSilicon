"""Bounded diskless load/store comparison; never attaches a macOS disk."""
import os
from pathlib import Path
import re
import subprocess
import statistics
import sys

root = Path(__file__).resolve().parents[1]
image = root / 'artifacts/perf/tlb-pair-memory.img'
subprocess.run(['nasm', '-DPVG_MEMORY_BENCH=1', '-f', 'bin',
                str(root / 'probes/tcg-pcid.asm'), '-o', str(image)], check=True)
env = dict(os.environ, PVG_TCG_TSO='1', PVG_TCG_TAIL_DISPATCH='0',
           PVG_TCG_SHARED_DISPATCH='0')
candidate = sys.argv[1] if len(sys.argv) == 2 else 'tlb-pair-v1'
if not re.fullmatch(r'tlb-(pair|scalar)-v[0-9]+', candidate):
    raise SystemExit('Expected a tlb-pair-vN or tlb-scalar-vN candidate name')
values = {'context-jump-v4': [], candidate: []}
for name in ['context-jump-v4', candidate, candidate,
             'context-jump-v4', 'context-jump-v4', candidate]:
    binary = root / f'artifacts/host-arm64/qemu-arm64-{name}'
    result = subprocess.run([str(binary), '-L', str(root/'vendor/qemu/pc-bios'),
        '-cpu', 'Penryn', '-accel', 'tcg', '-display', 'none', '-serial', 'none',
        '-monitor', 'none', '-debugcon', 'stdio',
        '-device', 'isa-debug-exit,iobase=0xf4,iosize=4',
        '-drive', f'file={image},format=raw,if=ide,snapshot=on'],
        env=env, capture_output=True, text=True, timeout=20)
    match = re.fullmatch(r'([0-9A-F]{16})\s*', result.stdout)
    if result.returncode != 33 or not match:
        raise RuntimeError((result.returncode, result.stdout, result.stderr))
    ticks = int(match[1], 16)
    values[name].append(ticks)
    print(name, ticks, 'ticks; data checks PASS', flush=True)
for name, ticks in values.items():
    print(name, 'median ticks', statistics.median(ticks))
print('paired/baseline', statistics.median(values[candidate]) /
      statistics.median(values['context-jump-v4']))
