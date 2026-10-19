"""Settled VM only. Balanced same-boot trials; restore both controls on exit."""
import json
import os
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
def q(cmd, **arguments):
    data = subprocess.check_output(['python3',str(root/'scripts/qemu-qmp.py'),
        '/tmp/pvg-mojave-oop-qmp.sock', json.dumps(dict(execute=cmd,arguments=arguments))],text=True)
    return json.loads(data)['return']
def get(prop):
    return q('qom-get',path='/machine',property=prop)
def set_controls(alternate, diagnostics):
    q('stop')
    try:
        for prop,value in [('pvg-alternate-lookup',alternate),('pvg-lookup-diagnostics',diagnostics)]:
            q('qom-set',path='/machine',property=prop,value=value)
            assert get(prop)==value
    finally:
        q('cont')
original = get('pvg-alternate-lookup'),get('pvg-lookup-diagnostics')
env=dict(os.environ,PVG_SSH_TIMEOUT='30',PVG_SSH_CONTROL_PATH='/tmp/pvg-mojave-perf-ssh')
with open(sys.argv[1], 'x') as out:
    try:
        for enabled in (False,True,True,False):
            set_controls(enabled,False)
            for repeat in range(3):
                # First window warms the cache cleared by the toggle.
                result=subprocess.run([str(root/'scripts/ssh-expect'),
                    os.environ.get('PVG_GUEST_SSH', 'ssh://127.0.0.1:22114'),'~/finder-window-time'],
                    env=env,capture_output=True,text=True,timeout=35,check=True)
                if 'created-window-closed=1' not in result.stdout:
                    raise RuntimeError('Window cleanup not confirmed')
                row=dict(enabled=enabled,warmup=repeat==0,output=result.stdout)
                out.write(json.dumps(row)+'\n');out.flush()
                print(json.dumps(row),flush=True)
    finally:
        set_controls(*original)
