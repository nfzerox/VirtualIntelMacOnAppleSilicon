"""Run on a settled diagnostic VM. Counts only, not a speed comparison.

Use PVG_SSH_PASSWORD; optional first argument names the output JSON file.
"""
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

root = Path(__file__).resolve().parents[1]
socket = '/tmp/pvg-mojave-oop-qmp.sock'

def snapshot():
    request = {'execute': 'human-monitor-command',
               'arguments': {'command-line': 'info jit'}}
    text = subprocess.check_output(['python3', str(root/'scripts/qemu-qmp.py'),
                                    socket, json.dumps(request)], text=True)
    report = json.loads(text)['return']
    counts = {}
    for cpu, fields in re.findall(r'C lookup reasons CPU (\d+): ([^\r\n]+)', report):
        counts[cpu] = {key: int(value) for key, value in
                       re.findall(r'([a-z-]+)=(\d+)', fields)}
    if not counts or not any(sum(c.values()) for c in counts.values()):
        raise RuntimeError('Nonzero diagnostic counters unavailable')
    return counts

def delta(a, b):
    if a.keys() != b.keys():
        raise RuntimeError('CPU set changed')
    out = {cpu: {k: b[cpu][k]-v for k,v in a[cpu].items()} for cpu in a}
    if any(v < 0 for c in out.values() for v in c.values()):
        raise RuntimeError('Counter reset: discard measurement')
    return out

results = []
for trial in range(2):
    before = snapshot()
    start = time.monotonic()
    p = subprocess.run([str(root/'scripts/ssh-expect'),
                        os.environ.get('PVG_GUEST_SSH', 'ssh://127.0.0.1:22114'), '~/finder-window-time'],
                       env=dict(os.environ, PVG_SSH_TIMEOUT='30',
                                PVG_SSH_CONTROL_PATH='/tmp/pvg-mojave-perf-ssh'),
                       capture_output=True, text=True, timeout=35, check=True)
    elapsed = time.monotonic()-start
    after = snapshot()
    if 'created-window-closed=1' not in p.stdout:
        raise RuntimeError('Probe did not confirm window cleanup')
    time.sleep(elapsed)
    idle = snapshot()
    results.append(dict(trial=trial, interval_seconds=elapsed,
                        window=delta(before,after), idle=delta(after,idle),
                        output=p.stdout))
text = json.dumps(results, indent=2)
if len(sys.argv) == 2:
    with open(sys.argv[1], 'x') as f:
        f.write(text+'\n')
print(text)
