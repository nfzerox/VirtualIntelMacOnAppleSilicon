#!/usr/bin/env python3
"""Diskless real QEMU device test: RAM DMA, negotiation and preflight atomicity.

Uses a test-only partial-I/O executor, never a guest disk or live GL service.
"""
import argparse
import os
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('qemu', type=Path)
p.add_argument('executor', type=Path)
a = p.parse_args()
with tempfile.TemporaryDirectory(prefix='vim-headless-dma-') as temporary:
    root = Path(temporary)
    rom = bytearray(65536)
    rom[-16:-12] = bytes.fromhex('fa f4 ebfd')
    (root/'halt.rom').write_bytes(rom)
    env = dict(os.environ, VIM_PASSTHROUGH_OPENGL_INPROCESS_LIBRARY=str(a.executor.resolve()))
    with (root/'qemu.log').open('w+b') as log:
        child = subprocess.Popen([str(a.qemu.resolve()), '-machine', 'q35', '-m', '64',
            '-nodefaults', '-display', 'none', '-serial', 'none', '-monitor', 'none',
            '-bios', str(root/'halt.rom'), '-device', 'vim-gl-headless,addr=06.0',
            '-qtest', f'unix:{root}/qtest,server=on,wait=off', '-qtest-log', '/dev/null'],
            env=env, stdout=log, stderr=log)
        try:
            channel = socket.socket(socket.AF_UNIX)
            deadline = time.monotonic()+10
            while True:
                try:
                    channel.connect(str(root/'qtest'))
                    break
                except (FileNotFoundError, ConnectionRefusedError):
                    assert child.poll() is None and time.monotonic()<deadline
                    time.sleep(.02)
            channel.settimeout(10)
            stream = channel.makefile('rwb', buffering=0)
            def q(command):
                channel.sendall((command+'\n').encode())
                while True:
                    line = stream.readline().decode().strip()
                    if line.startswith('IRQ '):
                        continue
                    assert line.startswith('OK'), (command, line)
                    return line[2:].strip()
            def write(address, data):
                q(f'write {address:#x} {len(data):#x} 0x'+data.hex())
            def read(address, size):
                return bytes.fromhex(q(f'read {address:#x} {size:#x}')[2:])
            def pci(offset, value):
                q(f'outl 0xcf8 {0x80003000+offset:#x}')
                q(f'outl 0xcfc {value:#x}')
            registers, mailbox = 0xe0000000, 0xe2000000
            pci(0x10, registers)
            pci(0x14, mailbox)
            pci(4, 7)
            magic, capabilities = 0x56474c31, 4|8|32
            assert int(q(f'readl {registers+12:#x}'), 16)==capabilities
            def packet(slot, action, token=0, size=0, reserved=0, data=b''):
                address = mailbox+slot*512*1024
                write(address, struct.pack('<IIIIiI', magic, action, token, size, -1, reserved)+data)
                q(f'writel {registers+8:#x} {slot}')
                result = struct.unpack('<IIIIiI', read(address, 24))
                return result[2], result[4], result[5]
            old, result, caps = packet(0, 1)
            assert old and result==0 and caps==64|256
            assert packet(0, 8, old, 1, 1, struct.pack('<QII', 0x200000, 1, 1))[1]<0
            token, result, caps = packet(1, 1, reserved=capabilities)
            assert token and result==0 and caps==capabilities|64|256
            # >256KiB, >64 ranges, noncontiguous/reversed physical rows.
            addresses = [0x200000+(100-i)*8192 for i in range(100)]
            lengths = [4093]*100
            table = b''.join(struct.pack('<QII', address, size, 1)
                             for address, size in zip(addresses, lengths))
            size = sum(lengths)
            pattern = bytes((i*17+3)&255 for i in range(size))
            for address in addresses:
                write(address-1, b'\xa5'*4095)
            # Last bad range must be rejected before writing ANY earlier RAM.
            for bad in (struct.pack('<QII', registers, lengths[-1], 1),
                        struct.pack('<QII', mailbox, lengths[-1], 1),
                        struct.pack('<QII', 0xffffffffffffffff, lengths[-1], 1),
                        struct.pack('<QII', 0xf0000, lengths[-1], 1),
                        struct.pack('<QII', addresses[-1], lengths[-1], 0)):
                assert packet(1, 8, token, size, 100, table[:-16]+bad)[1]<0
                assert read(addresses[0]-1, 4095)==b'\xa5'*4095
            assert packet(1, 8, token, size, 100, table)[1]==size
            offset = 0
            for address, length in zip(addresses, lengths):
                assert read(address-1, length+2)==b'\xa5'+pattern[offset:offset+length]+b'\xa5'
                offset += length
            assert packet(1, 10, token, size, 100, table)[1]==size
            assert packet(1, 4, token)[1]==0
            assert packet(0, 4, old)[1]==0
            print('PASS old/new capability isolation, >256KiB reversed RAM ranges, partial readv/writev + EINTR, guards, ROM/MMIO/mailbox/overflow/kind preflight rejection and close')
        except Exception:
            log.flush()
            log.seek(0)
            print(log.read().decode(errors='replace'))
            raise
        finally:
            child.terminate()
            try:
                child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait()
            if child.returncode not in (0, -15):
                log.seek(0)
                print(log.read().decode(errors='replace'))
