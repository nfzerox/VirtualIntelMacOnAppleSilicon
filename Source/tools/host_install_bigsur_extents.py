"""Build a detached Big Sur sealed System file-extent tree from a new disk."""

import argparse
from collections import namedtuple
import json
from pathlib import Path
import plistlib
import struct

from host_install_apfs import attached_images
from host_install_apfs_records import APFS, BLOCK, OID_MASK, SparseBundle, block_checksum, fixed_table_size, u64
from host_install_bigsur_canonical import FILE_EXTENT, metadata_records


FEXT = 31
PHYSICAL = 0x40000000
Extent = namedtuple("Extent", "stream offset length physical")


def file_extents(volume, remap):
    """Translate stream IDs; the actual file blocks stay where APFS allocated them."""
    if volume.sealed or volume.snapshot_count or volume.role != 1:
        raise ValueError("Expected a new, unsealed System volume without snapshots")
    for entry in metadata_records(volume.nodes()):
        tag = u64(entry.key, 0)
        if tag >> 60 != FILE_EXTENT:
            continue
        _, offset = struct.unpack("<QQ", entry.key)
        length, physical, crypto = struct.unpack("<QQQ", entry.value)
        stream = tag & OID_MASK
        if (not stream or not 0 < length < 1 << 56 or offset % BLOCK or length % BLOCK
                or not 0 < physical <= volume.container.blocks - length // BLOCK or crypto):
            raise ValueError(f"Unsupported Big Sur file extent for stream {stream:#x} at {offset:#x}")
        yield Extent(remap.get(stream, stream), offset, length, physical)


def extent_records(extents):
    """Coalesce adjacent records only when both their logical and physical blocks agree."""
    current = None
    for item in sorted(extents):
        if current is not None and current.stream == item.stream:
            end = current.offset + current.length
            if item.offset < end:
                raise ValueError(f"Overlapping Big Sur extents for stream {item.stream:#x}")
            if item.offset == end and item.physical == current.physical + current.length // BLOCK:
                current = current._replace(length=current.length + item.length)
                continue
        if current is not None:
            yield struct.pack("<QQ", current.stream, current.offset), struct.pack("<QQ", current.length, current.physical)
        current = item
    if current is not None:
        yield struct.pack("<QQ", current.stream, current.offset), struct.pack("<QQ", current.length, current.physical)


def fixed_node(rows, address, xid, level, totals=None, *, subtype=FEXT, key_size=16,
               leaf_value_size=16, tree_flags=0x12):
    """Encode a fixed-key physical APFS B-tree node."""
    root = totals is not None
    value_size = 8 if level else leaf_value_size
    table_size = fixed_table_size(key_size, value_size)
    value_end = BLOCK - (40 if root else 0)
    key_start = 56 + table_size
    free = key_start + key_size * len(rows)
    free_size = value_end - value_size * len(rows) - free
    if free_size < 0:
        raise ValueError("APFS fixed B-tree node overflow")
    node = bytearray(BLOCK)
    struct.pack_into("<QQII", node, 8, address, xid, PHYSICAL | (2 if root else 3), subtype)
    flags = 4 | (0 if level else 2) | int(root)
    struct.pack_into("<HHI8H", node, 32, flags, level, len(rows), 0, table_size,
                     free - key_start, free_size, 0xffff, 0, 0xffff, 0)
    for i, (key, value) in enumerate(rows):
        if len(key) != key_size or len(value) != value_size:
            raise ValueError("Invalid APFS fixed B-tree record width")
        key_offset, value_offset = i * key_size, (i + 1) * value_size
        struct.pack_into("<HH", node, 56 + i * 4, key_offset, value_offset)
        node[key_start + key_offset:key_start + key_offset + key_size] = key
        node[value_end - value_offset:value_end - value_offset + value_size] = value
    if root:
        struct.pack_into("<6I2Q", node, value_end, tree_flags, BLOCK, key_size, leaf_value_size,
                         key_size, leaf_value_size, *totals)
    struct.pack_into("<Q", node, 0, block_checksum(node))
    return node


def tree_block_count(records):
    nodes = 1
    if records > 111:
        records = (records + 111) // 112
        nodes += records
        while records > 140:
            records = (records + 139) // 140
            nodes += records
    return nodes


def write_tree(output, records, start, xid, *, subtype=FEXT):
    """Serialize a contiguous run of blocks for an allocator to place later."""
    if start < 1 or xid < 1:
        raise ValueError("Invalid Big Sur extent tree placement")
    address, count, level = start, len(records), 0
    rows = records
    while len(rows) > (140 if level else 111):
        step = 140 if level else 112
        parents = []
        for first in range(0, len(rows), step):
            children = rows[first:first + step]
            output.write(fixed_node(children, address, xid, level, subtype=subtype))
            parents.append((children[0][0], struct.pack("<Q", address)))
            address += 1
        rows = parents
        level += 1
    nodes = address - start + 1
    output.write(fixed_node(rows, address, xid, level, (count, nodes), subtype=subtype))
    return {"RootBlock": address, "FirstBlock": start, "BlockCount": nodes,
            "ExtentCount": count, "TransactionID": xid, "RootLevel": level}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path, help="Detached scratch System sparsebundle")
    parser.add_argument("remap", type=Path, help="Canonical object IDs derived from Apple's original mtree")
    parser.add_argument("output", type=Path, help="New ordinary file for FEXT blocks (never writes to the image)")
    parser.add_argument("--start-block", type=int, required=True, help="First physical block reserved for this tree")
    parser.add_argument("--xid", type=int, required=True, help="Checkpoint transaction that will own the tree")
    args = parser.parse_args()
    if attached_images(args.image):
        raise ValueError("Detach the scratch APFS disk before reading its extents")
    container = APFS(SparseBundle(args.image))
    volume, = (v for v in container.volumes() if v.role == 1)
    remap = {int(key): oid for key, oid in plistlib.loads(args.remap.read_bytes()).items() if key != "CHANGE"}
    records = list(extent_records(file_extents(volume, remap)))
    if args.xid <= container.xid:
        raise ValueError("The new extent tree must belong to a later APFS checkpoint")
    with args.output.open("xb") as output:
        print(json.dumps(write_tree(output, records, args.start_block, args.xid), indent=2))


if __name__ == "__main__":
    main()
