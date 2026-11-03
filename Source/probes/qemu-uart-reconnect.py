#!/usr/bin/env python3
"""Verify a closed Unix peer releases a paused QEMU UART with unread RX data."""

import argparse
import json
from pathlib import Path
import socket
import subprocess
import tempfile
import time


def connect(path, process):
    deadline = time.monotonic() + 5
    while True:
        probe = socket.socket(socket.AF_UNIX)
        try:
            probe.connect(str(path))
            return probe
        except OSError as error:
            probe.close()
            if (not isinstance(error, (FileNotFoundError, ConnectionRefusedError))
                    or process.poll() is not None or time.monotonic() >= deadline):
                raise
            time.sleep(.01)


def verify(qemu, bios):
    with tempfile.TemporaryDirectory(prefix="vim-uart-") as temporary:
        test, agent, monitor = (Path(temporary) / name for name in ("test", "agent", "qmp"))
        process = subprocess.Popen([
            str(qemu), "-L", str(bios), "-machine", "q35", "-accel", "tcg",
            "-S", "-m", "128", "-nodefaults", "-display", "none", "-monitor", "none",
            "-qtest-log", "/dev/null", "-qtest", f"unix:{test},server=on,wait=off",
            "-qmp", f"unix:{monitor},server=on,wait=off",
            "-chardev", f"socket,id=agent,path={agent},server=on,wait=off",
            "-device", "isa-serial,chardev=agent,index=1",
        ])
        try:
            with (connect(test, process) as control, control.makefile("rwb", buffering=0) as qtest,
                  connect(monitor, process) as management, management.makefile("rwb", buffering=0) as qmp):
                control.settimeout(5)
                management.settimeout(5)

                def command(line):
                    qtest.write((line + "\n").encode())
                    reply = qtest.readline().decode().split()
                    assert reply[0] == "OK", (line, reply)
                    return int(reply[1], 0) if len(reply) == 2 else None

                def execute(name):
                    qmp.write((json.dumps({"execute": name}) + "\n").encode())
                    while True:
                        reply = json.loads(qmp.readline())
                        if "event" not in reply:
                            return reply["return"]

                assert "QMP" in json.loads(qmp.readline())
                execute("qmp_capabilities")

                def connected():
                    current = next(item for item in execute("query-chardev") if item["label"] == "agent")
                    return not current["filename"].startswith("disconnected:")

                # Default non-FIFO mode has a single unread receive byte.
                # Read LSR only, never RBR, until both peers have been served.
                with connect(agent, process) as old_peer:
                    old_peer.sendall(b"x" * 64)
                    deadline = time.monotonic() + 2
                    while not command("inb 0x2fd") & 1:
                        assert time.monotonic() < deadline, "initial peer never filled UART RX"
                        time.sleep(.01)
                    assert connected(), "ordinary unread data disconnected a live peer"

                # No guest read or write is allowed to bail out the backend:
                # the independent socket hangup watch must release it.
                deadline = time.monotonic() + 2
                while connected():
                    assert time.monotonic() < deadline, "dead client still owns UART"
                    time.sleep(.01)

                with connect(agent, process) as new_peer:
                    new_peer.settimeout(.05)
                    deadline = time.monotonic() + 2
                    # connect() alone only proves the OS queued the new client.
                    # Actual UART TX must be delivered without draining RX.
                    while True:
                        command("outb 0x2f8 0x61")
                        try:
                            assert new_peer.recv(1) == b"a"
                            break
                        except socket.timeout:
                            assert time.monotonic() < deadline, "dead client still owns UART"
                    assert command("inb 0x2f8") == ord("x"), "unread UART byte was discarded"
                print("PASS closed peer released the backpressured UART")
        finally:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("qemu", type=Path)
    parser.add_argument("bios", type=Path)
    args = parser.parse_args()
    verify(args.qemu, args.bios)
