#!/usr/bin/env python3
"""Read-only guest-info probe of an app-owned private QGA Unix socket."""
import argparse
import json
import os
import pathlib
import socket
import stat
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('socket', type=pathlib.Path)
args = parser.parse_args()
endpoint = args.socket
parent = endpoint.parent.lstat()
node = endpoint.lstat()
if not stat.S_ISDIR(parent.st_mode) or parent.st_uid != os.getuid() or parent.st_mode & 0o077:
    parser.error('the socket parent must be a private directory owned by this user')
if not stat.S_ISSOCK(node.st_mode) or node.st_uid != os.getuid():
    parser.error('expected an app-owned Unix socket, not a link or regular file')
with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
    connection.settimeout(10)
    connection.connect(str(endpoint))
    connection.sendall(b'{"execute":"guest-info"}\n')
    pending = bytearray()
    deadline = time.monotonic()+10
    while time.monotonic()<deadline:
        connection.settimeout(max(0.01, deadline-time.monotonic()))
        chunk = connection.recv(4096)
        if not chunk:
            raise SystemExit('Guest-agent channel closed before a reply')
        pending.extend(chunk)
        if len(pending)>1024*1024:
            raise SystemExit('Guest-agent reply exceeded 1 MiB')
        while b'\n' in pending:
            line, _, tail = pending.partition(b'\n')
            pending = bytearray(tail)
            if not line.strip():
                continue
            response = json.loads(line)
            if not isinstance(response, dict):
                raise SystemExit('Invalid guest-agent reply')
            if 'event' in response:
                continue
            print(json.dumps(response, indent=2))
            raise SystemExit(0 if 'return' in response else 1)
    raise SystemExit('Guest-agent reply timed out')
