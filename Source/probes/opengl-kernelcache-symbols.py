#!/usr/bin/env python3
"""Read addresses from the exact recovery kernel, not a nearby KDK build.

Companion to opengl-kernelcache-inventory.py. Read-only; accepts a thin x86_64
Mach-O or Apple's thin comp/lzss cache and prints only the requested symbols.
"""
import argparse
import json
from pathlib import Path
import struct
import uuid

from importlib.util import module_from_spec, spec_from_file_location


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kernel", type=Path)
    parser.add_argument("symbols", nargs="+")
    args = parser.parse_args()
    data = args.kernel.read_bytes()
    if data.startswith(b"comp"):
        spec = spec_from_file_location("inventory", Path(__file__).with_name("opengl-kernelcache-inventory.py"))
        inventory = module_from_spec(spec)
        spec.loader.exec_module(inventory)
        data = bytes(inventory.decode(data))
    if len(data) < 32 or struct.unpack_from("<I", data)[0] != 0xFEEDFACF:
        raise ValueError("Expected a thin 64-bit little-endian Mach-O")
    commands, command_bytes = struct.unpack_from("<II", data, 16)
    cursor, end, table, identifier = 32, 32 + command_bytes, None, None
    if end > len(data):
        raise ValueError("Load commands exceed kernel")
    for _ in range(commands):
        command, size = struct.unpack_from("<II", data, cursor)
        if size < 8 or cursor + size > end:
            raise ValueError("Invalid load command")
        if command == 2 and size >= 24:
            table = struct.unpack_from("<IIII", data, cursor + 8)
        if command == 0x1B and size >= 24:
            identifier = str(uuid.UUID(bytes=data[cursor + 8:cursor + 24]))
        cursor += size
    if not table:
        raise ValueError("No symbol table")
    offset, count, strings, string_bytes = table
    if offset + count * 16 > len(data) or strings + string_bytes > len(data):
        raise ValueError("Symbol table exceeds kernel")
    found = {}
    for index in range(count):
        name_index, kind, section, description, address = struct.unpack_from("<IBBHQ", data, offset + index * 16)
        if name_index >= string_bytes or kind & 0xE0:
            continue
        finish = data.find(b"\0", strings + name_index, strings + string_bytes)
        if finish < 0:
            raise ValueError("Unterminated symbol")
        name = data[strings + name_index:finish].decode("ascii")
        if name in args.symbols:
            found[name] = hex(address)
    print(json.dumps({"UUID": identifier, "Symbols": found}, indent=2))
    missing = set(args.symbols) - found.keys()
    if missing:
        raise SystemExit("Missing symbols: " + ", ".join(sorted(missing)))


if __name__ == "__main__":
    main()
