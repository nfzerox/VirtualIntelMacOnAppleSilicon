#!/usr/bin/env python3
"""Build one system OpenGL framework: Catalina x86_64 + native Mojave i386.

No facade, private install names, resource-path patches, or runtime routing.
"""
import argparse
import pathlib
import runpy
import shutil
import subprocess
import sys
from pvg_macho import require
import importlib.util

SYSTEM = '/System/Library/Frameworks/OpenGL.framework'


def copy_framework(source, destination):
    destination.mkdir(exist_ok=True, parents=True)
    for path in sorted(source.rglob('*')):
        target = destination/path.relative_to(source)
        if path.is_symlink():
            if target.is_symlink():
                require(target.readlink() == path.readlink(), 'incompatible framework link')
            elif not target.exists():
                target.symlink_to(path.readlink())
        elif path.is_dir():
            target.mkdir(exist_ok=True)
        elif path.is_file():
            shutil.copyfile(path, target)
            target.chmod(0o755 if path.stat().st_mode & 0o111 else 0o644)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('catalina_framework', type=pathlib.Path)
    parser.add_argument('mojave_framework', type=pathlib.Path)
    parser.add_argument('output', type=pathlib.Path)
    args = parser.parse_args()
    require(not args.output.exists(), 'output already exists')
    for source in (args.catalina_framework, args.mojave_framework):
        require((source/'Versions/A/OpenGL').is_file(), 'full native/donor frameworks required')
    destination = args.output/'payload'/SYSTEM.lstrip('/')
    # Keep i386-only resources/plugins; donor supplies shared resources and
    # 64-bit images, then each native i386 slice is restored below.
    copy_framework(args.mojave_framework, destination)
    copy_framework(args.catalina_framework, destination)
    manifest = destination/'Versions/A/Resources/PVGDonorFiles.txt'
    manifest.write_text(''.join(str(p.relative_to(args.catalina_framework))+'\n'
                               for p in sorted(args.catalina_framework.rglob('*'))
                               if p.is_file() and not p.is_symlink()))
    merger = runpy.run_path(str(pathlib.Path(__file__).with_name('stage-opengl-universal.py')))
    merger['merge'](str(args.mojave_framework), str(destination))
    spec = importlib.util.spec_from_file_location('donor_signing', pathlib.Path(__file__).with_name('sign-donor.py'))
    signing = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(signing)
    for path in sorted(destination.rglob('*')):
        if not path.is_file() or path.is_symlink():
            continue
        with path.open('rb') as f:
            magic = f.read(4)
        if magic in (b'\xcf\xfa\xed\xfe', b'\xce\xfa\xed\xfe', b'\xca\xfe\xba\xbe', b'\xca\xfe\xba\xbf'):
            relative = path.relative_to(destination)
            original = args.catalina_framework/relative
            if not original.is_file():
                original = args.mojave_framework/relative
            if relative.as_posix() != 'Versions/A/Libraries/CVMServer':
                signing.sign(original, path)
                continue
            sign = ['codesign', '-f', '-s', '-', '--preserve-metadata=entitlements']
            if path.relative_to(destination).as_posix() == 'Versions/A/Libraries/CVMServer':
                # AMFI selector 3 requires Apple's original JIT authorization.
                # Dropping it silently breaks OpenCL CPU and CI software renders.
                sign += ['--entitlements', str(pathlib.Path(__file__).resolve().parent.parent/
                                               'support/CVMServer.entitlements.plist')]
            subprocess.check_call(sign + [str(path)])
    subprocess.check_call(['codesign', '-f', '-s', '-', '--preserve-metadata=entitlements', str(destination)])
    print('Staged universal system OpenGL:', destination)


if __name__ == '__main__':
    try:
        main()
    except (ValueError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
