#!/usr/bin/env python3
"""Read-only development check of generated Mojave/Catalina cache signatures and FIPS text."""
import hashlib
import hmac
import os
from pathlib import Path
import struct
import sys


def verify(path):
    with Path(path).open("rb") as file:
        size = os.fstat(file.fileno()).st_size

        def read(offset, length):
            if offset < 0 or length < 0 or offset + length > size:
                raise ValueError("cache range exceeds the file")
            data = os.pread(file.fileno(), length, offset)
            if len(data) != length:
                raise ValueError("incomplete cache read")
            return data

        header = read(0, 56)
        if not header.startswith(b"dyld_v1"):
            raise ValueError("not an Intel dyld cache")
        mapping_offset, mapping_count, image_offset, image_count = struct.unpack_from("<IIII", header, 16)
        signature_offset, signature_size = struct.unpack_from("<QQ", header, 40)
        signature = read(signature_offset, signature_size)
        magic, length, count = struct.unpack_from(">III", signature)
        if magic != 0xFADE0CC0 or length > len(signature):
            raise ValueError("invalid signature superblob")
        verified_pages = 0
        for index in range(count):
            kind, offset = struct.unpack_from(">II", signature, 12 + index * 8)
            if kind != 0 and not 0x1000 <= kind < 0x1005:
                continue
            cd_magic, cd_length = struct.unpack_from(">II", signature, offset)
            cd = signature[offset:offset + cd_length]
            if cd_magic != 0xFADE0C02 or len(cd) != cd_length:
                raise ValueError("invalid CodeDirectory")
            hash_offset, _, _, slots, limit = struct.unpack_from(">IIIII", cd, 16)
            hash_size, hash_type, _, page_power = struct.unpack_from("BBBB", cd, 36)
            if hash_type not in (1, 2) or limit != signature_offset:
                raise ValueError("unexpected cache hash format")
            page_size = 1 << page_power
            if slots != (limit + page_size - 1) // page_size:
                raise ValueError("cache page count differs from its signed range")
            algorithm = "sha1" if hash_type == 1 else "sha256"
            for page in range(slots):
                start = page * page_size
                digest = hashlib.new(algorithm, read(start, min(page_size, limit - start))).digest()
                expected = cd[hash_offset + page * hash_size:hash_offset + (page + 1) * hash_size]
                if not hmac.compare_digest(digest, expected):
                    raise ValueError(f"signature page {page} differs")
            verified_pages += slots
        if not verified_pages:
            raise ValueError("no verified CodeDirectory")

        mappings = [struct.unpack("<QQQII", read(mapping_offset + index * 32, 32))
                    for index in range(mapping_count)]
        for index in range(image_count):
            address, _, _, name_offset, _ = struct.unpack("<QQQII", read(image_offset + index * 32, 32))
            name = read(name_offset, min(1024, size - name_offset)).split(b"\0", 1)[0]
            if name != b"/usr/lib/system/libcorecrypto.dylib":
                continue
            base, _, file_offset, _, _ = next(mapping for mapping in mappings
                                              if mapping[0] <= address < mapping[0] + mapping[1])
            image = file_offset + address - base
            mh = read(image, 32)
            magic = struct.unpack_from("<I", mh)[0]
            if magic not in (0xFEEDFACE, 0xFEEDFACF):
                raise ValueError("invalid corecrypto Mach-O")
            is64 = magic == 0xFEEDFACF
            commands, command_size = struct.unpack_from("<II", mh, 16)
            load_commands = read(image + (32 if is64 else 28), command_size)
            cursor = 0
            sections = {}
            for _ in range(commands):
                cmd, length = struct.unpack_from("<II", load_commands, cursor)
                if cmd == (0x19 if is64 else 1) and load_commands[cursor + 8:cursor + 24].rstrip(b"\0") == b"__TEXT":
                    vm_address = struct.unpack_from("<Q" if is64 else "<I", load_commands, cursor + 24)[0]
                    section_count = struct.unpack_from("<I", load_commands, cursor + (64 if is64 else 48))[0]
                    for section in range(section_count):
                        at = cursor + (72 if is64 else 56) + section * (80 if is64 else 68)
                        name = load_commands[at:at + 16].rstrip(b"\0")
                        address, length, offset = struct.unpack_from("<QQI" if is64 else "<III", load_commands, at + 32)
                        if offset != address - vm_address:
                            raise ValueError("corecrypto TEXT offset is not image-relative")
                        sections[name] = read(image + address - vm_address, length)
                cursor += struct.unpack_from("<I", load_commands, cursor + 4)[0]
            digest = hmac.digest(b"\0", sections[b"__text"], "sha256")
            if not hmac.compare_digest(digest, sections[b"__fips_hmacs"]):
                raise ValueError("corecrypto text does not match its FIPS seal")
            print(f"{Path(path).name}: PASS {verified_pages} signed pages and corecrypto FIPS HMAC")
            return
        raise ValueError("corecrypto is absent from the cache")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit("usage: host-install-cache-fips.py CACHE [CACHE ...]")
    for path in sys.argv[1:]:
        verify(path)
