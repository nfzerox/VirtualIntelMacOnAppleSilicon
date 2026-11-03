#!/usr/bin/env python3
"""Finite, disposable BIOS TSC/PIT check. Never opens a macOS VM disk.

Usage: python3 check-tcg-tsc-frequency.py /path/to/qemu-system-x86_64
       [--bios-directory /path/to/pc-bios]
Intended for native ARM QEMU on Darwin. Tests the default and explicit rates.
"""
import argparse
from pathlib import Path
import re
import statistics
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('qemu', type=Path)
    parser.add_argument('--bios-directory', type=Path)
    parser.add_argument('--icount', action='store_true',
                        help='confirm instruction-count timing remains at its existing 1 GHz rate')
    args = parser.parse_args()
    source = Path(__file__).resolve().with_name('tcg-tsc-frequency.asm')
    with tempfile.TemporaryDirectory(prefix='vim-tsc-probe-') as directory:
        image = Path(directory) / 'probe.img'
        subprocess.run(['nasm', '-f', 'bin', str(source), '-o', str(image)], check=True)
        for frequency in (None, 1000000000, 1200000000, 2000000000):
            cpu = 'Penryn' + (f',tsc-frequency={frequency}' if frequency else '')
            command = [str(args.qemu.resolve()), '-machine', 'pc', '-accel', 'tcg',
                       '-cpu', cpu, '-smp', '1', '-m', '16', '-display', 'none',
                       '-serial', 'none', '-monitor', 'none', '-debugcon', 'stdio',
                       '-device', 'isa-debug-exit,iobase=0xf4,iosize=4',
                       '-drive', f'file={image},format=raw,if=ide,snapshot=on']
            if args.bios_directory:
                command += ['-L', str(args.bios_directory.resolve())]
            if args.icount:
                command += ['-icount', 'shift=3,align=off,sleep=off']
            result = subprocess.run(command, capture_output=True, text=True, timeout=30)
            lines = result.stdout.splitlines()
            if result.returncode != 33 or len(lines) != 5 or not all(
                    re.fullmatch('[0-9A-F]{16}', line) for line in lines):
                raise RuntimeError((command, result.returncode, result.stdout, result.stderr))
            # PIT channel 2 is loaded with 60000 ticks at 1193182 Hz.
            rates = [int(line, 16) * 1193182 / 60000 for line in lines]
            measured = statistics.median(rates)
            expected = 1000000000 if args.icount else frequency or 1000000000
            relative = abs(measured / expected - 1)
            print(f'{frequency or "default"}: measured={measured:.0f} Hz '
                  f'expected={expected} error={relative:.3%}', flush=True)
            if relative > 0.08:
                raise SystemExit('TSC/PIT rate mismatch; inspect host load and counter scaling')


if __name__ == '__main__':
    main()
