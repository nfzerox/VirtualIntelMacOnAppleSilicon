#!/usr/bin/env python3
"""Reproduce the Snow Leopard x86_64 serial-lock qualification candidate.

Maintainer tool only. Requires IOSerialFamily-56, Darwin 10 XNU headers, the
10.6 SDK and a native 10.0.3 universal executable. Does not modify a guest.
"""

import argparse
from pathlib import Path
import plistlib
import shutil
import subprocess
import tempfile


PATCH = (Path(__file__).resolve().parents[1] / 'vendor/patches'
         / 'ioserialfamily-snowleopard-allocation-lock.patch')


def run(*command, **options):
    subprocess.run(command, check=True, **options)


def symbols(binary, flags):
    return set(subprocess.check_output(('nm', '-arch', 'x86_64', flags, binary),
                                       text=True).splitlines())


def build(source, sdk, xnu, native, output):
    if output.exists():
        raise ValueError('Use a new output file')
    units = source / 'IOSerialFamily.kmodproj'
    info = plistlib.loads((units / 'IOSerialFamily-Info.plist').read_bytes())
    if info['CFBundleVersion'] != '10.0.3':
        raise ValueError('Use the original IOSerialFamily-56 sources')
    kheaders = sdk / 'System/Library/Frameworks/Kernel.framework/Headers'
    headers = [xnu / part for part in ('bsd', 'osfmk', 'iokit', 'libkern', 'pexpert')]
    headers += [kheaders, sdk / 'usr/include']
    arch = ('-arch', 'x86_64', '-mmacosx-version-min=10.6')
    with tempfile.TemporaryDirectory(prefix='vim-snow-serial-') as folder:
        work = Path(folder)
        shutil.copytree(units, work / units.name)
        run('/usr/bin/patch', '-p1', '-F', '0', '--batch', '--forward', '-i', PATCH, cwd=work)
        common = (*arch, '-isysroot', sdk, '-std=gnu++98', '-mkernel', '-fapple-kext',
                  '-fno-builtin', '-fno-exceptions', '-fno-rtti', '-fasm-blocks',
                  '-fstrict-aliasing', '-fno-common', '-nostdinc', '-DKERNEL',
                  '-DKERNEL_PRIVATE', '-DDRIVER_PRIVATE', '-DBSD_KERNEL_PRIVATE',
                  '-DAPPLE', '-DNeXT', '-O2', '-w')
        includes = [arg for path in headers for arg in ('-I', path)]
        objects = []
        for name in ('IOSerialBSDClient', 'IOSerialStreamSync'):
            obj = work / (name + '.o')
            run('xcrun', 'clang++', *common, *includes,
                '-c', work / units.name / (name + '.cpp'), '-o', obj)
            objects.append(obj)
        module = work / 'module.c'
        module.write_text('''#include <mach/kmod.h>
extern kern_return_t _start(kmod_info_t *,void *);
extern kern_return_t _stop(kmod_info_t *,void *);
KMOD_EXPLICIT_DECL(com.apple.iokit.IOSerialFamily,"10.0.3",_start,_stop)
__private_extern__ kmod_start_func_t *_realmain = 0;
__private_extern__ kmod_stop_func_t *_antimain = 0;
__private_extern__ int _kext_apple_cc = __APPLE_CC__;
''')
        run('xcrun', 'clang', *arch, '-nostdinc', '-I', kheaders, '-w',
            '-c', module, '-o', work / 'module.o')
        for name in ('kmod', 'kmodc++'):
            thin = work / (name + '.a')
            run('lipo', sdk / 'usr/lib' / ('lib' + name + '.a'),
                '-thin', 'x86_64', '-output', thin)
            # The current linker rejects the period archive's member alignment.
            run('/usr/bin/ar', '-x', thin, cwd=work)
        constructors = work / 'constructors.s'
        # Native GCC C1 and C2 do the same work for this single nonvirtual base.
        # Clang emits only C2 for the abstract class; keep the exported ABI.
        constructors.write_text('''.text
.globl __ZN18IOSerialDriverSyncC1EPK11OSMetaClass
__ZN18IOSerialDriverSyncC1EPK11OSMetaClass:
    jmp __ZN18IOSerialDriverSyncC2EPK11OSMetaClass
''')
        run('xcrun', 'clang', *arch, '-c', constructors, '-o', work / 'constructors.o')
        candidate = work / 'IOSerialFamily.x64'
        run('xcrun', 'ld', '-arch', 'x86_64', '-kext', '-macos_version_min', '10.6',
            *objects, *(work / name for name in ('cplus_start.o', 'cplus_stop.o', 'module.o',
                                                 'c_start.o', 'c_stop.o', 'constructors.o')),
            '-o', candidate)
        missing = symbols(native, '-gjU') - symbols(candidate, '-gjU')
        added = symbols(candidate, '-ju') - symbols(native, '-ju')
        # Darwin 10 Libkern.exports already aliases the Clang-emitted name.
        if '___bzero:_bzero' in (xnu / 'config/Libkern.exports').read_text().splitlines():
            added.discard('___bzero')
        if missing or added:
            raise ValueError(f'Unexpected native ABI difference: missing {missing}; imports {added}')
        stock32 = work / 'IOSerialFamily.i386'
        run('lipo', native, '-thin', 'i386', '-output', stock32)
        run('lipo', '-create', stock32, candidate, '-output', output)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ('source', 'sdk', 'xnu', 'native', 'output'):
        parser.add_argument('--' + option, type=Path, required=True)
    args = parser.parse_args()
    build(*(path.resolve() for path in vars(args).values()))
