#!/usr/bin/env python3
"""Build native guest SMB compatibility, never install it on the host."""
import argparse
from pathlib import Path
import shutil
import subprocess

SOURCE = Path(__file__).resolve().parents[1]
METADATA = {'lion': 'LionSMB.plist', 'yosemite': 'YosemiteSMB.plist',
            'elcapitan': 'ElCapitanSMB.plist', 'sierra': 'SierraSMB.plist'}


def verify(directory, guest='lion'):
    binaries = ('opengl-file-sharing',) + (('opengl-lion-smb-spnego.dylib',) if guest == 'lion' else ())
    for name in (*binaries, METADATA[guest]):
        if not (directory / name).is_file():
            raise ValueError('Native File Sharing support is incomplete: ' + name)
    for name in binaries:
        binary = directory / name
        if subprocess.check_output(['lipo', '-archs', binary], text=True).split() != ['x86_64']:
            raise ValueError('Native SMB requires the x86_64 guest ABI')
        subprocess.run(['codesign', '--verify', '--strict', binary], check=True)


def build(sdk, compiler, output, guest='lion'):
    output.mkdir(parents=True, exist_ok=True)
    flags = [str(compiler), '-arch', 'x86_64', '-isysroot', str(sdk),
             '-mmacosx-version-min=10.7', '-O2', '-Wall', '-Wextra', '-Werror',
             '-Wno-deprecated-declarations']
    if guest == 'lion':
        subprocess.run([*flags, '-dynamiclib', str(SOURCE / 'tools/opengl-lion-smb-spnego.c'),
                    str(SOURCE / 'vendor/source/smb-217.19/kernel/netsmb/vim_ntlmssp_wire.c'),
                    '-framework', 'GSS', '-o', str(output / 'opengl-lion-smb-spnego.dylib')], check=True)
    subprocess.run([*flags, '-fno-objc-arc', str(SOURCE / 'tools/opengl-native-file-sharing.m'),
                    '-framework', 'Foundation', '-framework', 'SystemConfiguration',
                    '-framework', 'DiskArbitration', '-framework', 'IOKit', '-framework', 'ServiceManagement',
                    '-o', str(output / 'opengl-file-sharing')], check=True)
    binaries = ('opengl-file-sharing',) + (('opengl-lion-smb-spnego.dylib',) if guest == 'lion' else ())
    for name in binaries:
        subprocess.run(['codesign', '--force', '--sign', '-', '--digest-algorithm=sha1,sha256', output / name], check=True)
    shutil.copyfile(SOURCE / 'PassthroughOpenGL' / METADATA[guest], output / METADATA[guest])
    verify(output, guest)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--verify', type=Path)
    parser.add_argument('--guest', choices=tuple(METADATA), default='lion')
    parser.add_argument('--sdk', type=Path, default=SOURCE / 'resources/sdk/MacOSX10.9.sdk')
    parser.add_argument('--cc', type=Path, default=SOURCE / 'resources/xcode/XcodeMojave.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.verify:
        verify(args.verify, args.guest)
    elif args.output:
        build(args.sdk.resolve(), args.cc.resolve(), args.output.resolve(), args.guest)
    else:
        parser.error('Supply --output or --verify')
