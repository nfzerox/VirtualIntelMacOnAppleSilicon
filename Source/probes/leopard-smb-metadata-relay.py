#!/usr/bin/env python3
"""Loopback-only SMB research relay; print framing/status, NEVER payloads.

Use a loopback service or SSH loopback tunnel as the upstream. Optional negative integrity tests
flip exactly one byte of a structurally parsed SPNEGO MIC, never remove it.
This is a finite diagnostic, not a production transport or authentication fix.
"""
import argparse
import json
import socket
import struct
import threading
import time


def nodes(data, start, end):
    result = []
    while start < end:
        if end - start < 2:
            raise ValueError("short DER header")
        tag, size = data[start:start + 2]
        value = start + 2
        if size & 128:
            count = size & 127
            if not 1 <= count <= 3 or value + count > end:
                raise ValueError("invalid DER size")
            size = int.from_bytes(data[value:value + count], "big")
            value += count
        stop = value + size
        if stop > end:
            raise ValueError("DER bounds")
        result.append((tag, value, stop))
        start = stop
    return result


def mic_range(blob):
    outer = nodes(blob, 0, len(blob))
    if len(outer) != 1 or outer[0][0] != 0xA1:
        return None
    seq = nodes(blob, *outer[0][1:])
    if len(seq) != 1 or seq[0][0] != 0x30:
        return None
    fields = nodes(blob, *seq[0][1:])
    mic = [x for x in fields if x[0] == 0xA3]
    if len(mic) != 1:
        return None
    octets = nodes(blob, *mic[0][1:])
    if len(octets) != 1 or octets[0][0] != 4 or octets[0][2] - octets[0][1] != 16:
        raise ValueError("invalid NTLM MIC")
    return octets[0][1:]


def frame_metadata(payload, direction, tamper, negotiate_unicode=False):
    info = {"direction": direction, "length": len(payload)}
    if len(payload) >= 64 and payload[:4] == b"\xfeSMB":
        status, command = struct.unpack_from("<IH", payload, 8)
        info.update(protocol="SMB2/3", command=hex(command), status=hex(status))
        if direction == "server" and command == 1 and status == 0 and len(payload) >= 72:
            structure, flags = struct.unpack_from("<HH", payload, 64)
            if structure == 9:
                info.update(guest=bool(flags & 1), anonymous=bool(flags & 2))
        return payload, info
    if len(payload) < 35 or payload[:4] != b"\xffSMB":
        return payload, info
    info.update(command=hex(payload[4]), status=hex(struct.unpack_from("<I", payload, 5)[0]),
                flags2=hex(struct.unpack_from("<H", payload, 10)[0]), words=payload[32])
    # Darwin9 deliberately leaves this flag clear on its dialect-only, unsigned
    # negotiation. Current Apple smbd rejects that before returning a dialect.
    # Isolate this one pre-auth compatibility bit; never rewrite signed traffic,
    # authentication tokens, paths, or later session semantics.
    if negotiate_unicode and direction == "client" and payload[4] == 0x72 and payload[32] == 0:
        flags = struct.unpack_from("<H", payload, 10)[0]
        if flags & 0x0004:
            raise ValueError("refusing a signed negotiation rewrite")
        changed = bytearray(payload)
        struct.pack_into("<H", changed, 10, flags | 0x8000)
        payload = bytes(changed)
        info["negotiate_unicode_added"] = not bool(flags & 0x8000)
    if payload[4] != 0x73:
        return payload, info
    wc = payload[32]
    if (direction == "client" and wc != 12) or (direction == "server" and wc != 4):
        return payload, info
    length_at = 33 + (14 if direction == "client" else 6)
    blob_at = 33 + wc * 2 + 2
    length = struct.unpack_from("<H", payload, length_at)[0]
    if blob_at + length > len(payload):
        raise ValueError("security blob bounds")
    blob = payload[blob_at:blob_at + length]
    info["security_length"] = length
    try:
        mic = mic_range(blob)
    except ValueError:
        info["der_valid"] = False
        return payload, info
    info["mic_present"] = mic is not None
    if mic and direction == tamper:
        changed = bytearray(payload)
        changed[blob_at + mic[0] + 4] ^= 1  # checksum byte, not sequence/version
        payload = bytes(changed)
        info["mic_corrupted"] = True
    return payload, info


def receive_exact(connection, count):
    data = bytearray()
    while len(data) < count:
        part = connection.recv(count - len(data))
        if not part:
            return None
        data.extend(part)
    return data


def pump(source, destination, direction, tamper, negotiate_unicode):
    try:
        while True:
            header = receive_exact(source, 4)
            if header is None:
                break
            size = int.from_bytes(header[1:], "big")
            if size > 1024 * 1024:
                raise ValueError("bounded diagnostic frame exceeded")
            payload = receive_exact(source, size)
            if payload is None:
                break
            payload, info = frame_metadata(payload, direction, tamper, negotiate_unicode)
            print(json.dumps(info), flush=True)
            destination.sendall(header + payload)
    except (OSError, ValueError) as error:
        print(json.dumps({"direction": direction, "error": type(error).__name__}), flush=True)
    finally:
        try:
            destination.shutdown(socket.SHUT_WR)
        except OSError:
            pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--listen", type=int, default=7447)
    parser.add_argument("--upstream", type=int, default=7446)
    parser.add_argument("--corrupt-mic", choices=["client", "server"])
    parser.add_argument("--negotiate-unicode", action="store_true",
                        help="Research only: add Unicode to an unsigned dialect negotiation")
    parser.add_argument("--seconds", type=int, default=60)
    parser.add_argument("--connections", type=int, default=1,
                        help="Bounded retries for native clients that reconnect after negotiation")
    args = parser.parse_args()
    if not 1024 <= args.listen <= 65535 or not 1 <= args.upstream <= 65535 or not 1 <= args.seconds <= 300:
        parser.error("Use an unprivileged listener, a valid loopback upstream port and a finite1–300 second diagnostic")
    if not 1 <= args.connections <= 8:
        parser.error("Use1–8 diagnostic connections")
    deadline = time.monotonic() + args.seconds
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", args.listen))
        listener.listen(1)
        listener.settimeout(args.seconds)
        print(json.dumps({"ready": args.listen, "upstream": args.upstream}), flush=True)
        for number in range(args.connections):
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            listener.settimeout(remaining)
            try:
                client, _ = listener.accept()
            except TimeoutError:
                break
            print(json.dumps({"connection": number + 1}), flush=True)
            with client, socket.create_connection(("127.0.0.1", args.upstream), timeout=min(10, remaining)) as server:
                client.settimeout(remaining)
                server.settimeout(remaining)
                first = threading.Thread(target=pump, args=(client, server, "client", args.corrupt_mic, args.negotiate_unicode))
                second = threading.Thread(target=pump, args=(server, client, "server", args.corrupt_mic, args.negotiate_unicode))
                first.start(); second.start()
                # Socket activity must not extend this diagnostic indefinitely.
                first.join(max(0, deadline - time.monotonic()))
                second.join(max(0, deadline - time.monotonic()))
                for connection in (client, server):
                    try:
                        connection.shutdown(socket.SHUT_RDWR)
                    except OSError:
                        pass
                first.join(); second.join()


if __name__ == "__main__":
    main()
