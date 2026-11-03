#!/usr/bin/env python3
"""Qualify the local HVF diagnostic's arguments using blank, diskless machines.

Pass an explicit just-built x64 QEMU. Never connects to the app's QMP socket,
boots an OS, touches a VM disk or changes host security. Each test explicitly
quits its own stopped 32-MiB machine through its private stdin control pipe.
"""
import argparse
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('qemu', type=Path)
a = p.parse_args()
qemu = a.qemu.resolve(strict=True)
cases = [('0x60ec', None, False), ('0x60e0', '0xffe0', False),
         ('0x60ec', '0xffe0', True), ('0x60e0', '0xffe1', True),
         (None, '0xffe0', True), ('0x60e0', '0', True),
         ('0x60e0', '0x10000', True), ('0x60e0', 'bad', True)]
for port, mask, refused in cases:
    environment = dict(os.environ)
    for key in ('VIM_HVF_IO_LATENCY_PORT', 'VIM_HVF_IO_LATENCY_MASK'):
        environment.pop(key, None)
    if port is not None:
        environment['VIM_HVF_IO_LATENCY_PORT'] = port
    if mask is not None:
        environment['VIM_HVF_IO_LATENCY_MASK'] = mask
    result = subprocess.run([str(qemu), '-machine', 'none', '-accel', 'hvf',
        '-m', '32M', '-nodefaults', '-display', 'none', '-S', '-qmp', 'stdio'],
        env=environment, input='{"execute":"qmp_capabilities"}\n{"execute":"quit"}\n',
        text=True, capture_output=True, timeout=10)
    # The timeout owns ONLY this blank self-test process, never the research
    # guest or a setup/graphics job. A harness timeout is NOT a passing case.
    if bool(result.returncode) != refused:
        raise RuntimeError('Unexpected diagnostic admission: ' + repr((port, mask, result.stderr)))
    if refused:
        if 'VIM_HVF_IO_LATENCY_MASK' not in result.stderr:
            raise RuntimeError('An unrelated initialization failure is not a valid refusal')
    elif 'VIM_HVF_IO_LATENCY armed' not in result.stderr or '"return"' not in result.stdout:
        raise RuntimeError('Missing positive HVF initialization/control coverage')
    print('PASS', port, mask, 'refused' if refused else 'admitted/quit')
