"""Temporary same-boot nice experiment; always restore original priority.
Requires PVG_ADMIN_PASSWORD and PVG_SSH_PASSWORD; never prints either.
"""
import os
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
pid = int(sys.argv[1])
identity = subprocess.check_output(['ps', '-p', str(pid), '-o', 'comm='], text=True).strip()
if not identity.startswith(str(root / 'artifacts/host-arm64/qemu-arm64-')):
    raise SystemExit('Target is not a workspace QEMU binary')
original = int(subprocess.check_output(['ps', '-p', str(pid), '-o', 'ni='], text=True))
password = os.environ['PVG_ADMIN_PASSWORD']
env = dict(os.environ, PVG_SSH_CONTROL_PATH='/tmp/pvg-mojave-perf-ssh', PVG_SSH_TIMEOUT='30')
env.pop('PVG_ADMIN_PASSWORD', None)

def priority(value):
    current = subprocess.check_output(['ps', '-p', str(pid), '-o', 'comm='], text=True).strip()
    if current != identity:
        raise RuntimeError('QEMU target changed')
    subprocess.run(['sudo', '-S', '-p', '', 'renice', str(value), '-p', str(pid)],
                   input=password+'\n', text=True, check=True,
                   stdout=subprocess.DEVNULL, timeout=10)

try:
    for trial, value in enumerate([original, -5, -5, original]):
        priority(value)
        print(f'trial={trial} nice={value}', flush=True)
        subprocess.run([str(root/'scripts/ssh-expect'), os.environ.get('PVG_GUEST_SSH', 'ssh://127.0.0.1:22114'),
                        '~/finder-window-time && ~/tcg-short-perf-current'],
                       env=env, check=True, timeout=40)
finally:
    priority(original)
    print(f'restored nice={original}', flush=True)
