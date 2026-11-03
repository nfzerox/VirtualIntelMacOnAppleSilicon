#!/usr/bin/env python3
"""Check completed-frame ownership in an isolated, diskless QEMU device.

No guest OS, real VM monitor connection, GL renderer or pixel timing heuristic.
Write/publish one framebuffer, overwrite guest VRAM WITHOUT publishing, then
publish again. Only the last completed UPDATE may appear in each screendump.
"""
import argparse
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qemu', type=Path, required=True)
    parser.add_argument('--bios-dir', type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='vim-scanout-') as scratch:
        work = Path(scratch)
        environment = dict(os.environ, VIM_DISPLAY_EVENT_DRIVEN='1')
        # A stopped CPU is intentional. Only this test's QTest device accesses
        # run; it cannot touch an installed VM, guest disk or host USB device.
        command = [str(args.qemu.resolve()), '-L', str(args.bios_dir.resolve()),
                   '-machine', 'q35', '-accel', 'tcg', '-S', '-nodefaults',
                   '-display', 'none', '-vga', 'none', '-device',
                   'vmware-svga,id=svga,bus=pcie.0,addr=0x4,passthrough-opengl-bridge=on',
                   '-qtest', f'unix:{work}/test,server=on,wait=off',
                   '-qmp', f'unix:{work}/monitor,server=on,wait=off']
        with (work/'stderr').open('wb') as log:
            process = subprocess.Popen(command, env=environment, stdout=subprocess.DEVNULL, stderr=log)
            connections = []
            try:
                def connect(name):
                    connection = socket.socket(socket.AF_UNIX)
                    connection.settimeout(10)
                    deadline = time.monotonic()+10
                    while True:
                        try:
                            connection.connect(str(work/name))
                            break
                        except (FileNotFoundError, ConnectionRefusedError):
                            if process.poll() is not None or time.monotonic() >= deadline:
                                raise RuntimeError((work/'stderr').read_text())
                            time.sleep(0.02)
                    connections.append(connection)
                    return connection, connection.makefile('rb')
                qmp, monitor = connect('monitor')
                assert 'QMP' in json.loads(monitor.readline())
                def execute(name, **arguments):
                    qmp.sendall(json.dumps({'execute': name, 'arguments': arguments}).encode()+b'\n')
                    while True:
                        reply = json.loads(monitor.readline())
                        if 'event' not in reply:
                            assert 'return' in reply, reply
                            return reply['return']
                execute('qmp_capabilities')
                device, test = connect('test')
                def access(command):
                    device.sendall(command.encode()+b'\n')
                    reply = test.readline().decode().strip()
                    assert reply.startswith('OK'), reply
                    return reply
                def out(port, value):
                    access(f'outl {port:#x} {value:#x}')
                def pci(register, value):
                    out(0xcf8, 0x80002000+register)
                    out(0xcfc, value)
                pci(0x10, 0xc001)
                pci(0x14, 0xd0000000)
                pci(0x18, 0xe0000000)
                pci(4, 3)
                def register(index, value):
                    out(0xc000, index)
                    out(0xc001, value)
                def write(address, data):
                    access(f'write {address:#x} {len(data):#x} 0x{data.hex()}')
                width, height = 320, 200
                for index, value in ((0, 0x90000002), (2, width), (3, height), (7, 32), (1, 1)):
                    register(index, value)
                write(0xe0000000, struct.pack('<4I', 16, 0x10000, 16, 16))
                register(20, 1)
                def publish():
                    write(0xe0000010, struct.pack('<5I', 1, 0, 0, width, height))
                    access('writel 0xe0000008 36')
                    register(21, 1)
                    # Reuse the same tiny FIFO only after native SYNC consumed it.
                    write(0xe0000008, struct.pack('<2I', 16, 16))
                def frame(name):
                    path = work/(name+'.ppm')
                    execute('screendump', filename=str(path))
                    return path.read_bytes()
                write(0xd0000000, struct.pack('<I', 0xff224488)*(width*height))
                publish()
                first = frame('published')
                write(0xd0000000, struct.pack('<I', 0xff66aacc)*(width*height))
                pending = frame('not-published')
                assert pending == first, 'In-progress guest VRAM leaked into scanout'
                publish()
                second = frame('next-published')
                assert second != first, 'Completed next frame was not delivered'
                assert first.endswith(bytes.fromhex('224488')*width*height)
                assert second.endswith(bytes.fromhex('66aacc')*width*height)
                print('PASS: unpublished guest writes cannot alter completed scanout')
                # The native source-to-framebuffer copy must finish in one
                # device transaction, before the guest publishes UPDATE. A
                # rejected vector must not write even its valid first entry.
                source, packet = 8*1024*1024, 12*1024*1024
                pixels=struct.pack('<I',0xff335599)*(width*height)
                write(0xd0000000+source,pixels)
                def blit(entries):
                    size=sum(entry[2] for entry in entries)
                    header=struct.pack('<4IiI',0x56474c31,9,0,size,-1,len(entries))
                    table=b''.join(struct.pack('<Q4I',src,dst,length,kind,0)
                                   for src,dst,length,kind in entries)
                    write(0xd0000000+packet,header+table)
                    register(0x1000,packet)
                    return int(access(f'readl {0xd0000000+packet+16:#x}').split()[1],16)
                assert blit([(source,0,len(pixels),0)])==len(pixels)
                assert frame('native-not-published')==second
                publish()
                native=frame('native-published')
                assert native.endswith(bytes.fromhex('335599')*width*height)
                rejected=blit([(source,0,4,0),(source,len(pixels),4,0)])
                assert rejected & 0x80000000, 'Invalid native vector was accepted'
                publish()
                assert frame('native-rejected')==native
                print('PASS: native framebuffer blit completes before UPDATE; invalid vectors write nothing')
            finally:
                for connection in connections:
                    connection.close()
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


if __name__ == '__main__':
    main()
