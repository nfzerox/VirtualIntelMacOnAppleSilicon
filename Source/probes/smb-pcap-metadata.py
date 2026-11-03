#!/usr/bin/env python3
"""Read a finite diagnostic PCAP pipe; print SMB1 header metadata only.

No packet capture file, raw bytes, paths, names, tokens or credentials are
written. Intended input: tcpdump -i lo0 -s96 -U -w- 'tcp port445'.
Truncated TCP segments without a complete SMB header are ignored.
"""
import json
import struct
import sys


def exact(count):
    result = bytearray()
    while len(result) < count:
        part = sys.stdin.buffer.read(count - len(result))
        if not part:
            return None
        result.extend(part)
    return result


def main():
    header = exact(24)
    if not header:
        return
    byteorder = {b"\xd4\xc3\xb2\xa1": "<", b"\xa1\xb2\xc3\xd4": ">",
                 b"\x4d\x3c\xb2\xa1": "<", b"\xa1\xb2\x3c\x4d": ">"}.get(bytes(header[:4]))
    if byteorder is None:
        raise ValueError("unsupported PCAP format")
    linktype = struct.unpack_from(byteorder + "I", header, 20)[0]
    if linktype not in (0, 1, 108):
        raise ValueError("unsupported link type")
    print(json.dumps({"ready": True, "linktype": linktype}), flush=True)
    while True:
        record = exact(16)
        if record is None:
            return
        sec, fraction, size, original = struct.unpack(byteorder + "IIII", record)
        if size > 65536:
            raise ValueError("unexpected capture size")
        packet = exact(size)
        if packet is None:
            return
        ip = 14 if linktype == 1 else 4
        if len(packet) < ip + 20 or packet[ip] >> 4 != 4 or packet[ip + 9] != 6:
            continue
        tcp = ip + (packet[ip] & 15) * 4
        if len(packet) < tcp + 20:
            continue
        source, destination = struct.unpack_from("!HH", packet, tcp)
        if packet[tcp + 13] & 5:  # FIN/RST, never TCP payload or credentials
            print(json.dumps({"time": sec + fraction / 1000000,
                              "client_port": destination if source == 445 else source,
                              "direction": "server" if source == 445 else "client",
                              "close": "reset" if packet[tcp + 13] & 4 else "finish"}), flush=True)
        smb = tcp + (packet[tcp + 12] >> 4) * 4 + 4
        if len(packet) < smb + 32 or packet[smb:smb + 4] != b"\xffSMB":
            continue
        print(json.dumps({"time": sec + fraction / 1000000,
                          "client_port": destination if source == 445 else source,
                          "direction": "server" if source == 445 else "client",
                          "command": hex(packet[smb + 4]),
                          "status": hex(struct.unpack_from("<I", packet, smb + 5)[0]),
                          "flags2": hex(struct.unpack_from("<H", packet, smb + 10)[0]),
                          "mid": struct.unpack_from("<H", packet, smb + 30)[0],
                          "tid": struct.unpack_from("<H", packet, smb + 24)[0],
                          "uid": struct.unpack_from("<H", packet, smb + 28)[0],
                          "words": packet[smb + 32] if len(packet) > smb + 32 else None,
                          "packet_size": original}), flush=True)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
