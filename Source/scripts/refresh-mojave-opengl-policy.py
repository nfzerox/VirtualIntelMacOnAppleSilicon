#!/usr/bin/env python3
"""Refresh fast-build Mojave GL signing/AppKit policy without rebuilding donors.

Input is an existing verified release directory. Output must be new. Scratch
is retained on the selected large volume for inspection. No guest/host system
files, native seed binaries, or unrelated release payload members are changed.
"""
import argparse
import hashlib
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import tempfile
import runpy


def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as stream:
        for data in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(data)
    return value.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('release', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--scratch', type=Path, required=True)
    parser.add_argument('--prepare-tool', type=Path, required=True)
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[1]
    release = args.release.resolve()
    archive = runpy.run_path(str(source/'scripts/payload-archive.py'))['release_archive'](release)
    checks = (release / 'SHA256SUMS').read_text().splitlines()
    expected = [line.split()[0] for line in checks
                if len(line.split()) == 2 and line.split()[1] == archive.name]
    if expected != [digest(archive)]:
        raise ValueError('Source archive does not match its release manifest')
    if args.output.exists():
        raise ValueError('Output already exists; choose a new release directory')
    work = Path(tempfile.mkdtemp(prefix='Mojave-GL-Policy-', dir=args.scratch.resolve()))
    extracted = work / 'extracted'
    subprocess.run([str(args.prepare_tool.resolve()), '--extract', str(archive), str(extracted)], check=True)
    payload = extracted / 'payload'
    framework = payload / 'System/Library/Frameworks/OpenGL.framework'
    cvms = framework / 'Versions/A/Libraries/CVMServer'
    entitlements = source / 'support/CVMServer.entitlements.plist'
    subprocess.run(['codesign', '-f', '-s', '-', '--entitlements', str(entitlements), str(cvms)], check=True)
    subprocess.run(['codesign', '-f', '-s', '-', str(framework)], check=True)
    subprocess.run(['codesign', '--verify', '--deep', '--strict', str(framework)], check=True)
    policy = subprocess.check_output(['codesign', '-d', '--entitlements', ':-', str(cvms)], stderr=subprocess.DEVNULL)
    if plistlib.loads(policy) != plistlib.loads(entitlements.read_bytes()):
        raise ValueError('CVMServer did not retain the exact original Apple JIT policy')
    for app in ('Aperture', 'iPhoto'):
        preference = payload / f'Library/Preferences/com.apple.{app}.plist'
        preference.parent.mkdir(parents=True, exist_ok=True)
        # Canonical guest staging merges the real guest's existing preferences;
        # these are only seeds for fast-build release completeness.
        prepared = work / (app + '.plist')
        subprocess.run([str(args.prepare_tool.resolve()), '--aperture-policy',
                        str(preference), str(prepared)], check=True)
        shutil.copyfile(prepared, preference)
    shutil.copytree(release, args.output, ignore=shutil.ignore_patterns('guest-payload.tar.gz','guest-payload.tar.xz'))
    files = sorted(p for p in payload.rglob('*') if p.is_file() and not p.is_symlink()
                   and p.relative_to(payload).as_posix() != 'manifest.sha256')
    (args.output / 'FILES.sha256').write_text(''.join(
        f'{digest(p)}  ./{p.relative_to(payload).as_posix()}\n' for p in files))
    env = dict(os.environ, COPYFILE_DISABLE='1')
    archive_name='guest-payload.tar.xz'
    subprocess.run(['tar', '--exclude', 'payload/manifest.sha256', '-cJf',
                    str(args.output.resolve() / archive_name), '-C', str(extracted), 'payload'], check=True, env=env)
    revision = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
    with (args.output / 'RELEASE.txt').open('a') as stream:
        stream.write(f'\nGL compatibility policy refresh: {revision} (plus working-tree policy refresh script).\n'
                     'Restored exact Apple CVMServer JIT entitlements; enabled Aperture/iPhoto AppKit root layer backing.\n'
                     'Guest-native staging still merges preferences and re-signs its universal OpenGL framework.\n')
    members = [archive_name, 'FILES.sha256', 'RELEASE.txt']
    members += [p.relative_to(args.output).as_posix() for p in sorted((args.output / 'probes').glob('*')) if p.is_file()]
    (args.output / 'SHA256SUMS').write_text(''.join(f'{digest(args.output / name)}  {name}\n' for name in members))
    print(f'Refreshed release: {args.output}\nInspection scratch retained: {work}')


if __name__ == '__main__':
    main()
