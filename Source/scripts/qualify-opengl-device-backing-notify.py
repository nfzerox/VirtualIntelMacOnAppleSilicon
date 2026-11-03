#!/usr/bin/env python3
"""Local research only: build/run the actual completion-queue lifetime harness.

Reuses a configured QEMU build's compiler/header flags, not a new dependency
setup. Does not build/boot QEMU, mutate a VM, or qualify actual GPU/PCI timing.
"""
import argparse
import json
from pathlib import Path
import shlex
import subprocess

source = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--qemu-build', type=Path, default=source/'Build/qemu/x86_64')
parser.add_argument('--output', type=Path, default=source/'Build/PassthroughOpenGLResearch/BackingNotify/opengl-device-backing-notify')
parser.add_argument('--sanitize', action='store_true')
args = parser.parse_args()
entries = json.loads((args.qemu_build/'compile_commands.json').read_text())
entry = next(item for item in entries if item['file'].endswith('/hw/display/vmware_vga.c'))
command = shlex.split(entry['command'])
filtered = []
skip = False
for argument in command:
    if skip:
        skip = False
        continue
    if argument in ('-o', '-MQ', '-MF'):
        skip = True
    elif argument not in ('-MD', '-c') and not argument.endswith('/hw/display/vmware_vga.c'):
        filtered.append(argument)
args.output = args.output.resolve()
args.output.parent.mkdir(parents=True, exist_ok=True)
filtered += ['-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter']
if args.sanitize:
    filtered += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
filtered += [str(source/'probes/opengl-device-backing-notify.c'), '-o', str(args.output)]
subprocess.run(filtered, cwd=entry['directory'], check=True)
subprocess.run([str(args.output)], check=True)
