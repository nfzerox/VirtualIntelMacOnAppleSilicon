#!/usr/bin/env python3
"""Rebuild Yosemite through High Sierra support with its genuine SDK, without a VM.

Normal app builds use the checked-in payload. This explicit developer build
requires an i386-capable Apple clang and the selected period macOS SDK.
The output must be new; no existing payload or guest is overwritten.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    source = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--guest', choices=('yosemite','elcapitan','sierra','highsierra'), default='yosemite')
    parser.add_argument('--cc', type=Path, required=True)
    parser.add_argument('--wire-sdk', type=Path, default=source/'resources/sdk/MacOSX10.9.sdk')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    minimum={'yosemite':'10.10','elcapitan':'10.11','sierra':'10.12','highsierra':'10.13'}[args.guest]
    output = args.output.resolve()
    if output.exists():
        parser.error('Choose a new output directory')
    output.mkdir(parents=True)
    env = dict(os.environ, VIM_OPENGL_SDK=str(args.sdk.resolve()),
               VIM_OPENGL_CC=str(args.cc.resolve()),
               VIM_OPENGL_CXX=str(args.cc.resolve())+'++',
               VIM_OPENGL_WIRE_SDK=str(args.wire_sdk.resolve()),
               VIM_OPENGL_MIN_VERSION=minimum,
               VIM_OPENGL_AGENT_BUILD=str(output/'Build/Tools'),
               VIM_GUEST_GL_PAIR_BUILD=str(output/'Build/GL'),
               VIM_PASSTHROUGH_OPENGL_BUILD=str(output/'Build/Companion'))
    def run(*command):
        subprocess.run([str(item) for item in command], env=env, check=True)
    for script in ('build-opengl-guest-agent', 'build-opengl-guest-gl-pair', 'build-opengl-ga'):
        run('/bin/bash', source/'scripts'/script)
    for family in ('Kernel', 'Audio'):
        command = [sys.executable, source/'scripts/build-opengl-kernel.py',
                   '--sdk', args.sdk, '--cc', args.cc, '--output', output/family]
        if family == 'Audio':
            command.append('--audio-only')
        run(*command)
        shutil.rmtree(output/family/'Objects')
    run(sys.executable, source/'scripts/package-opengl-guest-support.py',
        '--guest', args.guest, '--tools', output/'Build/Tools',
        '--gl-pair', output/'Build/GL', '--ga', output/'Build/Companion/GA/VMsvga2GA',
        '--output', output/'GuestTools')
    print(args.guest+' support rebuilt:', output)


if __name__ == '__main__':
    main()
