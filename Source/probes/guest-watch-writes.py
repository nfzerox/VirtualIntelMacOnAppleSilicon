#!/usr/bin/env python3
"""Bounded TCG guest write-watch diagnosis over the app's opt-in GDB port.

Never opens QMP/QGA, changes guest memory, or resumes after the requested
byte pattern is observed. The VM must be a disposable development boot.
"""
import argparse
import json
import socket
import time


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--port', type=int, required=True)
    p.add_argument('--address', type=lambda s: int(s, 0), action='append', required=True)
    p.add_argument('--pattern', default='0101010101010101')
    p.add_argument('--seconds', type=int, default=300)
    p.add_argument('--stops', type=int, default=2048)
    a = p.parse_args()
    with socket.create_connection(('127.0.0.1', a.port), timeout=10) as s:
        s.settimeout(a.seconds)

        def receive():
            while True:
                byte = s.recv(1)
                if not byte:
                    raise RuntimeError('GDB connection closed')
                if byte == b'$':
                    break
            payload = bytearray()
            while True:
                b = s.recv(1)
                if not b:
                    raise RuntimeError('GDB connection closed')
                if b == b'#':
                    break
                payload += b
            checksum = s.recv(2)
            if int(checksum, 16) != sum(payload) % 256:
                raise RuntimeError('GDB packet checksum mismatch')
            s.sendall(b'+')
            return payload.decode('ascii')

        def command(value):
            data = value.encode('ascii')
            s.sendall(b'$' + data + b'#' + f'{sum(data)%256:02x}'.encode())
            return receive()

        print(command('qSupported'), flush=True)
        s.sendall(b'\x03')
        # QEMU can already be stopped at attach (the app also starts with -S).
        # Query the stop reason rather than waiting for an unsolicited reply.
        print('Stopped:', command('?'), flush=True)
        for addr in a.address:
            response = command(f'Z2,{addr:x},8')
            if response != 'OK':
                raise RuntimeError(f'write watch {addr:#x} rejected: {response}')
        deadline = time.monotonic() + a.seconds
        for index in range(a.stops):
            if time.monotonic() >= deadline:
                raise RuntimeError('Bounded observation ended; VM remains stopped')
            s.settimeout(max(1, deadline-time.monotonic()))
            stop = command('c')
            values = {hex(addr): command(f'm{addr:x},8') for addr in a.address}
            registers = command('g')
            pc = int.from_bytes(bytes.fromhex(registers[256:272]), 'little') if len(registers)>=272 else None
            print(json.dumps({'Stop': index, 'Reason': stop, 'PC': hex(pc) if pc is not None else None,
                              'Values': values, 'Registers': registers}), flush=True)
            if a.pattern in values.values():
                print('Requested pattern observed; VM left stopped.', flush=True)
                return
        raise RuntimeError('Stop limit reached; VM remains stopped')


if __name__ == '__main__':
    main()
