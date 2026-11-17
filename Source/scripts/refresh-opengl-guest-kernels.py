#!/usr/bin/env python3
"""Rebuild only the pre-Metal accelerators, preserving profile/audio metadata.

Ordinary app builds use these checked-in outputs, not historical toolchains.
Stages all selected builds before optional publication. No VM, mounting,
guest boot cache, firmware image or source-sealing manifest is involved.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

GUESTS = ('tiger', 'leopard', 'snowleopard', 'lion', 'mountainlion',
          'mavericks', 'yosemite', 'elcapitan', 'sierra', 'highsierra')
EXECUTABLE = Path('VMsvga2Accel.kext/Contents/MacOS/VMsvga2Accel')


def main():
    source = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True, help='New candidate directory')
    parser.add_argument('--guest', action='append', choices=GUESTS)
    parser.add_argument('--sdk-root', type=Path, default=source/'resources/sdk')
    parser.add_argument('--sdk', action='append', default=[], metavar='GUEST=PATH',
                        help='Override an SDK location, e.g. sierra=/path/MacOSX.sdk')
    parser.add_argument('--cc', type=Path, default=os.environ.get('VIM_OPENGL_CC',
        source/'resources/xcode/XcodeMojave.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang'))
    parser.add_argument('--period-cc', type=Path, default=os.environ.get('VIM_OPENGL_KERNEL_PERIOD_CC',
        source/'resources/xcode/Xcode3.2.6-GCC-Tools/usr/bin/i686-apple-darwin10-gcc-4.2.1'))
    parser.add_argument('--period-binutils', type=Path, default=os.environ.get('VIM_OPENGL_KERNEL_PERIOD_BINUTILS',
        source/'resources/xcode/Xcode3.2.6-CLI-Tools/usr/bin'))
    parser.add_argument('--publish', action='store_true')
    args = parser.parse_args()
    selected = [guest for guest in GUESTS if not args.guest or guest in args.guest]
    sdks = {guest:args.sdk_root/f'MacOSX10.{"4u" if index==0 else 4+index}.sdk'
            for index, guest in enumerate(GUESTS)}
    for override in args.sdk:
        guest, separator, path = override.partition('=')
        if not separator or guest not in GUESTS or not path:
            parser.error('--sdk must be a supported GUEST=PATH')
        sdks[guest] = Path(path)
    if args.output.exists():
        parser.error('--output must be a new directory')
    # Fail on missing inputs BEFORE compiling any family.
    for guest in selected:
        if not (sdks[guest]/'SDKSettings.plist').is_file():
            parser.error(f'Supply the genuine {guest} SDK with --sdk {guest}=PATH')
        compiler = args.period_cc if guest in ('tiger', 'leopard') else args.cc
        if not os.access(compiler, os.X_OK):
            parser.error('Compiler unavailable: '+str(compiler))
        target = source/'prebuilt/PassthroughOpenGL'/guest/'Kernel'/EXECUTABLE
        if not target.is_file():
            parser.error('Existing accelerator payload unavailable: '+str(target))
    if any(guest in ('tiger', 'leopard') for guest in selected):
        for name in ('as', 'ld'):
            if not os.access(args.period_binutils/name, os.X_OK):
                parser.error('Period binutils unavailable: '+str(args.period_binutils/name))
    args.output = args.output.resolve()
    args.output.mkdir(parents=True)
    for guest in selected:
        period = guest in ('tiger', 'leopard')
        command = [sys.executable, source/'scripts/build-opengl-kernel.py',
            '--sdk', sdks[guest].resolve(), '--cc', (args.period_cc if period else args.cc).resolve(),
            '--accelerator-only', '--output', args.output/guest]
        if period:
            command += ['--binutils', args.period_binutils.resolve()]
        log = args.output/(guest+'.log')
        print(f'Building {guest} accelerator; log: {log}', flush=True)
        with log.open('x') as stream:
            subprocess.run(list(map(str, command)), stdout=stream, stderr=subprocess.STDOUT, check=True)
    if args.publish:
        # Keep the existing framebuffer, audio, Info.plist personalities and
        # file inventories. The EFI builder applies each family's actual caps.
        for guest in selected:
            target = source/'prebuilt/PassthroughOpenGL'/guest/'Kernel'/EXECUTABLE
            shutil.copy2(args.output/guest/EXECUTABLE, target)
            target.chmod(0o755)
        print('Published source-built accelerator executables for: '+', '.join(selected))
    else:
        print('Candidates only; checked-in payloads and guests unchanged.')


if __name__ == '__main__':
    main()
