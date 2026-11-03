#!/usr/bin/env python3
"""Rebuild the ten shipping OpenGL pairs and 2D companions from shared source.

Only these three owned libraries are refreshed: existing guest controls, kernel
drivers, audio, system frameworks and firmware are left alone. Ordinary app
builds still consume the checked-in payloads and do not need historical SDKs.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys


GUESTS = ('tiger', 'leopard', 'snowleopard', 'lion', 'mountainlion',
          'mavericks', 'yosemite', 'elcapitan', 'sierra', 'highsierra')
LIBRARIES = ('GLEngine', 'libVIMPassthroughOpenGL.dylib',
             'VMsvga2GA.plugin/Contents/MacOS/VMsvga2GA')


def verify_libraries(stage, guest):
    for relative in LIBRARIES:
        binary = stage/relative
        arches = ['i386'] if guest == 'tiger' else ['x86_64', 'i386']
        for arch in arches:
            subprocess.run(['lipo', binary, '-verify_arch', arch], check=True)
        if guest != 'tiger':
            signature = ['codesign', '--verify', '--strict']
            if relative.startswith('VMsvga2GA.plugin/'):
                # Match package-opengl-guest-support: verify the Mach-O without
                # demanding an unshipped bundle resource envelope.
                signature.append('--ignore-resources')
            subprocess.run([*signature, binary], check=True)


def publish(root, directory, guests):
    # Validate all selected stages before replacing any checked-in library.
    for guest in guests:
        verify_libraries(directory/'Libraries'/guest, guest)
    for guest in guests:
        target = root/'prebuilt/PassthroughOpenGL'/guest/'GuestTools'
        for relative in LIBRARIES:
            shutil.copy2(directory/'Libraries'/guest/relative, target/relative)
    print('Published the three owned libraries for: '+', '.join(guests))


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    destination = parser.add_mutually_exclusive_group(required=True)
    destination.add_argument('--output', type=Path,
                        help='New build directory; per-guest logs and staged libraries go here')
    destination.add_argument('--publish-from', type=Path,
                             help='Publish an existing candidate directory, without rebuilding it')
    parser.add_argument('--guest', action='append', choices=GUESTS,
                        help='Repeat to rebuild a subset; default: all ten')
    parser.add_argument('--cc', type=Path, default=os.environ.get('VIM_OPENGL_CC',
        root/'resources/xcode/XcodeMojave.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang'))
    parser.add_argument('--sdk', type=Path, default=os.environ.get('VIM_OPENGL_SDK',
                        root/'resources/sdk/MacOSX10.9.sdk'), help='Canonical 10.9 compile/wire SDK')
    for name, description in (
        ('tiger-sdk', 'Genuine 10.4u link SDK'),
        ('tiger-ld', 'Xcode 3.2.6 period linker (x86_64-hosted)'),
        ('leopard-sdk', '10.5 SDK: audited 759-slot Tiger / 757-slot Leopard layout'),
        ('snowleopard-sdk', '10.6 SDK: audited 764-slot layout'),
        ('mountainlion-sdk', '10.8 SDK: audited 972-slot layout')):
        parser.add_argument('--'+name, type=Path,
                            default=os.environ.get('VIM_OPENGL_'+name.replace('-', '_').upper()),
                            help=description)
    parser.add_argument('--publish', action='store_true',
                        help='After all builds succeed, replace only the three libraries in prebuilt')
    args = parser.parse_args()
    selected = [name for name in GUESTS if not args.guest or name in args.guest]
    if args.publish_from:
        if args.publish:
            parser.error('--publish-from already selects publication')
        publish(root, args.publish_from.resolve(), selected)
        return
    needed = {'cc', 'sdk'}
    if 'tiger' in selected:
        needed.update(('tiger_sdk', 'tiger_ld', 'leopard_sdk'))
    for name in ('leopard', 'snowleopard', 'mountainlion'):
        if name in selected:
            needed.add(name+'_sdk')
    # Check all selected inputs before spending time on the first compilation.
    for name in sorted(needed):
        path = getattr(args, name)
        if not path or not path.exists():
            parser.error('Supply an existing --'+name.replace('_', '-')+' input')
    if args.output.exists():
        parser.error('--output must be a new directory')
    for name in ('cc', 'tiger_ld'):
        if name in needed and not os.access(getattr(args, name), os.X_OK):
            parser.error('--'+name.replace('_', '-')+' must be executable')
    args.output = args.output.resolve()
    args.output.mkdir(parents=True)
    env = os.environ.copy()
    # A release refresh must not accidentally inherit profiling/diagnostic builds.
    env.pop('VIM_PASSTHROUGH_OPENGL_CLIENT_PROFILE', None)
    env.pop('VIM_ROSETTA_RECOVERY_DIAGNOSTICS', None)
    env.update(VIM_OPENGL_CC=str(args.cc.resolve()),
               VIM_OPENGL_CXX=str(args.cc.resolve())+'++',
               VIM_OPENGL_SDK=str(args.sdk.resolve()),
               VIM_OPENGL_WIRE_SDK=str(args.sdk.resolve()))

    for guest in selected:
        build = args.output/'Builds'/guest
        log_path = args.output/(guest+'.log')
        print('Building '+guest+'; log: '+str(log_path), flush=True)
        with log_path.open('w') as log:
            if guest == 'tiger':
                command = [sys.executable, root/'scripts/build-tiger-opengl.py',
                           '--sdk', args.tiger_sdk.resolve(), '--canonical-sdk', args.sdk.resolve(),
                           '--dispatch-sdk', args.leopard_sdk.resolve(), '--runtime-dispatch-count', '759',
                           '--cc', args.cc.resolve(), '--ld', args.tiger_ld.resolve(), '--output', build]
                subprocess.run(list(map(str, command)), env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
                ga = build/LIBRARIES[2]
            else:
                dispatch = getattr(args, guest+'_sdk', None) if guest in (
                    'leopard', 'snowleopard', 'mountainlion') else args.sdk
                target_env = dict(env, VIM_OPENGL_MIN_VERSION='10.'+str(4+GUESTS.index(guest)),
                                  VIM_OPENGL_DISPATCH_SDK=str(dispatch.resolve()),
                                  VIM_GUEST_GL_PAIR_BUILD=str(build),
                                  VIM_PASSTHROUGH_OPENGL_BUILD=str(build))
                for recipe in ('build-opengl-guest-gl-pair', 'build-opengl-ga'):
                    subprocess.run([root/'scripts'/recipe], env=target_env,
                                   stdout=log, stderr=subprocess.STDOUT, check=True)
                ga = build/'GA/VMsvga2GA'
        stage = args.output/'Libraries'/guest
        for relative, source in zip(LIBRARIES, (build/'GLEngine', build/'libVIMPassthroughOpenGL.dylib', ga)):
            destination = stage/relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, destination)
        verify_libraries(stage, guest)
        print('Built '+guest, flush=True)
    if args.publish:
        # Deliberately no source sealing/checksum manifest. Power users can still
        # replace these libraries. Publish only after every requested build passed.
        publish(root, args.output, selected)
    else:
        print('Candidates only; no checked-in payload or guest was modified.')


if __name__ == '__main__':
    main()
