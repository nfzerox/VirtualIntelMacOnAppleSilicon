#!/usr/bin/env python3
"""Rebuild bundled guest tools without modifying checked-in fast-build inputs."""
import argparse
import hashlib
from pathlib import Path
import plistlib
import subprocess
import shutil

TOOLS = ('guest-image-patch', 'guest-status', 'guest-macho', 'guest-prepare', 'guest-install', 'guest-progress', 'guest-display', 'guest-clipboard')

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[1]
    args.output.mkdir(parents=True, exist_ok=False)
    receipt = {'FormatVersion': 1, 'Architecture': 'x86_64',
               'MinimumGuestVersion': '10.14', 'Tools': {}}
    for name in TOOLS:
        script = source/'scripts'/('build-'+name)
        code = source/'tools'/(name+'.m')
        binary = args.output/name
        subprocess.run(['/bin/zsh', str(script), str(binary)], check=True)
        architecture = subprocess.check_output(['/usr/bin/lipo', '-archs', str(binary)], text=True).strip()
        if architecture != 'x86_64':
            raise RuntimeError(f'{name}: unexpected architecture {architecture}')
        subprocess.run(['/usr/bin/codesign', '--verify', '--strict', str(binary)], check=True)
        receipt['Tools'][name] = {'SourceSHA256': digest(code), 'BuildScriptSHA256': digest(script),
                                  'BinarySHA256': digest(binary),
                                  'SourceFiles': {str(p.relative_to(source)): digest(p)
                                                  for p in [code, *sorted(code.parent.glob('guest-*.h')),
                                                            *(sorted((source/'vendor/xz-api').rglob('*.h')) if name == 'guest-prepare' else []),
                                                            *([source/'support/GuestProgress-Info.plist'] if name == 'guest-progress' else []),
                                                            *([code.parent/'VIMFileTransaction.m', code.parent/'VIMFileTransaction.h',
                                                               source/'scripts/generate-guest-install-targets.py',
                                                               source/'support/install-targets.txt', source/'support/install-targets-mojave.txt',
                                                               source/'support/install-targets-bigsur.txt']
                                                              if name == 'guest-install' else [])]}}
    surface = args.output/'PVGGLIOKit.dylib'
    allocator = source/'vendor/guest-codesign/codesign_allocate'
    expected = '247071c760a11a7994e01e075d92d8957b28123aaf60763962cfdb821d8af447'
    if digest(allocator) != expected:
        raise RuntimeError('Legacy guest code-signature allocator checksum mismatch')
    shutil.copyfile(allocator, args.output/'codesign_allocate')
    (args.output/'codesign_allocate').chmod(0o755)
    receipt['Tools']['codesign_allocate'] = {'BinarySHA256': expected,
        'SourceFiles': {str(p.relative_to(source)): digest(p) for p in
                        (allocator, allocator.parent/'APPLE_LICENSE')}}
    subprocess.run(['/bin/zsh', str(source/'scripts/build-mojave-surface-helper'), str(surface)], check=True)
    receipt['MojaveSurfaceHelperSHA256'] = digest(surface)
    for name, script, extra in (
            ('PVGBigSurMetalCompat.dylib', 'build-bigsur-metal-compat', []),
            ('PVGOpenGLMetal.dylib', 'build-opengl-metal-probe', ['--bigsur'])):
        binary=args.output/name
        subprocess.run(['/bin/zsh', str(source/'scripts'/script), str(binary), *extra], check=True)
        subprocess.run(['/usr/bin/codesign', '--verify', '--strict', str(binary)], check=True)
        receipt['Tools'][name]={'BinarySHA256':digest(binary),
            'SourceFiles':{str(p.relative_to(source)):digest(p) for p in
                [source/'scripts'/script, *sorted((source/'compat/PVGOpenGLProbe').glob('*'))] if p.is_file()}}
    (args.output/'BuildReceipt.plist').write_bytes(plistlib.dumps(receipt))
    print(f'Built all guest tools: {args.output}')

if __name__ == '__main__':
    main()
