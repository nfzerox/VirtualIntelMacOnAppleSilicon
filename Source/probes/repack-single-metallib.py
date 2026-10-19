"""Offline single-function MTLB experiment; rejects unfamiliar layouts."""
import hashlib
import pathlib
import struct
import sys

source, bitcode, destination = map(pathlib.Path, sys.argv[1:])
data = bytearray(source.read_bytes())
bc = bitcode.read_bytes()
if bc[:4] == b'\xde\xc0\x17\x0b':
    _, _, bc_offset, bc_length, _ = struct.unpack_from('<IIIII', bc)
    assert bc_offset >= 20 and bc_offset + bc_length <= len(bc)
    bc = bc[bc_offset:bc_offset+bc_length]
assert data[:4] == b'MTLB' and bc[:4] == b'BC\xc0\xde'
assert struct.unpack_from('<Q', data, 16)[0] == len(data)
start, size = struct.unpack_from('<QQ', data, 72)
assert start + size == len(data)
magic, version, offset, length, cpu = struct.unpack_from('<IIIII', data, start)
assert magic == 0x0b17c0de and offset == 20 and offset + length <= size
function_start = struct.unpack_from('<Q', data, 24)[0]
assert struct.unpack_from('<I', data, function_start)[0] == 1
cursor = function_start + 8
tags = {}
while data[cursor:cursor+4] != b'ENDT':
    tag = bytes(data[cursor:cursor+4])
    count = struct.unpack_from('<H', data, cursor+4)[0]
    assert tag not in tags and cursor + 6 + count <= start
    tags[tag] = (cursor+6, count)
    cursor += 6 + count
hash_offset, hash_size = tags[b'HASH']
size_offset, size_size = tags[b'MDSZ']
assert hash_size == 32 and size_size == 8
assert data[hash_offset:hash_offset+32] == hashlib.sha256(data[start:]).digest()
assert struct.unpack_from('<Q', data, size_offset)[0] == size
wrapper = struct.pack('<IIIII', magic, version, offset, len(bc), cpu) + bc
wrapper += bytes((-len(wrapper)) % 16)
data[start:] = wrapper
struct.pack_into('<Q', data, 16, len(data))
struct.pack_into('<Q', data, 80, len(wrapper))
struct.pack_into('<Q', data, size_offset, len(wrapper))
data[hash_offset:hash_offset+32] = hashlib.sha256(wrapper).digest()
destination.write_bytes(data)
