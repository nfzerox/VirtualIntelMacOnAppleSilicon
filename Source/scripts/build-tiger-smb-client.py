#!/usr/bin/env python3
"""Rebuild/package native Tiger/Leopard SMB clients, never install/load them.

Normal builds reuse the self-contained published component. An explicit
--rebuild uses genuine period SDK/GCC/binutils and the vendored kernel headers,
not a research checkout or a guest toolchain. No generated hash manifests.
"""
import argparse
from pathlib import Path
import plistlib
import runpy
import shutil
import subprocess
import sys


FILES = ('tiger-smbfs', 'TigerSMBClient-Info.plist', 'SMB-COPYRIGHT.txt', 'SMB-VIM-LICENSE.txt')
CLIENTS = {
    'tiger': ('Tiger', '10.4', '1.3.7', 'smb-217.19', 'xnu-792.22.5-smb-headers',
              '1b47f5645571a9af0e85e55af5ff68f09d310537'),
    'leopard': ('Leopard', '10.5', '1.4.6', 'smb-348.7', 'xnu-1228.15.4-smb-headers',
                '91f35b3eb0b2445137005c97a04da6482e0c2a11'),
}

def client_files(guest):
    return (guest+'-smbfs',CLIENTS[guest][0]+'SMBClient-Info.plist',
            'SMB-COPYRIGHT.txt','SMB-VIM-LICENSE.txt')


def verify(directory, source=None, guest='tiger'):
    source = source or Path(__file__).resolve().parents[1]
    display,minimum,version,*_=CLIENTS[guest];files=client_files(guest)
    record = plistlib.loads((directory/(display+'SMBClient.plist')).read_bytes())
    info = plistlib.loads((directory/(display+'SMBClient-Info.plist')).read_bytes())
    if (record.get('FormatVersion') != 1 or record.get('Guest') != guest or
            record.get('PolicyVersion') != 2 or set(record.get('Files', {})) != set(files) or
            info.get('CFBundleIdentifier') != 'com.apple.filesystems.smbfs' or
            info.get('CFBundleExecutable') != 'smbfs' or info.get('CFBundleVersion') != version):
        raise ValueError('Native SMB client payload is incomplete or incompatible')
    for name in files:
        if not (directory/name).is_file() or not (directory/name).stat().st_size:
            raise ValueError('Missing native SMB client component: '+name)
    runpy.run_path(str(source/'scripts/build-opengl-kernel.py'))['verify_period_object'](
        directory/(guest+'-smbfs'), minimum)
    return record


def publish(build, output, source, guest='tiger'):
    output.mkdir(parents=True, exist_ok=False)
    display,minimum,version,upstream,headers,commit=CLIENTS[guest]
    origins = {
        guest+'-smbfs': build/'smbfs.kext/Contents/MacOS/smbfs',
        display+'SMBClient-Info.plist': build/'smbfs.kext/Contents/Info.plist',
        'SMB-COPYRIGHT.txt': source/'vendor/source'/upstream/'COPYRIGHT',
        'SMB-VIM-LICENSE.txt': source/'vendor/source'/upstream/'VIM-LICENSE.txt',
    }
    for name, origin in origins.items():
        shutil.copyfile(origin, output/name)
        (output/name).chmod(0o755 if name == guest+'-smbfs' else 0o644)
    # Darwin8 kld requires the original relocatable layout; modern codesign
    # must not convert that native kernel object into a later bundle format.
    record = {'FormatVersion': 1, 'Guest': guest, 'PolicyVersion': 2,
              'Architecture': 'i386', 'MinimumOS': minimum,
              'RequiresGuestDeveloperTools': False,
              'UpstreamCommit': commit,
              'Files': {name: True for name in client_files(guest)}}
    (output/(display+'SMBClient.plist')).write_bytes(plistlib.dumps(record))
    verify(output, source, guest)


def main():
    source = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--guest',choices=CLIENTS,default='tiger')
    for name in ('sdk', 'cc', 'binutils', 'build', 'from-build', 'verify'):
        parser.add_argument('--'+name, type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--rebuild', action='store_true')
    args = parser.parse_args()
    display,minimum,version,upstream,headers,commit=CLIENTS[args.guest]
    if args.output is None:args.output=source/('Build/'+display+'SMBClient')
    if args.verify:
        verify(args.verify.resolve(), source, args.guest)
        print(display+' native SMB client payload verified')
        return
    if args.from_build:
        if args.rebuild:
            parser.error('Choose --rebuild or --from-build')
        publish(args.from_build.resolve(), args.output.resolve(), source, args.guest)
    elif args.rebuild:
        if not all((args.sdk, args.cc, args.binutils, args.build)):
            parser.error('--rebuild requires --sdk, --cc, --binutils and a new --build directory')
        subprocess.run([sys.executable, str(source/'probes/build-leopard-smb-client.py'),
                        '--source', str(source/'vendor/source'/upstream),
                        '--xnu', str(source/'vendor/source'/headers),
                        '--sdk', str(args.sdk.resolve()), '--cc', str(args.cc.resolve()),
                        '--binutils', str(args.binutils.resolve()),
                        '--output', str(args.build.resolve()), '--link'], check=True)
        publish(args.build.resolve(), args.output.resolve(), source, args.guest)
    else:
        prebuilt = source/('prebuilt/'+display+'SMBClient')
        verify(prebuilt, source, args.guest)
        if args.output.resolve() != prebuilt.resolve():
            shutil.copytree(prebuilt, args.output)
    print(args.output)


if __name__ == '__main__':
    main()
