#!/usr/bin/env python3
"""Build a scratch Common EFI with a Snow Leopard or Lion UART startup lock.

The extension subclasses Apple's original IOSerialBSDClient. It does not
replace an installed driver or change any of the other guest profiles.
"""
import argparse
from pathlib import Path
import plistlib
import runpy
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
IDENTIFIER = 'com.virtualintelmac.SerialStartup'
BUNDLE = 'VIMSerialStartup.kext'
PLIST = 'Contents/Info.plist'
BINARY = 'Contents/MacOS/VIMSerialStartup'


def build_extension(work, serial, sdk, xnu):
    kernel_headers = sdk / 'System/Library/Frameworks/Kernel.framework/Headers'
    headers = [xnu / part for part in ('bsd', 'osfmk', 'iokit', 'libkern', 'pexpert')]
    headers += [kernel_headers, sdk / 'usr/include', serial / 'IOSerialFamily.kmodproj']
    arch = ('-arch', 'x86_64', '-mmacosx-version-min=10.6')
    includes = [arg for path in headers for arg in ('-I', path)]
    source = HERE.parent / 'tools/snowleopard-serial-startup.cpp'

    def run(*args):
        subprocess.run(args, cwd=work, check=True)

    run('xcrun', 'clang++', *arch, '-isysroot', sdk, *includes, '-std=gnu++98',
        '-mkernel', '-fapple-kext', '-fno-builtin', '-fasm-blocks', '-nostdinc',
        '-DKERNEL', '-DKERNEL_PRIVATE', '-DDRIVER_PRIVATE', '-DBSD_KERNEL_PRIVATE',
        '-DAPPLE', '-DNeXT', '-O2', '-w', '-c', source, '-o', 'startup.o')
    run('xcrun', 'clang', *arch, '-nostdinc', '-I', kernel_headers,
        '-w', '-c', HERE.parent / 'tools/snowleopard-serial-module.c', '-o', 'module.o')
    for name in ('kmod', 'kmodc++'):
        thin = work / (name + '.a')
        run('lipo', sdk / 'usr/lib' / ('lib' + name + '.a'),
            '-thin', 'x86_64', '-output', thin)
        run('/usr/bin/ar', '-x', thin)
    run('xcrun', 'ld', '-arch', 'x86_64', '-kext', '-macos_version_min', '10.6',
        'startup.o', 'cplus_start.o', 'cplus_stop.o', 'module.o', 'c_start.o',
        'c_stop.o', '-o', 'VIMSerialStartup')
    return (work / 'VIMSerialStartup').read_bytes()


def serial_profile_entries(config_bytes, executable, guest='snowleopard'):
    kernels = {'snowleopard': ('10.7.3', '10.8.0'), 'lion': ('11.0.0', '11.99.99')}
    minimum, maximum = kernels[guest]
    config = plistlib.loads(config_bytes)
    info = dict(CFBundleIdentifier=IDENTIFIER, CFBundleName='VIMSerialStartup',
                CFBundleExecutable='VIMSerialStartup', CFBundlePackageType='KEXT',
                CFBundleVersion='1.0.0', OSBundleRequired='Root',
                OSBundleLibraries={'com.apple.iokit.IOSerialFamily': '10.0.3',
                                   **{f'com.apple.kpi.{p}': '10.0.0d2'
                                      for p in ('iokit', 'libkern', 'mach')}},
                IOKitPersonalities={'Virtual PC UART': dict(
                    CFBundleIdentifier=IDENTIFIER, IOClass='VIMSerialStartup',
                    IOProviderClass='com_apple_driver_16X50UARTSync',
                    IOProbeScore=1100, IOResourceMatch='IOBSD')})
    config['Kernel']['Add'] = [entry for entry in config['Kernel']['Add']
                               if entry.get('BundlePath') != BUNDLE]
    config['Kernel']['Add'].append(dict(Arch='x86_64', BundlePath=BUNDLE, Enabled=True,
        Comment='Serialize native '+('Lion' if guest == 'lion' else 'Snow Leopard')+' PC UART publication',
        ExecutablePath=BINARY, PlistPath=PLIST, MinKernel=minimum, MaxKernel=maximum))
    return {'config.plist': plistlib.dumps(config, sort_keys=False),
            'Kexts/' + BUNDLE + '/' + PLIST: plistlib.dumps(info),
            'Kexts/' + BUNDLE + '/' + BINARY: executable}


def build_image(work, firmware, executable, output, guest='snowleopard'):
    unified = runpy.run_path(str(HERE / 'build-unified-efi.py'))
    volume = unified['volume'](firmware)
    fat = unified['FAT32'](volume)
    root = 'EFI/VIM/'+guest+'/'
    entries = serial_profile_entries(fat.read(root + 'config.plist'), executable, guest)
    for path, data in entries.items():
        fat.write(root + path, data, replace=path == 'config.plist')
    raw = work / 'BOOT.img'
    raw.write_bytes(volume)
    runpy.run_path(str(HERE / 'make-efi-disk.py'))['create'](raw, output)
    readback = unified['FAT32'](unified['volume'](output))
    if any(readback.read(root + path) != data for path, data in entries.items()):
        raise ValueError(guest+' EFI failed read-back')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--guest', choices=('snowleopard', 'lion'), default='snowleopard')
    parser.add_argument('--firmware', type=Path,
                        help='embed in this Common EFI; omit to output only the compiled kext executable')
    for option in ('executable', 'serial', 'sdk', 'xnu'):
        parser.add_argument('--' + option, type=Path)
    args = parser.parse_args()
    sources = (args.serial, args.sdk, args.xnu)
    if (args.executable and any(sources)) or (not args.executable and not all(sources)):
        parser.error('Supply either --executable or all of --serial, --sdk and --xnu')
    if args.output.exists():
        parser.error('Use a new output path')
    with tempfile.TemporaryDirectory(prefix='vim-snow-serial-efi-') as folder:
        work = Path(folder)
        binary = (args.executable.read_bytes() if args.executable else
                  build_extension(work, *(p.resolve() for p in sources)))
        if args.firmware:
            build_image(work, args.firmware.resolve(), binary, args.output.resolve(), args.guest)
        else:
            args.output.write_bytes(binary)
