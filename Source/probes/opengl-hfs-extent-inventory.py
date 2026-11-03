#!/usr/bin/env python3
"""Read-only HFS+ file-fragment inventory in a generated APM recovery image.

Layout reference: Apple's HFS+ structures, also in OpenCore OpenHfsPlus.
No mount, privilege, block device access or image modification is performed.
The two B-trees must themselves fit their inline extents; decline otherwise.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct


def u16(data, offset): return struct.unpack_from('>H', data, offset)[0]
def u32(data, offset): return struct.unpack_from('>I', data, offset)[0]
def u64(data, offset): return struct.unpack_from('>Q', data, offset)[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('path')
    args = parser.parse_args()
    with args.image.open('rb') as stream:
        stream.seek(0, 2)
        image_size = stream.tell()
        def read(offset, size):
            if offset < 0 or size < 0 or offset + size > image_size:
                raise ValueError('Read outside image')
            stream.seek(offset)
            data = stream.read(size)
            if len(data) != size: raise ValueError('Short image read')
            return data
        descriptor = read(0, 512)
        if descriptor[:2] != b'ER': raise ValueError('Expected APM driver descriptor')
        sector = u16(descriptor, 2)
        if sector < 512 or sector > 4096 or sector & (sector - 1): raise ValueError('Invalid APM block size')
        first = read(sector, 512)
        if first[:2] != b'PM': raise ValueError('Expected APM recovery image')
        count = u32(first, 4)
        if not 1 <= count <= 128: raise ValueError('Invalid partition count')
        partitions = []
        for index in range(1, count + 1):
            entry = read(index * sector, 512)
            if entry[:2] != b'PM': raise ValueError('Invalid partition entry')
            if entry[48:80].rstrip(b'\0') == b'Apple_HFS':
                partitions.append((u32(entry, 8) * sector, u32(entry, 12) * sector))
        if len(partitions) != 1: raise ValueError('Expected one HFS partition')
        base, volume_size = partitions[0]
        header = read(base + 1024, 512)
        if header[:2] != b'H+': raise ValueError('Expected HFS+')
        block = u32(header, 40)
        if block < 512 or block & (block - 1): raise ValueError('Invalid block size')
        def extents(data, offset):
            return [list(struct.unpack_from('>II', data, offset + i * 8)) for i in range(8)
                    if u32(data, offset + i * 8 + 4)]
        def fork_read(exts, position, length):
            result = bytearray()
            for start, blocks in exts:
                size = blocks * block
                if (start + blocks) * block > volume_size: raise ValueError('Extent outside volume')
                if position >= size:
                    position -= size
                    continue
                take = min(length, size - position)
                result.extend(read(base + start * block + position, take))
                length -= take
                position = 0
                if not length: return bytes(result)
            raise ValueError('Required data exceeds available extents')
        def leaves(fork_offset):
            exts = extents(header, fork_offset + 16)
            h = fork_read(exts, 14, 106)
            node, node_size, total = u32(h, 10), u16(h, 18), u32(h, 22)
            if node_size < 512 or node_size & (node_size - 1): raise ValueError('Invalid B-tree node size')
            seen = set()
            while node:
                if node >= total or node in seen: raise ValueError('Invalid B-tree chain')
                seen.add(node)
                data = fork_read(exts, node * node_size, node_size)
                if data[8] != 255: raise ValueError('Expected leaf node')
                records = u16(data, 10)
                if records > (node_size - 14) // 4: raise ValueError('Invalid record count')
                for index in range(records):
                    start = u16(data, node_size - (index + 1) * 2)
                    end = u16(data, node_size - (index + 2) * 2)
                    if not 14 <= start < end <= node_size - (records + 1) * 2:
                        raise ValueError('Invalid record bounds')
                    record = data[start:end]
                    key_size = u16(record, 0) + 2
                    if not 2 <= key_size < len(record): raise ValueError('Invalid key')
                    yield record[:key_size], record[key_size:]
                node = u32(data, 0)
        catalog = {}
        for key, body in leaves(272):
            kind = u16(body, 0)
            if kind not in (1, 2): continue
            name = key[8:8 + u16(key, 6) * 2].decode('utf-16be')
            catalog[(u32(key, 2), name)] = (u32(body, 8), kind, body)
        parent = 2
        for name in args.path.strip('/').split('/'):
            parent, kind, body = catalog[(parent, name)]
        if kind != 2: raise ValueError('Expected a regular catalog file')
        logical = u64(body, 88)
        initial = extents(body, 104)
        overflow = []
        for key, body in leaves(192):
            if len(key) != 12: raise ValueError('Invalid extent key')
            if key[2] == 0 and u32(key, 4) == parent:
                overflow.append((u32(key, 8), extents(body, 0)))
        combined = initial[:]
        blocks = sum(e[1] for e in combined)
        for start, exts in sorted(overflow):
            if start != blocks: raise ValueError('Non-contiguous logical extent chain')
            combined.extend(exts)
            blocks += sum(e[1] for e in exts)
        if logical > 512 * 1024 * 1024: raise ValueError('File too large for bounded probe')
        digest = hashlib.sha256(fork_read(combined, 0, logical)).hexdigest()
        print(json.dumps(dict(Path=args.path, CatalogID=parent, Bytes=logical,
            BlockSize=block, InlineExtents=initial,
            InlineBytes=sum(e[1] for e in initial) * block,
            OverflowRecords=sorted(overflow), SHA256=digest), indent=2))


if __name__ == '__main__': main()
