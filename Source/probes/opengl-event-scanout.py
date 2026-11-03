#!/usr/bin/env python3
"""Diskless QEMU device test: FIFO notification -> owned app frame, no guest.

Deliberately uses a 1000-ms fallback poll. Event delivery must not wait for it.
No performance claim about macOS/Metal is made by this protocol fixture.
"""
import argparse
import array
import json
import mmap
import os
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('qemu', type=Path)
p.add_argument('bios', type=Path)
p.add_argument('--polling', action='store_true', help='Measure the timer-only comparison')
a = p.parse_args()
wire = struct.Struct('<IIQIIII')
magic = 0x56494d31


def exact(channel, size):
    data = bytearray()
    while len(data) < size:
        part = channel.recv(size-len(data))
        if not part:
            raise AssertionError('Unexpected display disconnect')
        data.extend(part)
    return bytes(data)


with tempfile.TemporaryDirectory(prefix='vim-event-scanout-') as scratch:
    base = Path(scratch)
    display_path, qtest_path = str(base/'display'), str(base/'qtest')
    listener = socket.socket(socket.AF_UNIX)
    listener.bind(display_path)
    listener.listen(1)
    listener.settimeout(5)
    env = dict(os.environ, VIM_INSTALLER_DISPLAY_SOCKET=display_path,
               VIM_DISPLAY_REFRESH_MS='1000',
               VIM_DISPLAY_EVENT_DRIVEN='0' if a.polling else '1')
    for key in ('VIM_DISPLAY_TIMING', 'VIM_OPENGL_VRAM_SERVICE', 'VIM_OPENGL_RENDERER_PID'):
        env.pop(key, None)
    with (base/'qemu.log').open('wb') as log:
        child = subprocess.Popen([str(a.qemu.resolve()), '-L', str(a.bios.resolve()),
                                  '-machine', 'q35', '-accel', 'tcg', '-S', '-m', '128',
                                  '-nodefaults', '-vga', 'vmware', '-global',
                                  'vmware-svga.passthrough-opengl-bridge=on', '-global',
                                  'vmware-svga.vgamem_mb=128', '-display', 'none',
                                  '-monitor', 'none', '-serial', 'none', '-qtest',
                                  f'unix:{qtest_path},server=on,wait=off', '-qtest-log', '/dev/null'],
                                 env=env, stdout=log, stderr=log)
        try:
            channel, _ = listener.accept()
            channel.settimeout(5)
            hello, ancillary, flags, _ = channel.recvmsg(wire.size, socket.CMSG_SPACE(4))
            assert len(hello) == wire.size and wire.unpack(hello) == (magic, 1, 0, 0, 0, 0, 0)
            fds = array.array('i')
            for level, kind, data in ancillary:
                if level == socket.SOL_SOCKET and kind == socket.SCM_RIGHTS:
                    fds.frombytes(data)
            assert len(fds) == 1 and not flags
            frame = mmap.mmap(fds[0], 8192*8192*4, access=mmap.ACCESS_READ)
            os.close(fds[0])
            control = socket.socket(socket.AF_UNIX)
            deadline = time.monotonic()+5
            while True:
                try:
                    control.connect(qtest_path)
                    break
                except (FileNotFoundError, ConnectionRefusedError):
                    assert child.poll() is None and time.monotonic() < deadline
                    time.sleep(.01)
            control.settimeout(5)
            q = control.makefile('rwb', buffering=0)

            def command(text):
                q.write((text+'\n').encode())
                reply = q.readline().decode().strip().split()
                assert reply and reply[0] == 'OK', reply
                return int(reply[1], 0) if len(reply) > 1 else None

            def config(offset, value):
                command(f'outl 0xcf8 {pci | offset:#x}')
                command(f'outl 0xcfc {value:#x}')

            def register(index, value):
                command(f'outl 0xc000 {index:#x}')
                command(f'outl 0xc001 {value:#x}')

            def write(address, value):
                command(f'writel {address:#x} {value:#x}')

            def next_frame(timeout):
                deadline = time.monotonic()+timeout
                while True:
                    channel.settimeout(max(.001, deadline-time.monotonic()))
                    m = wire.unpack(exact(channel, wire.size))
                    assert m[0] == magic and not m[6]
                    if m[1] == 5:
                        exact(channel, m[3]*m[4]*4)
                    elif m[1] == 2:
                        return m
                    else:
                        assert m[1] == 4, m

            def ack(m):
                channel.sendall(wire.pack(magic, 3, m[2], 0, 0, 0, 0))

            def no_frame(timeout):
                try:
                    m = next_frame(timeout)
                except socket.timeout:
                    return
                raise AssertionError(f'Unexpected frame while producer is backpressured/idle: {m}')

            for slot in range(32):
                pci = 0x80000000 | (slot << 11)
                command(f'outl 0xcf8 {pci:#x}')
                if command('inl 0xcfc') == 0x040515ad:
                    break
            else:
                raise AssertionError('VMware PCI device absent')
            vram, fifo = 0xf0000000, 0xe0000000
            config(0x10, 0xc001)
            config(0x14, vram)
            config(0x18, fifo)
            config(4, 3)
            register(1, 1)
            register(2, 1280)
            register(3, 800)
            register(7, 32)
            for i, value in enumerate((16, 65536, 16, 16)):
                write(fifo+i*4, value)
            register(20, 1)
            register(21, 1)
            for _ in range(4):
                m = next_frame(2.5)
                ack(m)
                if m[3:5] == (1280, 800):
                    break
            else:
                raise AssertionError('Native SVGA mode not established')
            cursor = 16

            def update(color, incomplete=False):
                global cursor
                write(vram, color)
                for i, value in enumerate((1, 0, 0, 1, 1)):
                    write(fifo+cursor+i*4, value)
                write(fifo+8, cursor+(12 if incomplete else 20))
                register(21, 1)
                if not incomplete:
                    cursor += 20

            latencies = []
            started = time.monotonic()
            update(0xff123456)
            first = next_frame(2.5)
            latencies.append((time.monotonic()-started)*1000)
            assert frame[:4] == struct.pack('<I', 0xff123456)
            # A completed host frame remains immutable through later guest
            # updates until its exact ACK, then delivers the coalesced latest.
            update(0xff654321)
            update(0xffabcdef)
            no_frame(.1)
            assert frame[:4] == struct.pack('<I', 0xff123456)
            started = time.monotonic()
            ack(first)
            latest = next_frame(2.5)
            latencies.append((time.monotonic()-started)*1000)
            assert latest[2] == first[2]+1 and frame[:4] == struct.pack('<I', 0xffabcdef)
            ack(latest)
            update(0xff998877, incomplete=True)
            no_frame(.1)
            # Complete the same command; the partial FIFO must not have been
            # announced as a complete update or consumed prematurely.
            started = time.monotonic()
            write(fifo+8, cursor+20)
            register(21, 1)
            complete = next_frame(2.5)
            latencies.append((time.monotonic()-started)*1000)
            assert frame[:4] == struct.pack('<I', 0xff998877)
            ack(complete)
            no_frame(.1)
            if not a.polling:
                assert max(latencies) < 250, latencies
            print(json.dumps({'Mode': 'polling' if a.polling else 'event-driven',
                              'FallbackPollMilliseconds': 1000,
                              'DeliveryMilliseconds': latencies,
                              'OwnedFrameImmutable': True, 'LatestUpdateCoalesced': True,
                              'IncompleteFIFOWithheld': True, 'IdleNoFrames': True,
                              'Caution': 'Diskless protocol test; not a macOS FPS benchmark.'}, indent=2))
            frame.close()
            channel.close()
            q.close()
            control.close()
        except BaseException:
            log.flush()
            print((base/'qemu.log').read_text(errors='replace')[-4000:])
            raise
        finally:
            child.terminate()
            try:
                child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait()
            listener.close()
