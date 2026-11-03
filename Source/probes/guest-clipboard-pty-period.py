#!/usr/bin/python
"""Python 2.5+ finite UART check; never authenticates or accesses a pasteboard."""
import os
import re
import select
import subprocess
import sys
import tempfile
import time

directory = tempfile.mkdtemp(prefix='vim-clipboard-pty-')
token = os.path.join(directory, 'token')
descriptor = os.open(token, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0600)
os.write(descriptor, 'a' * 64)
os.close(descriptor)
master, slave = os.openpty()
os.chmod(os.ttyname(slave), 0600)
child = None
try:
    child = subprocess.Popen([sys.argv[1], '--serial', os.ttyname(slave), token])
    deadline = time.time() + 5
    line = ''
    while '\n' not in line and time.time() < deadline:
        if select.select([master], [], [], max(0, deadline - time.time()))[0]:
            line += os.read(master, 1024)
            if len(line) > 1024:
                raise RuntimeError('Oversized private PTY hello')
    # Only this fixed synthetic hello, in any JSON dictionary order. No eval,
    # dependencies, secrets or general-purpose JSON approximation on Python2.5.
    compact = re.sub(r'\s', '', line)
    fields = ['"type":"hello"', '"protocol":2', '"token":"' + 'a' * 64 + '"']
    if not compact.startswith('{') or not compact.endswith('}') or sorted(compact[1:-1].split(',')) != sorted(fields):
        raise RuntimeError('Unexpected private PTY hello')
    began = time.time()
    os.write(master, '{"type":"text","sequence":1,"text":"must not be applied"}\n')
    while child.poll() is None and time.time() - began < 3:
        time.sleep(.02)
    if child.poll() != 75:
        raise RuntimeError('Read event did not reject pre-authentication text')
    os.chmod(token, 0644)
    child = subprocess.Popen([sys.argv[1], '--serial', os.ttyname(slave), token])
    began = time.time()
    while child.poll() is None and time.time() - began < 3:
        time.sleep(.02)
    if child.poll() != 77:
        raise RuntimeError('Public token was not rejected')
    print('PASS private PTY readiness, pre-authentication text and public token rejected; no pasteboard access')
finally:
    if child is not None and child.poll() is None:
        # Popen.kill arrived in 2.6; the reference Leopard ships Python2.5.
        import signal
        os.kill(child.pid, signal.SIGKILL)
        child.wait()
    os.close(master)
    os.close(slave)
    os.unlink(token)
    os.rmdir(directory)
