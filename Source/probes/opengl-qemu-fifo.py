#!/usr/bin/env python3
"""Paused, diskless VMware device test; never boots or contacts a real VM.

Exercise the actual FIFO/BAR parser, including producer boundaries, a partial
packet and ring wrap. This is not an OS timing/graphics acceptance substitute.
"""
import argparse
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time


def qualify(qemu, bios):
    with tempfile.TemporaryDirectory(prefix='vim-fifo-') as temporary:
        root = Path(temporary)
        endpoint = root / 'qtest'
        environment = dict(os.environ, VIM_DISPLAY_EVENT_DRIVEN='1')
        with (root / 'qemu.log').open('w+') as log:
            process = subprocess.Popen([
                str(qemu), '-L', str(bios), '-machine', 'q35', '-accel', 'tcg',
                '-S', '-m', '128', '-nodefaults', '-display', 'none',
                '-device', 'vmware-svga,addr=04.0,passthrough-opengl-bridge=on',
                '-monitor', 'none', '-serial', 'none', '-qtest-log', '/dev/null',
                '-qtest', f'unix:{endpoint},server=on,wait=off'],
                stdout=log, stderr=log, env=environment)
            try:
                channel = socket.socket(socket.AF_UNIX)
                channel.settimeout(5)
                deadline = time.monotonic() + 30
                while True:
                    try:
                        channel.connect(str(endpoint))
                        break
                    except (FileNotFoundError, ConnectionRefusedError):
                        if process.poll() is not None or time.monotonic() >= deadline:
                            raise RuntimeError(f'device-only QEMU did not start (exit={process.poll()})')
                        time.sleep(.01)
                with channel, channel.makefile('rwb', buffering=0) as stream:
                    def command(text):
                        stream.write((text + '\n').encode())
                        while True:
                            reply = stream.readline().decode().strip()
                            if reply.startswith('IRQ '):
                                continue
                            assert reply.startswith('OK'), (text, reply)
                            return int(reply.split()[1], 0) if ' ' in reply else 0

                    def pci(offset, value):
                        command(f'outl 0xcf8 {0x80002000 | offset:#x}')
                        command(f'outl 0xcfc {value:#x}')

                    def register(index, value):
                        command(f'outl 0xc000 {index}')
                        command(f'outl 0xc001 {value}')

                    vram, fifo = 0xf0000000, 0xe0000000
                    pci(0x10, 0xc001)
                    pci(0x14, vram)
                    pci(0x18, fifo)
                    pci(4, 3)
                    for index, value in ((2, 64), (3, 32), (1, 1), (20, 1)):
                        register(index, value)
                    low, high = 0x1000, 0x10000  # SVGA requires at least 10 KiB.

                    def write(offset, value):
                        command(f'writel {fifo + offset:#x} {value:#x}')

                    def read(offset):
                        return command(f'readl {fifo + offset:#x}')

                    def ring(start):
                        for offset, value in ((0, low), (4, high), (8, start), (12, start)):
                            write(offset, value)

                    def append(start, words):
                        offset = start
                        for word in words:
                            write(offset, word)
                            offset += 4
                            if offset == high:
                                offset = low
                        write(8, offset)
                        return offset

                    def sync(expected):
                        register(21, 1)
                        command('outl 0xc000 22')
                        assert command('inl 0xc001') == 0, 'FIFO BUSY stayed set after SYNC'
                        assert read(12) == expected, ('FIFO STOP/NEXT', read(12), expected)

                    def pixel(x, y):
                        return command(f'readl {vram + (y * 64 + x) * 4:#x}')

                    ring(low)
                    end = append(low, [2, 0x112233, 0, 0, 64, 32,
                                       29, 0xffffff, 0, 0, 64, 32, 3,
                                       2, 0x445566, 3, 4, 5, 6,
                                       3, 3, 4, 30, 15, 5, 6,
                                       30, 0xdeadbeef, 0, 1, 0, 0, 64, 32])
                    write(end, 0xfeedface)  # Must never be consumed past NEXT.
                    sync(end)
                    assert read(24) == 0xdeadbeef
                    assert pixel(0, 0) == 0x112233 and pixel(3, 4) == 0x445566
                    assert pixel(8, 4) == 0x112233 and pixel(3, 10) == 0x112233
                    assert pixel(30, 15) == 0x445566 and pixel(35, 15) == 0x112233
                    print('PASS front hint preserves VRAM; fill/copy bounds, fence and NOP stop exactly at NEXT')

                    ring(low)
                    end = append(low, [2, 0x778899, 12])
                    sync(low)  # An incomplete packet must rewind, not consume.
                    end = append(end, [14, 3, 2])
                    sync(end)
                    assert pixel(12, 14) == 0x778899 and pixel(15, 14) == 0x112233
                    print('PASS partial packet is preserved until its actual producer commit')

                    start = high - 16
                    ring(start)
                    end = append(start, [29, 0xffffff, 20, 20, 4, 3, 3,
                                         2, 0xaabbcc, 20, 20, 4, 3,
                                         25, 0, 0, 64, 32, 0xdecafbad])
                    sync(end)
                    assert pixel(20, 20) == 0xaabbcc and pixel(24, 20) == 0x112233
                    print('PASS wrapped command and UPDATE_VERBOSE reason preserve framing')

                    ring(low)
                    end = append(low, [29, 0xffffff, 0, 0, 64, 32, 0,
                                       2, 0x123456, 1, 1, 1, 1])
                    sync(end)
                    assert pixel(0, 0) == 0x112233 and pixel(1, 1) == 0x123456
                    print('PASS unsupported ROP cannot desynchronize or execute its payload')

                    # During Leopard's first boot the framebuffer timer can
                    # fill the queue before a valid scanout mode exists. A
                    # SYNC must still consume packets and complete fences,
                    # without applying rectangles to an invalid aperture.
                    register(2, 0)
                    ring(low)
                    end = append(low, [2, 0xbadcab, 1, 1, 1, 1,
                                       1, 0, 0, 64, 32, 30, 0xc01dbeef])
                    sync(end)
                    assert read(24) == 0xc01dbeef
                    assert pixel(1, 1) == 0x123456
                    register(2, 64)
                    ring(low)
                    end = append(low, [2, 0x234567, 1, 1, 1, 1])
                    sync(end)
                    assert pixel(1, 1) == 0x234567
                    print('PASS transitional scanout retires FIFO/fence without pixels and recovers a valid mode')
            except BaseException:
                log.flush()
                log.seek(0)
                print(log.read()[-4000:])
                raise
            finally:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('qemu', type=Path)
    parser.add_argument('bios', type=Path)
    arguments = parser.parse_args()
    qualify(arguments.qemu.resolve(), arguments.bios.resolve())
