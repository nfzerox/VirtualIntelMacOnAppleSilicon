#!/usr/bin/env python3
"""Restricted service handshake: no generated GL layout or GL context needed."""
import argparse
import socket
import struct

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--port', type=int, default=18093)
args = p.parse_args()
if not 1024 <= args.port <= 65535:
    p.error('Invalid private executor port')


def message(op, values, flags=1):
    return struct.pack('<4I16Q', op, 0, 0, flags, *(values + [0] * (16-len(values))))


def receive(s, count):
    data = b''
    while len(data) < count:
        part = s.recv(count-len(data))
        if not part:
            break
        data += part
    return data


for invalid in ('version', 'namespace', 'GL command', 'repeat hello'):
    with socket.create_connection(('127.0.0.1', args.port), timeout=3) as s:
        hello = message(0x10033, [0x56534731, 2 if invalid == 'version' else 1, 0,
                                  0 if invalid == 'namespace' else 1, 0])
        s.sendall(hello)
        if invalid not in ('version', 'namespace'):
            reply = receive(s, 16)
            assert len(reply) == 16, 'Valid restricted handshake failed'
            size, error, capabilities = struct.unpack('<IIQ', reply)
            assert size == 0 and error == 0 and capabilities == (4 | 131072), reply
            s.sendall(message(0x10000, []) if invalid == 'GL command' else hello)
        assert receive(s, 16) == b'', 'Invalid service request was not rejected: ' + invalid
        print('PASS restricted service rejects ' + invalid)
