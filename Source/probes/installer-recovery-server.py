#!/usr/bin/env python3
"""Loopback fault fixtures for the real installer download client."""
import argparse
import http.server
import json
import socket
import threading
import time
import urllib.parse

PAYLOAD = bytes(index % 251 for index in range(524288))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port-file', required=True)
    parser.add_argument('--events', required=True)
    args = parser.parse_args()
    lock = threading.Lock()
    counts = {}

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            path = urllib.parse.urlsplit(self.path).path
            with lock:
                count = counts[path] = counts.get(path, 0) + 1
                with open(args.events, 'a') as stream:
                    stream.write(json.dumps({'path': path, 'count': count,
                        'range': self.headers.get('Range'), 'ifRange': self.headers.get('If-Range'),
                        'host': self.headers.get('Host'),
                        'fixtureToken': {'AssetToken=fixture-A': 'A', 'AssetToken=fixture-B': 'B'}.get(self.headers.get('Cookie'))}) + '\n')
            case = path.split('/')[-2]
            if case == 'redirect-auth':
                self.send_response(302)
                self.send_header('Location', f'http://localhost:{self.server.server_port}/token-sink/InstallESD.dmg')
                self.send_header('Content-Length', '0')
                self.end_headers()
                return
            if case == 'auth-denied' or (case == 'auth-refresh' and count == 1):
                self.send_response(403)
                self.send_header('Content-Length', '0')
                self.end_headers()
                return
            if case == 'progress-loop':
                requested = self.headers.get('Range')
                start = int(requested.removeprefix('bytes=').removesuffix('-')) if requested else 0
                total = 32 * 1024 * 1024
                self.send_response(206 if start else 200)
                self.send_header('Content-Length', str(total - start))
                self.send_header('ETag', '"fixture-v1"')
                if start:
                    self.send_header('Content-Range', f'bytes {start}-{total-1}/{total}')
                self.end_headers()
                time.sleep(.05)
                try:
                    for _ in range(64):
                        self.wfile.write(PAYLOAD[:16384])
                        self.wfile.flush()
                        time.sleep(.002)
                    time.sleep(.05)
                    self.close_connection = True
                    self.connection.shutdown(socket.SHUT_RDWR)
                except (BrokenPipeError, ConnectionResetError, OSError):
                    pass
                return
            if case == 'missing':
                self.send_error(404)
                return
            if case in ('unavailable', 'pause-wait', 'lock-wait', 'long-wait') or (case == 'busy' and count <= 2):
                self.send_response(503)
                self.send_header('Retry-After', '3600' if case == 'long-wait' else '0')
                self.send_header('Content-Length', '0')
                self.end_headers()
                return
            if case == 'rate-limit' and count == 1:
                self.send_response(429)
                self.send_header('Retry-After', '1')
                self.send_header('Content-Length', '0')
                self.end_headers()
                return
            start = 0
            requested = self.headers.get('Range')
            if requested and case != 'changed':
                start = int(requested.removeprefix('bytes=').removesuffix('-'))
            status = 206 if start else 200
            self.send_response(status)
            self.send_header('Content-Length', str(len(PAYLOAD) - start))
            self.send_header('ETag', '"changed-v2"' if case == 'changed' else '"fixture-v1"')
            if start:
                first = start + 1 if case == 'bad-range' else start
                self.send_header('Content-Range', f'bytes {first}-{len(PAYLOAD)-1}/{len(PAYLOAD)}')
            self.end_headers()
            try:
                if case not in ('complete', 'changed', 'bad-range', 'bad-hash', 'pause-bad-hash', 'empty-etag', 'busy', 'rate-limit', 'pause-active', 'no-validator', 'token-sink', 'auth-refresh', 'auth-control') and count == 1:
                    time.sleep(.05)
                    for offset in range(start, start + 65536, 16384):
                        self.wfile.write(PAYLOAD[offset:offset + 16384])
                        self.wfile.flush()
                        time.sleep(.025)
                    time.sleep(.05)
                    self.close_connection = True
                    self.connection.shutdown(socket.SHUT_RDWR)
                    return
                data = b'!' * len(PAYLOAD) if case in ('bad-hash', 'pause-bad-hash') else PAYLOAD
                for offset in range(start, len(data), 16384):
                    self.wfile.write(data[offset:offset + 16384])
                    self.wfile.flush()
                    if case == 'pause-active':
                        time.sleep(.01)
            except (BrokenPipeError, ConnectionResetError, OSError):
                pass

    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    with open(args.port_file, 'w') as stream:
        stream.write(str(server.server_port))
    server.serve_forever()


if __name__ == '__main__':
    main()
