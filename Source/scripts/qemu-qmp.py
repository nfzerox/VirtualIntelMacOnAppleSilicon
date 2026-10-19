#!/usr/bin/env python3
"""Send one JSON QMP command to a local VM; bounded socket timeout."""
import json
import socket
import sys

with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
    connection.settimeout(15)
    connection.connect(sys.argv[1])
    with connection.makefile('rwb', buffering=0) as stream:
        def read_reply():
            while True:
                line = stream.readline()
                if not line:
                    raise RuntimeError('QMP disconnected')
                reply = json.loads(line)
                if 'event' not in reply:
                    return reply
        greeting = read_reply()
        if 'QMP' not in greeting:
            raise RuntimeError('Not a QMP server')
        stream.write(b'{"execute":"qmp_capabilities"}\n')
        if 'error' in read_reply():
            raise RuntimeError('Cannot negotiate QMP')
        stream.write(json.dumps(json.loads(sys.argv[2])).encode() + b'\n')
        reply = read_reply()
        print(json.dumps(reply, indent=2))
        sys.exit(1 if 'error' in reply else 0)
