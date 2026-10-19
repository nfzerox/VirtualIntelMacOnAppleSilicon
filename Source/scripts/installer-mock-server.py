#!/usr/bin/env python3
"""Loopback-only Apple installer mirror with HTTP Range / If-Range support.
Store files beneath ROOT at their original swcdn.apple.com URL paths.
No downloads or external network access are performed by this server.
"""
import argparse
import email.utils
import http.server
import pathlib
import re
import time
import urllib.parse


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', required=True, type=pathlib.Path)
    parser.add_argument('--port', type=int, default=8765)
    parser.add_argument('--bytes-per-second', type=int, default=0)
    parser.add_argument('--ready-file', type=pathlib.Path)
    args = parser.parse_args()
    root = args.root.resolve(strict=True)

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_HEAD(self):
            self.serve(False)

        def do_GET(self):
            self.serve(True)

        def serve(self, body):
            relative = urllib.parse.unquote(urllib.parse.urlsplit(self.path).path).lstrip('/')
            path = (root / relative).resolve()
            if not path.is_relative_to(root) or not path.is_file():
                self.send_error(404)
                return
            stat = path.stat()
            etag = f'"{stat.st_size:x}-{stat.st_mtime_ns:x}"'
            modified = email.utils.formatdate(stat.st_mtime, usegmt=True)
            start, end, status = 0, stat.st_size - 1, 200
            requested = self.headers.get('Range')
            validator = self.headers.get('If-Range')
            if requested and (not validator or validator in (etag, modified)):
                match = re.fullmatch(r'bytes=(\d+)-(\d*)', requested)
                if not match or int(match[1]) >= stat.st_size:
                    self.send_response(416)
                    self.send_header('Content-Range', f'bytes */{stat.st_size}')
                    self.end_headers()
                    return
                start = int(match[1])
                end = min(end, int(match[2])) if match[2] else end
                if end < start:
                    self.send_error(416)
                    return
                status = 206
            self.send_response(status)
            self.send_header('Content-Length', str(end - start + 1))
            self.send_header('Content-Type', 'application/octet-stream')
            self.send_header('Accept-Ranges', 'bytes')
            self.send_header('ETag', etag)
            self.send_header('Last-Modified', modified)
            if status == 206:
                self.send_header('Content-Range', f'bytes {start}-{end}/{stat.st_size}')
            self.end_headers()
            if not body:
                return
            remaining = end - start + 1
            try:
                with path.open('rb') as stream:
                    stream.seek(start)
                    while remaining:
                        chunk = stream.read(min(65536, remaining))
                        if not chunk:
                            return
                        self.wfile.write(chunk)
                        remaining -= len(chunk)
                        if args.bytes_per_second:
                            time.sleep(len(chunk) / args.bytes_per_second)
            except (BrokenPipeError, ConnectionResetError):
                pass  # Pause/cancel is an expected client operation.

    server = http.server.ThreadingHTTPServer(('127.0.0.1', args.port), Handler)
    if args.ready_file:
        args.ready_file.write_text(str(server.server_port), encoding='ascii')
    print(f'Installer mirror: http://127.0.0.1:{server.server_port} → {root}', flush=True)
    server.serve_forever()


if __name__ == '__main__':
    main()
