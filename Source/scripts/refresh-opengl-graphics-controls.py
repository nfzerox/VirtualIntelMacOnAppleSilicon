#!/usr/bin/env python3
"""Refresh only period graphics installers/status tools; no VM or OS install."""
import argparse
from pathlib import Path
import shutil
import subprocess


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True, help='New candidate directory')
    parser.add_argument('--publish', action='store_true', help='Copy verified tools into prebuilt')
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    cc = root/'resources/xcode/XcodeMojave.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang'
    old_ld = root/'resources/xcode/Xcode3.2.6-CLI-Tools/usr/bin/ld'
    stages = {}

    def run(command):
        subprocess.run(list(map(str, command)), check=True)

    for family, version, sdk_name in (
            ('modern', '10.7', '10.9'), ('snowleopard', '10.6', '10.6'),
            ('leopard', '10.5', '10.5'), ('tiger', '10.4', '10.4u')):
        stage = output/family
        stage.mkdir()
        sdk = root/('resources/sdk/MacOSX'+sdk_name+'.sdk')
        arch = 'i386' if family == 'tiger' else 'x86_64'
        flags = ['-arch', arch, '-isysroot', sdk, '-mmacosx-version-min='+version,
                 '-O2', '-fno-objc-arc', '-fno-builtin-memchr', '-Wall', '-Wextra',
                 '-Wno-deprecated-declarations', '-Wno-deprecated-objc-isa-usage',
                 '-Wno-deprecated-objc-pointer-introspection']
        objects = []
        if family != 'modern':
            flags += ['-include', root/'tools/opengl-period-foundation.h']
            for unit in ('tools/opengl-period-foundation.m', 'vendor/source/JSONKit/JSONKit.m'):
                obj = stage/(Path(unit).stem+'.o')
                run([cc, *flags, '-c', root/unit, '-o', obj])
                objects.append(obj)
        names = ('opengl-tiger-graphics', 'opengl-setup-status') if family == 'tiger' else (
            'opengl-install-bootstrap', 'opengl-setup-status')
        for name in names:
            binary = stage/name
            frameworks = ['Foundation', 'CoreFoundation', 'IOKit', 'SystemConfiguration',
                          'DiskArbitration', 'CoreServices', 'Security']
            if family == 'modern':
                frameworks.append('ServiceManagement')
            libraries = [word for framework in frameworks for word in ('-framework', framework)]
            if family == 'tiger':
                obj = stage/(name+'.o')
                run([cc, *flags, '-c', root/('tools/'+name+'.m'), '-o', obj])
                run([old_ld, '-arch', 'i386', '-macosx_version_min', '10.4',
                     '-syslibroot', sdk, '-no_uuid', '-lcrt1.o', obj, *objects,
                     *libraries, '-lgcc_s.10.4', '-lSystem', '-o', binary])
            else:
                run([cc, *flags, root/('tools/'+name+'.m'), *objects, *libraries, '-o', binary])
                run(['codesign', '--force', '--sign', '-', '--digest-algorithm=sha1,sha256', binary])
                run(['codesign', '--verify', '--strict', binary])
            run(['lipo', binary, '-verify_arch', arch])
            if family == 'modern':
                guests = ('lion', 'mountainlion', 'mavericks', 'yosemite', 'elcapitan', 'sierra', 'highsierra')
            else:
                guests = (family,)
            for guest in guests:
                stages[(guest, name)] = binary
        print('Built '+family+' graphics controls', flush=True)
    # Publish only after every period build passed. No new checksum manifest.
    if args.publish:
        for (guest, name), binary in stages.items():
            destination = root/'prebuilt/PassthroughOpenGL'/guest/'GuestTools'/name
            shutil.copy2(binary, destination)
            destination.chmod(0o755)
        print('Published period graphics installers/status tools')


if __name__ == '__main__':
    main()
