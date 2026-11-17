#!/usr/bin/env python3
"""Compare focused kernel transitions under real QEMU/TCG and QEMU/hybrid.

Run on Apple silicon with QEMU pointing to an Engine-enabled universal QEMU.
Set QEMU_DATA_DIR to its packaged pc-bios directory when needed. No OS disk,
guest setup, cache rebuild or live debugger is involved.
"""
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    here = Path(__file__).resolve().parent
    qemu = os.environ['QEMU']
    with tempfile.TemporaryDirectory(prefix='engine-kernel-transitions-') as work:
        work = Path(work)
        subprocess.run(['xcrun', 'clang', '-target', 'i386-unknown-none-elf', '-c',
                        str(here/'x86/compat/boot.s'), '-o', str(work/'boot.o')], check=True)
        subprocess.run(['python3', str(here/'x86/compat/link32.py'), '0xF0000',
                        str(work/'boot.bin'), str(work/'boot.o')], check=True)
        for fixture in ('descriptor-widths', 'kernel-far-jumps', 'kernel-far-null-stack'):
            subprocess.run(['zsh', str(here/'build-cross-key-jumps.sh'),
                            str(work), fixture], check=True)
            subprocess.run(['python3', str(here/'x86/compat/mkqemu.py'),
                            str(work/fixture), str(work/'boot.bin'), str(work/'rom'),
                            str(work/'ram'), '0x80000000'], check=True)
            args = [qemu, '-display', 'none', '-no-user-config', '-nodefaults',
                    '-M', 'pc', '-accel', 'tcg,thread=multi',
                    '-cpu', 'max,vendor=GenuineIntel', '-smp', '1', '-m', '2G',
                    '-bios', str(work/'rom'), '-device',
                    f'loader,file={work}/ram,addr=0x100000,force-raw=on',
                    '-device', 'isa-debugcon,iobase=0xe9,chardev=con',
                    '-device', 'isa-debug-exit,iobase=0xf4,iosize=4']
            if os.environ.get('QEMU_DATA_DIR'):
                args += ['-L', os.environ['QEMU_DATA_DIR']]
            outputs = []
            for hybrid in (False, True):
                console = work/('hybrid.out' if hybrid else 'tcg.out')
                run = subprocess.run(args + ['-chardev', f'file,id=con,path={console}'],
                    env={**os.environ, 'LC_HVDBT': str(int(hybrid))},
                    capture_output=True, timeout=30)
                output = console.read_bytes()
                # isa-debug-exit encodes guest status zero as process status 1.
                if run.returncode != 1 or (hybrid and
                        b'hvdbt: the engine takes over' not in run.stderr):
                    raise RuntimeError(f'{fixture}, hybrid={hybrid}: exit {run.returncode}\n'
                                       + (output + run.stderr).decode(errors='replace'))
                outputs.append(output)
            if outputs[0] != outputs[1] or b'PASS ' not in outputs[1]:
                raise RuntimeError(f'{fixture}: different or incomplete transcripts: {outputs!r}')
            print(f'PASS {fixture}: identical TCG/hybrid transcript')


if __name__ == '__main__':
    main()
