#!/usr/bin/env python3
"""Same-boot, alternating context-cache experiment; run on a settled test VM.

Requires PVG_SSH_PASSWORD and the stopped-only QOM property in the candidate.
Terminates only the test Reminders application between measurements.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument('--socket', default='/tmp/pvg-mojave-oop-qmp.sock')
p.add_argument('--guest', default=os.environ.get('PVG_GUEST_SSH', 'ssh://127.0.0.1:22114'))
p.add_argument('--output', required=True)
args = p.parse_args()

def qmp(command, **arguments):
    request = {'execute': command}
    if arguments:
        request['arguments'] = arguments
    result = subprocess.run(['python3', str(ROOT/'scripts/qemu-qmp.py'),
                             args.socket, json.dumps(request)],
                            check=True, capture_output=True, text=True, timeout=15)
    return json.loads(result.stdout)

def toggle(enabled):
    qmp('stop')
    try:
        qmp('qom-set', path='/machine', property='pvg-context-jump-cache',
            value=enabled)
        assert qmp('qom-get', path='/machine',
                   property='pvg-context-jump-cache')['return'] == enabled
    finally:
        qmp('cont')

env = dict(os.environ, PVG_SSH_CONTROL_PATH='/tmp/pvg-mojave-perf-ssh',
           PVG_SSH_TIMEOUT='60')
command = '''for pid in $(pgrep -x Reminders); do
if [ "$(ps -p "$pid" -o comm=)" = "/Applications/Reminders.app/Contents/MacOS/Reminders" ]; then kill -TERM "$pid"; fi
done
sleep 1
~/app-launch-window-time-v3 /Applications/Reminders.app || exit $?
~/tcg-short-perf-current'''
with open(args.output, 'x') as out:
    # One warm-up per state followed by balanced measured pairs.
    for index, enabled in enumerate([False, True, False, True, True, False]):
        toggle(enabled)
        time.sleep(2)
        header = f'round={index} enabled={enabled} warmup={index < 2}'
        print(header, flush=True)
        out.write(header+'\n'); out.flush()
        result = subprocess.run([str(ROOT/'scripts/ssh-expect'), args.guest,
                                 command], env=env, stdout=out,
                                stderr=subprocess.STDOUT, timeout=75)
        out.flush()
        if result.returncode:
            raise SystemExit(result.returncode)
