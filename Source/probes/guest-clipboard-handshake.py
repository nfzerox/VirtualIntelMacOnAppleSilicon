#!/usr/bin/env python3
"""Handshake rejection check. Never authenticates or touches the pasteboard."""
import json
import pathlib
import os
import select
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="vim-clipboard-test-") as directory:
    token = pathlib.Path(directory) / "token"
    token.write_text("a" * 64)
    token.chmod(0o600)
    master, slave = os.openpty()
    device = os.ttyname(slave)
    os.fchmod(slave, 0o600)
    try:
        child = subprocess.Popen([sys.argv[1], "--serial", device, str(token)])
        try:
            assert select.select([master], [], [], 5)[0], "No serial hello"
            hello = json.loads(os.read(master, 1024))
            assert hello == {"type": "hello", "protocol": 2, "token": "a" * 64}
            os.write(master, b'{"type":"text","sequence":1,"text":"must not be applied"}\n')
            assert child.wait(timeout=5) == 75
        finally:
            if child.poll() is None:
                child.kill()
                child.wait()
        token.chmod(0o644)
        denied = subprocess.run([sys.argv[1], "--serial", device, str(token)], timeout=5)
        assert denied.returncode == 77
    finally:
        os.close(master)
        os.close(slave)
print("PASS: authenticated hello framing, pre-authentication text rejected, public token rejected; no pasteboard read/write")
