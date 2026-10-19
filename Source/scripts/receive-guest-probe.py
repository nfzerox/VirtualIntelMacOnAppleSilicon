#!/usr/bin/env python3
"""Bounded development-only receiver for explicitly selected guest probe files.

Listens only on host loopback (guest user-network gateway 10.0.2.2). No command
execution, file serving, overwriting, arbitrary paths, or production app use.
"""
import argparse
import http.server
import pathlib
import secrets
import socket
import socketserver
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('output', type=pathlib.Path)
p.add_argument('names', nargs='+')
p.add_argument('--port', type=int, default=61382)
args = p.parse_args()
if args.output.exists() or any(not n.isascii() or not n.replace('-', '').replace('.', '').isalnum() for n in args.names):
    p.error('use a new output directory and simple filenames')
args.output.mkdir(parents=True)
token = secrets.token_hex(12)
pending = set(args.names)

class Receiver(http.server.BaseHTTPRequestHandler):
    def do_PUT(self):
        name = self.path.removeprefix('/'+token+'/')
        if self.path != '/'+token+'/'+name or name not in pending:
            self.send_error(404)
            return
        try:
            length = int(self.headers.get('Content-Length', '-1'))
        except ValueError:
            length = -1
        if self.headers.get('Transfer-Encoding') or not 0 < length <= 16*1024*1024:
            self.send_error(413)
            return
        self.connection.settimeout(20)
        try:
            data = self.rfile.read(length)
            if len(data) != length:
                self.send_error(400)
                return
            with (args.output/name).open('xb') as stream:
                stream.write(data)
        except (OSError, socket.timeout):
            self.send_error(500)
            return
        pending.remove(name)
        self.send_response(201)
        self.end_headers()
        print(f'Received {name}: {length} bytes', flush=True)

    def log_message(self, format, *values):
        pass  # Do not log the temporary upload capability.

class LoopbackServer(http.server.HTTPServer):
    def get_request(self):
        connection, address = super().get_request()
        connection.settimeout(20)
        return connection, address

    def server_bind(self):
        # HTTPServer normally performs a reverse DNS lookup here. This local
        # development endpoint does not need DNS and must start while offline.
        socketserver.TCPServer.server_bind(self)
        self.server_name = 'localhost'
        self.server_port = self.server_address[1]

server = LoopbackServer(('127.0.0.1', args.port), Receiver)
server.timeout = 1
with (args.output/'.endpoint').open('x') as stream:
    (args.output/'.endpoint').chmod(0o600)
    stream.write(f'http://10.0.2.2:{args.port}/{token}/\n')
print(f'Guest upload prefix: http://10.0.2.2:{args.port}/{token}/', flush=True)
print(f'Expires in five minutes; allowed files: {", ".join(args.names)}', flush=True)
deadline = time.monotonic()+300
try:
    while pending and time.monotonic()<deadline:
        server.handle_request()
finally:
    server.server_close()
if pending:
    raise SystemExit('Receiver expired before all selected files arrived')
