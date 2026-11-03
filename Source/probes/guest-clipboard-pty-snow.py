#!/usr/bin/python
"""Python2.6-compatible finite read-event check; never authenticates or reads a pasteboard."""
import json
import os
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
os.fchmod(slave, 0600)
child = None
try:
    child = subprocess.Popen([sys.argv[1], '--serial', os.ttyname(slave), token])
    if not select.select([master], [], [], 5)[0]:
        raise RuntimeError('No private PTY hello')
    hello = json.loads(os.read(master, 1024))
    if hello != {'type': 'hello', 'protocol': 2, 'token': 'a' * 64}:
        raise RuntimeError('Unexpected private PTY hello')
    began = time.time()
    os.write(master, '{"type":"text","sequence":1,"text":"must not be applied"}\n')
    while child.poll() is None and time.time() - began < 3:
        time.sleep(.02)
    if child.poll() != 75:
        raise RuntimeError('Read event did not reject pre-authentication text within three seconds')
    print('PASS private PTY read event, immediate pre-authentication rejection; no pasteboard access')
finally:
    if child is not None and child.poll() is None:
        child.kill()
        child.wait()
    os.close(master)
    os.close(slave)
    os.unlink(token)
    os.rmdir(directory)
