"""Run one bounded SMB authentication trial on the guest's loopback only.

Run under the app-owned root agent with the staged smbd path and a port.
No changes to the native service, shares, password database or launchd.
Compatible with Snow Leopard's Python 2.6.
"""
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import time

if len(sys.argv) not in (3, 4) or os.geteuid() != 0:
    sys.exit(64)
executable = sys.argv[1]
port = int(sys.argv[2])
if not os.path.isabs(executable) or not 1024 <= port <= 65535:
    sys.exit(64)
module = sys.argv[3] if len(sys.argv) == 4 else ''
if module and (not os.path.isabs(module) or any(c.isspace() for c in module)):
    sys.exit(64)
work = tempfile.mkdtemp(prefix='vim-snow-smb-')
child = None
try:
    config = os.path.join(work, 'smb.conf')
    share = os.path.join(work, 'share')
    os.chmod(work, 0o711)
    os.mkdir(share, 0o755)
    os.chmod(share, 0o755)  # root agent deliberately uses a restrictive umask
    marker_path = os.path.join(share, 'readback.txt')
    with open(marker_path, 'w') as marker:
        marker.write('Snow Leopard signed SMB readback\n')
    os.chmod(marker_path, 0o644)
    with open(config, 'w') as output:
        output.write("""[global]
    workgroup = WORKGROUP
    netbios name = VIMSNOWTEST
    security = user
    opendirectory = yes
    auth methods = opendirectory
    preload modules = %(module)s
    passdb backend = odsam
    smb passwd file = %(work)s/smbpasswd
    map to guest = Never
    server signing = mandatory
    interfaces = 127.0.0.1/8
    bind interfaces only = yes
    lock directory = %(work)s
    pid directory = %(work)s
    private dir = %(work)s
    log file = %(work)s/smbd.log
[mac]
    path = %(work)s/share
    read only = yes
    valid users = mac
""" % {'work': work, 'module': module})
    child = subprocess.Popen([executable, '-i', '-s', config, '-p', str(port)])
    deadline = time.time() + 45
    while child.poll() is None and time.time() < deadline:
        time.sleep(.25)
    if child.poll() is None:
        os.kill(child.pid, signal.SIGTERM)
    sys.exit(child.wait())
finally:
    shutil.rmtree(work)
