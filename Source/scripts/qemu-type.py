#!/usr/bin/env python3
"""Type ASCII into an explicitly selected local VM console using QMP keys."""
import json
import socket
import sys
import time

plain = {' ': 'spc', '\n': 'ret', '-': 'minus', '=': 'equal', '/': 'slash',
         '.': 'dot', ',': 'comma', ';': 'semicolon', "'": 'apostrophe',
         '[': 'bracket_left', ']': 'bracket_right', '\\': 'backslash', '`': 'grave_accent'}
shifted = dict(zip('!@#$%^&*()', '1234567890'))
shifted.update({'_': 'minus', '+': 'equal', ':': 'semicolon', '"': 'apostrophe',
                '<': 'comma', '>': 'dot', '?': 'slash', '|': 'backslash',
                '{': 'bracket_left', '}': 'bracket_right', '~': 'grave_accent'})
sequence = []
for char in sys.argv[2]:
    if char in shifted:
        keys = ['shift', shifted[char]]
    elif char.isascii() and char.isalpha():
        keys = (['shift'] if char.isupper() else []) + [char.lower()]
    elif char in '0123456789':
        keys = [char]
    else:
        keys = [plain[char]]
    sequence.append(keys)
with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
    connection.settimeout(10)
    connection.connect(sys.argv[1])
    stream = connection.makefile('rwb', buffering=0)
    def reply():
        while True:
            value = json.loads(stream.readline())
            if 'event' not in value:
                if 'error' in value: raise RuntimeError(value)
                return value
    reply()
    stream.write(b'{"execute":"qmp_capabilities"}\n')
    reply()
    for keys in sequence:
        command = {'execute': 'send-key', 'arguments': {
            'keys': [{'type': 'qcode', 'data': key} for key in keys], 'hold-time': 15}}
        stream.write(json.dumps(command).encode() + b'\n')
        reply()
        time.sleep(0.04)
