#!/usr/bin/env python3
"""Rebuild bundled guest tools without modifying checked-in fast-build inputs."""
import argparse
from pathlib import Path
import subprocess
import shutil

TOOLS = ('guest-image-patch', 'guest-status', 'guest-macho', 'guest-prepare', 'guest-install', 'guest-progress', 'guest-display', 'guest-clipboard')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[1]
    args.output.mkdir(parents=True, exist_ok=False)
    for name in TOOLS:
        script = source/'scripts'/('build-'+name)
        binary = args.output/name
        subprocess.run(['/bin/zsh', str(script), str(binary)], check=True)
        architecture = subprocess.check_output(['/usr/bin/lipo', '-archs', str(binary)], text=True).strip()
        if architecture != 'x86_64':
            raise RuntimeError(f'{name}: unexpected architecture {architecture}')
        subprocess.run(['/usr/bin/codesign', '--verify', '--strict', str(binary)], check=True)
    for name, script in (('guest-native-installer','build-native-installer'),('guest-create-account','build-modern-account'),
                         ('guest-recovery-agent','build-recovery-agent')):
        subprocess.run(['/bin/zsh',str(source/'scripts'/script),str(args.output/name)],check=True)
    surface = args.output/'PVGGLIOKit.dylib'
    allocator = source/'vendor/guest-codesign/codesign_allocate'
    shutil.copyfile(allocator, args.output/'codesign_allocate')
    (args.output/'codesign_allocate').chmod(0o755)
    subprocess.run(['/bin/zsh', str(source/'scripts/build-mojave-surface-helper'), str(surface)], check=True)
    for name, script, extra in (
            ('PVGBigSurMetalCompat.dylib', 'build-bigsur-metal-compat', []),
            ('PVGOpenGLMetal.dylib', 'build-opengl-metal-probe', ['--bigsur'])):
        binary=args.output/name
        subprocess.run(['/bin/zsh', str(source/'scripts'/script), str(binary), *extra], check=True)
        subprocess.run(['/usr/bin/codesign', '--verify', '--strict', str(binary)], check=True)
    print(f'Built all guest tools: {args.output}')

if __name__ == '__main__':
    main()
