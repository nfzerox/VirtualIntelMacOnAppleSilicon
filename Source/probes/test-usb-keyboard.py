#!/usr/bin/env python3
"""Read real USB keyboard reports from a diskless QEMU through UHCI/QTest.

The tiny CLI/HLT ROM never boots an OS. No host input, USB hardware or existing
VM is touched. QMP uses the same key-value input path as the production app.
"""
import argparse
import json
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qemu', type=Path, required=True)
    parser.add_argument('--layout', choices=['ansi', 'iso', 'jis'], default='ansi')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='vim-usb-keyboard-') as directory:
        work = Path(directory)
        rom = bytearray(65536)
        rom[-16:-12] = bytes.fromhex('faf4ebfd')  # cli; hlt; jmp hlt
        (work/'bios').write_bytes(rom)
        with (work/'stderr').open('wb') as log:
            process = subprocess.Popen([str(args.qemu.resolve()), '-machine', 'q35,i8042=off',
                '-accel', 'tcg', '-nodefaults', '-display', 'none', '-bios', str(work/'bios'),
                '-device', 'ich9-usb-uhci1,id=usb,bus=pcie.0,addr=0x1d', '-device', f'usb-kbd,id=keyboard,bus=usb.0,usb_version=1,apple-layout={args.layout}',
                '-qtest', f'unix:{work}/test,server=on,wait=off',
                '-qmp', f'unix:{work}/monitor,server=on,wait=off'],
                stdout=subprocess.DEVNULL, stderr=log)
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
                    while True:
                        reply = test.readline().decode().strip()
                        if reply.startswith('IRQ_'):
                            continue
                        assert reply.startswith('OK'), reply
                        return reply.split()[1:]

                def write(address, data):
                    access(f'write {address:#x} {len(data):#x} 0x{data.hex()}')

                def read(address, length):
                    return bytes.fromhex(access(f'read {address:#x} {length:#x}')[0][2:])

                def pci(offset, value):
                    access(f'outl 0xcf8 {0x8000e800+offset:#x}')  # 00:1d.0
                    access(f'outl 0xcfc {value:#x}')
                pci(0x20, 0xc001)
                pci(4, 5)  # IO and bus mastering
                access('outw 0xc010 0x200')
                time.sleep(0.02)
                access('outw 0xc010 4')
                write(0x1000, struct.pack('<1024I', *([0x2002]*1024)))
                write(0x2000, struct.pack('<2I', 1, 1))
                access('outl 0xc008 0x1000')
                access('outw 0xc000 1')

                def control(request_type, request, value=0, length=0):
                    write(0x4000, struct.pack('<BBHHH', request_type, request, value, 0, length))
                    # SETUP, optional DATA1 IN and opposite-direction status.
                    transfers = [(0x2d, 8, 0, 0x4000)]
                    for offset in range(0, length, 8):
                        transfers.append((0x69, min(8, length-offset), 1 ^ ((offset//8) & 1), 0x4100+offset))
                    transfers.append((0xe1 if length else 0x69, 0, 1, 0x4200))
                    for index, (pid, size, toggle, buffer) in enumerate(transfers):
                        address = 0x3000+index*16
                        link = address+16 if index+1 < len(transfers) else 1
                        token = pid | (toggle << 19) | (((size-1) & 0x7ff) << 21)
                        write(address, struct.pack('<4I', link, (1 << 23) | (3 << 27), token, buffer))
                    write(0x2004, struct.pack('<I', 0x3000))
                    for _ in range(20):
                        time.sleep(0.005)
                        statuses = [struct.unpack('<I', read(0x3004+i*16, 4))[0] for i in range(len(transfers))]
                        if not any(status & (1 << 23) for status in statuses):
                            assert not any(status & 0x7e0000 for status in statuses), [hex(s) for s in statuses]
                            return read(0x4100, length) if length else b''
                    raise AssertionError(f'USB transfer did not complete: {statuses}')

                control(0, 9, 1)  # SetConfiguration
                descriptor = control(0x80, 6, 0x100, 18)
                assert struct.unpack_from('<HH', descriptor, 8) == (0x05ac, {'ansi': 0x0220, 'iso': 0x0221, 'jis': 0x0222}[args.layout]), descriptor.hex()

                def report():
                    return control(0xa1, 1, 0x100, 8)

                def key(value, down):
                    execute('input-send-event', events=[{'type': 'key', 'data': {'down': down,
                        'key': {'type': 'number' if isinstance(value, int) else 'qcode', 'data': value}}}])

                assert report() == bytes(8)
                cases = [('a', 0x04), ('less', 0x64), ('grave_accent', 0x35), ('yen', 0x89),
                    ('ro', 0x87), (0x5c, 0x8c), ('lang1', 0x90), ('lang2', 0x91),
                    ('kp_equals', 0x67), ('help', 0x75)]
                cases += [(f'f{i}', 0x68+i-13) for i in range(13, 21)]
                for value, usage in cases:
                    key(value, True)
                    # E0 prefixes occupy a queue entry of their own.
                    for _ in range(3):
                        actual = report()
                        if actual[2] == usage:
                            break
                    assert actual == bytes([0, 0, usage, 0, 0, 0, 0, 0]), (value, actual.hex(), hex(usage))
                    key(value, False)
                    for _ in range(3):
                        actual = report()
                        if actual == bytes(8):
                            break
                    assert actual == bytes(8), (value, 'release', actual.hex())
                key('alt_r', True)
                report()  # E0 prefix
                assert report()[0] == 0x40
                key('alt_r', False)
                report()
                assert report() == bytes(8)
                print(f'PASS: Apple {args.layout} identity, {len(cases)} ANSI/ISO/JIS/keypad/function keys and right Alt; actual USB reports and releases')
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
