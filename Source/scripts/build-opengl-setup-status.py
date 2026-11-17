#!/usr/bin/env python3
"""Rebuild only the cold OpenGL setup-status control for a period runtime.

The 10.7 binary is shared by Lion through High Sierra. Earlier Foundation
runtimes use the existing period shim and JSONKit; Tiger remains native i386.
No graphics driver, guest cache, SSH client or source manifest is rebuilt.
"""
import argparse
from pathlib import Path
import subprocess


def build(source, sdk, compiler, linker, minimum, output):
    output.mkdir(parents=True, exist_ok=False)
    tiger = minimum == '10.4'
    period = minimum in ('10.4', '10.5', '10.6')
    architecture = 'i386' if tiger else 'x86_64'
    flags = ['-arch', architecture, '-isysroot', str(sdk),
             '-mmacosx-version-min='+minimum, '-O2', '-fno-objc-arc',
             '-Wall', '-Wextra', '-Werror', '-Wno-deprecated-declarations']
    if period:
        flags += ['-include', 'tools/opengl-period-foundation.h']
    if tiger:
        flags += ['-fno-builtin-memchr', '-Wno-deprecated-objc-isa-usage',
                  '-Wno-deprecated-objc-pointer-introspection']
    objects = []
    units = ['tools/opengl-setup-status.m']
    if period:
        units += ['tools/opengl-period-foundation.m', 'vendor/source/JSONKit/JSONKit.m']
    for unit in units:
        target = output/(Path(unit).stem+'.o')
        warnings = ['-Wno-error', '-Wno-deprecated-objc-isa-usage'] if 'JSONKit' in unit else []
        subprocess.run([str(compiler), *flags, *warnings, '-c', unit, '-o', str(target)],
                       cwd=source, check=True)
        objects.append(str(target))
    executable = output/'opengl-setup-status'
    frameworks = ['Foundation', 'SystemConfiguration', 'DiskArbitration', 'IOKit']
    if tiger:
        frameworks += ['DirectoryService', 'CoreFoundation']
        command = [str(linker), '-arch', architecture, '-macosx_version_min', minimum,
                   '-syslibroot', str(sdk), '-no_uuid', '-lcrt1.o', *objects,
                   '-lgcc_s.10.4', '-lSystem']
    else:
        frameworks += ['CoreServices', 'Security']
        if not period:
            frameworks += ['ServiceManagement']
        command = [str(compiler), *flags, *objects]
    for framework in frameworks:
        command += ['-framework', framework]
    subprocess.run([*command, '-o', str(executable)], cwd=source, check=True)
    if not tiger:
        subprocess.run(['codesign', '--force', '--sign', '-', '--digest-algorithm=sha1,sha256', executable], check=True)
        subprocess.run(['codesign', '--verify', '--strict', executable], check=True)
    print(executable)
    return executable


def main():
    source = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--minimum', choices=('10.4', '10.5', '10.6', '10.7'), required=True)
    parser.add_argument('--sdk', type=Path)
    toolchain = source/'resources/xcode/XcodeMojave.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin'
    parser.add_argument('--cc', type=Path, default=toolchain/'clang')
    parser.add_argument('--ld', type=Path, default=toolchain/'ld')
    parser.add_argument('--output', type=Path, required=True, help='new component build directory')
    args = parser.parse_args()
    sdk_version = {'10.4': '10.4u', '10.5': '10.5', '10.6': '10.6', '10.7': '10.9'}[args.minimum]
    sdk = args.sdk or source/f'resources/sdk/MacOSX{sdk_version}.sdk'
    build(source, sdk.resolve(), args.cc.resolve(), args.ld.resolve(), args.minimum, args.output.resolve())


if __name__ == '__main__':
    main()
