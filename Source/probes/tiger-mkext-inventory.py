#!/usr/bin/env python3
"""Read-only, bounded inventory of a native i386 mkext1 archive.

Layout: Apple's Darwin9 libsa/libsa/mkext.h (same1.0.0 format in this DVD).
LZSS decoding follows Haruhiko Okumura's freely usable1989 algorithm retained
in Apple's libsa/mkext.c. No extraction, executable loading or image changes.
"""
import argparse
import json
from pathlib import Path
import plistlib
import struct
import zlib


def decompress(data, expected):
    if not 0 < expected <= 4 * 1024 * 1024:
        raise ValueError("Invalid bounded plist size")
    ring = [32] * (4096 - 18) + [None] * 18
    write, at, flags = 4096 - 18, 0, 0
    output = bytearray()
    while at < len(data):
        flags >>= 1
        if not flags & 0x100:
            flags = data[at] | 0xff00
            at += 1
        if flags & 1:
            if at >= len(data):
                raise ValueError("Truncated LZSS literal")
            values = [data[at]]
            at += 1
            for value in values:
                output.append(value); ring[write] = value; write = (write + 1) & 4095
        else:
            if at + 2 > len(data):
                raise ValueError("Truncated LZSS match")
            low, high = data[at:at + 2]
            at += 2
            offset, count = low | ((high & 0xf0) << 4), (high & 15) + 3
            for i in range(count):
                value = ring[(offset + i) & 4095]
                if value is None:
                    raise ValueError("LZSS references uninitialized history")
                output.append(value); ring[write] = value; write = (write + 1) & 4095
        if len(output) > expected:
            raise ValueError("LZSS exceeded declared size")
    if len(output) != expected:
        raise ValueError("LZSS did not produce declared size")
    return bytes(output)


def inventory(path):
    if not 32 <= path.stat().st_size <= 64 * 1024 * 1024:
        raise ValueError("Invalid bounded mkext size")
    data = path.read_bytes()
    magic, signature, length, checksum, version, count, cpu, subtype = struct.unpack_from(">8I", data)
    if (magic, signature, length, version, cpu) != (0x4d4b5854, 0x4d4f5358, len(data), 0x01008000, 7):
        raise ValueError("Not an intact native i386 mkext1")
    if not 0 < count <= 1024 or 32 + count * 32 > len(data) or zlib.adler32(data[16:]) != checksum:
        raise ValueError("Invalid mkext table/checksum")
    rows = []
    for index in range(count):
        offset, compressed, real, modified = struct.unpack_from(">4I", data, 32 + index * 32)
        size = compressed or real
        if not 32 + count * 32 <= offset or not 0 < real <= 4 * 1024 * 1024 or offset + size > len(data):
            raise ValueError("Invalid plist range")
        contents = data[offset:offset + size]
        info = plistlib.loads((decompress(contents, real) if compressed else contents).rstrip(b"\0"))
        if not isinstance(info, dict) or not isinstance(info.get("CFBundleIdentifier"), str):
            raise ValueError("Invalid kernel bundle metadata")
        row = {"Identifier": info["CFBundleIdentifier"], "Version": info.get("CFBundleVersion"),
               "Required": info.get("OSBundleRequired"), "Personalities": {}}
        for name, personality in info.get("IOKitPersonalities", {}).items():
            keys = ("IOClass", "IOProviderClass", "IOPCIPrimaryMatch", "IOPCIClassMatch", "IOPCIMatch", "IONameMatch")
            row["Personalities"][name] = {key: personality[key] for key in keys if key in personality}
        rows.append(row)
    return {"FormatVersion": 1, "NativeArchitecture": "i386", "ArchiveVersion": hex(version),
            "KernelExtensionCount": count, "Extensions": rows}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--matching", default="", help="Print only matching bundle identifiers")
    args = parser.parse_args()
    report = inventory(args.archive)
    if args.matching:
        report["Extensions"] = [row for row in report["Extensions"] if args.matching.lower() in row["Identifier"].lower()]
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
