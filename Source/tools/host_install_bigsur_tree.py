"""Rebuild Big Sur Intel's authenticated fsroot from canonical records."""

from dataclasses import dataclass, field
from hashlib import sha256
from heapq import merge
from itertools import zip_longest
import struct

from host_install_apfs_records import BLOCK, DREC, OID_MASK, XATTR, node_records, u64
from host_install_bigsur_canonical import FILE_INFO, canonical_metadata
from host_install_bigsur_seal import digest_records


HEADER = 56
SLOT = 8
ROOT_FOOTER = 40
INDEX_VALUE = 40


def ordered_key(record):
    key, _ = record
    raw = u64(key, 0)
    oid, kind = raw & OID_MASK, raw >> 60
    if kind == DREC:
        return oid, kind, struct.unpack_from("<I", key, 8)[0] >> 10, key[12:]
    if kind == XATTR:
        return oid, kind, key[10:]
    if kind == FILE_INFO:
        return oid, kind, u64(key, 8)
    return oid, kind


def toc_size(count):
    return ((count + 7) & ~7) * SLOT


def leaf_bytes(entries):
    """Serialize a sealed nonroot leaf (virtual IDs live outside its bytes)."""
    data = bytearray(BLOCK)
    table = toc_size(len(entries))
    key_base = HEADER + table
    keys, values = key_base, BLOCK
    if not entries or key_base + sum(len(k) + len(v) for k, v in entries) > BLOCK:
        raise ValueError("Big Sur leaf record set does not fit")
    for index, (key, value) in enumerate(entries):
        values -= len(value)
        struct.pack_into("<HHHH", data, HEADER + index * SLOT, keys - key_base, len(key), BLOCK - values, len(value))
        data[keys:keys + len(key)] = key
        data[values:values + len(value)] = value
        keys += len(key)
    struct.pack_into("<HHI8H", data, 32, 0x1a, 0, len(entries), 0, table,
                     keys - key_base, values - keys, 0xffff, 0, 0xffff, 0)
    return bytes(data)


@dataclass
class TreeNode:
    relative: int
    level: int
    key: bytes
    digest: bytes = b""
    children: list = field(default_factory=list)
    pivot_hole: bytes = b""


def index_bytes(node, root_footer=b""):
    """Pack an index node, including the free-list slot left by an append split."""
    data = bytearray(BLOCK)
    hole = node.pivot_hole
    count = len(node.children)
    table = toc_size(count + bool(hole))
    key_base = HEADER + table
    end = BLOCK - len(root_footer)
    keys, values = key_base, end
    for index, child in enumerate(node.children):
        key = child.key
        value = struct.pack("<Q", child.relative) + child.digest
        if len(value) != INDEX_VALUE:
            raise ValueError("Missing Big Sur child node digest")
        values -= len(value)
        struct.pack_into("<HHHH", data, HEADER + index * SLOT, keys - key_base, len(key), end - values, len(value))
        data[keys:keys + len(key)] = key
        data[values:values + len(value)] = value
        keys += len(key)
    free_key, free_value = 0xffff, 0xffff
    if hole:
        free_key = keys - key_base
        data[keys:keys + len(hole)] = hole
        struct.pack_into("<HH", data, keys, 0xffff, len(hole))
        keys += len(hole)
        values -= INDEX_VALUE
        free_value = end - values
        struct.pack_into("<HH", data, values, 0xffff, INDEX_VALUE)
        # The append split decrements nkeys but keeps the removed pivot's
        # unused table entry. Those bytes are still covered by the seal.
        struct.pack_into("<HHHH", data, HEADER + count * SLOT,
                         free_key, len(hole), free_value, INDEX_VALUE)
    if not count or values < keys:
        raise ValueError("Big Sur index records do not fit")
    struct.pack_into("<HHI8H", data, 32, 0x18 | bool(root_footer), node.level, count, 0, table,
                     keys - key_base, values - keys, free_key, len(hole), free_value, INDEX_VALUE if hole else 0)
    if root_footer:
        data[end:] = root_footer
    return bytes(data)


class IndexTree:
    """Simulate APFS's append splits with root-relative virtual object IDs."""

    def __init__(self, write_block=None):
        self.root = TreeNode(0, 0, b"")
        self.next_id = 1
        self.record_count = self.max_key = self.max_value = 0
        self.last_leaf_max = 0
        self.write_block = write_block
        self.first_leaf = None

    def new_node(self, level, key, digest=b"", children=None):
        node = TreeNode(self.next_id, level, key, digest, [] if children is None else children)
        self.next_id += 1
        return node

    def rightmost(self, level):
        node = self.root
        while node.level > level:
            node = node.children[-1]
        return node

    def has_room(self, node, key_lengths):
        count = len(node.children) + len(key_lengths)
        used = HEADER + toc_size(count) + INDEX_VALUE * count + sum(key_lengths) + sum(len(c.key) for c in node.children)
        return used <= BLOCK - (ROOT_FOOTER if node is self.root else 0)

    def split(self, node):
        level = node.level
        pivot = node.children[-1]
        if node is self.root:
            right = self.new_node(level, pivot.key, children=[pivot])
            left = self.new_node(level, node.key, children=node.children[:-1])
            node.level += 1
            node.children = [left, right]
        else:
            parent = self.rightmost(level + 1)
            if not self.has_room(parent, (self.last_leaf_max, self.incoming_key_size)):
                self.split(parent)
            node.children.pop()
            node.pivot_hole = pivot.key
            right = self.new_node(level, pivot.key, children=[pivot])
            self.rightmost(level + 1).children.append(right)

    def reserve_for_leaf_record(self, size, current_max):
        parent = self.rightmost(1)
        if not self.has_room(parent, (size, current_max)):
            self.split(parent)

    def reserve_upper_for_record(self, size):
        # Unlike the leaf-parent split, upper-index reservation is checked
        # while walking individual records, including inside an active leaf.
        # It can allocate the upper sibling several leaves before the lower
        # index splits. That chronology determines the hashed relative IDs.
        self.max_key = max(self.max_key, size)
        if self.root.level < 2:
            return
        lower, upper = self.rightmost(1), self.rightmost(2)
        if self.has_room(lower, (self.max_key, self.max_key)):
            return
        separator = max(size, max(len(child.key) for child in lower.children))
        if not self.has_room(upper, (separator, separator)):
            self.split(upper)

    def append(self, leaf):
        _, _, _, entries = node_records(leaf)
        key = entries[0].key
        self.incoming_key_size = len(key)
        digest = sha256(leaf).digest()
        self.record_count += len(entries)
        self.max_value = max(self.max_value, max(len(e.value) for e in entries))
        self.reserve_upper_for_record(len(key))
        if self.root.level == 0:
            if not self.root.key:
                self.root.key, self.root.digest = key, digest
                if self.write_block:
                    self.first_leaf = leaf
                self.last_leaf_max = max(len(e.key) for e in entries)
                self.max_key = max(self.max_key, self.last_leaf_max)
                return
            right = self.new_node(0, key, digest)
            left = self.new_node(0, self.root.key, self.root.digest)
            self.root.level = 1
            self.root.children = [left, right]
            if self.write_block:
                self.write_block(left.relative, self.first_leaf)
                self.write_block(right.relative, leaf)
                self.first_leaf = None
        else:
            self.reserve_for_leaf_record(len(key), self.last_leaf_max)
            node = self.new_node(0, key, digest)
            self.rightmost(1).children.append(node)
            if self.write_block:
                self.write_block(node.relative, leaf)
        for entry in entries[1:]:
            self.reserve_upper_for_record(len(entry.key))
        self.last_leaf_max = max(len(entry.key) for entry in entries)

    def finalize(self):
        if not self.root.level:
            raise ValueError("Big Sur production System tree unexpectedly fits in a single leaf")
        footer = struct.pack("<6I2Q", 0x1c2, BLOCK, 0, 0, self.max_key, self.max_value,
                             self.record_count, self.next_id)
        nodes = {}

        def visit(node):
            if node.level:
                for child in node.children:
                    visit(child)
                block = index_bytes(node, footer if node is self.root else b"")
                node.digest = sha256(block).digest()
                nodes[node.relative] = block
                if self.write_block:
                    self.write_block(node.relative, block)
        visit(self.root)
        return self.root.digest, nodes


def canonical_leaves(records):
    """Pack sorted records in APFS's append order.

    Before the first split the initial leaf is still the root, so its
    capacity excludes the tree-wide footer. After splitting, its output
    layout and all following leaves are nonroot layouts.
    """
    entries, payload, capacity, previous = [], 0, BLOCK - ROOT_FOOTER, None
    for record in records:
        order = ordered_key(record)
        if previous is not None and previous >= order:
            raise ValueError("Canonical Big Sur records are not strictly ordered")
        previous = order
        count = len(entries) + 1
        size = len(record[0]) + len(record[1])
        if entries and HEADER + toc_size(count) + payload + size > capacity:
            yield leaf_bytes(entries)
            entries, payload, capacity = [], 0, BLOCK
        entries.append(record)
        payload += size
    if entries:
        yield leaf_bytes(entries)


def compare_leaves(metadata, digests, reference_nodes):
    records = merge(sorted(metadata.items(), key=ordered_key), digest_records(digests), key=ordered_key)
    wanted = (node.data for node in reference_nodes if node.leaf)
    matched = 0
    for candidate, reference in zip_longest(canonical_leaves(records), wanted):
        if candidate != reference:
            raise ValueError(f"Big Sur canonical leaf {matched + 1} differs from the authenticated tree")
        matched += 1
    return matched


def build_candidate_tree(metadata, digests, *, write_block=None):
    records = merge(sorted(metadata.items(), key=ordered_key), digest_records(digests), key=ordered_key)
    tree = IndexTree(write_block)
    for leaf in canonical_leaves(records):
        tree.append(leaf)
    return tree.finalize()


def export_verified_tree(output, metadata, digests, expected):
    """Produce relative fsroot blocks, never a mounted volume or sealed disk."""
    import os
    import plistlib
    import tempfile
    from pathlib import Path
    from host_install_apfs_records import hashed_tree

    if output.exists():
        raise FileExistsError(output)
    with tempfile.TemporaryDirectory(prefix=".vim-bigsur-fsroot-", dir=output.parent) as folder:
        blocks = Path(folder) / "FSRoot.blocks"
        with blocks.open("xb+") as stream:
            def write(relative, block):
                stream.seek(relative * BLOCK)
                stream.write(block)

            actual, _ = build_candidate_tree(metadata, digests, write_block=write)
            if actual != expected:
                raise ValueError(f"Candidate root hash {actual.hex()} does not match {expected.hex()}")
            stream.flush()
            size = os.fstat(stream.fileno()).st_size
            if size % BLOCK:
                raise ValueError("Truncated Big Sur metadata block file")
            read = lambda relative: os.pread(stream.fileno(), BLOCK, relative * BLOCK)
            count = sum(1 for _ in hashed_tree(read, int, 0, expected))
            if count != size // BLOCK:
                raise ValueError("The exported Big Sur tree has unreachable blocks")
            os.fsync(stream.fileno())
        # Allocate the destination exclusively after authentication; the
        # receipt is written last. Neither artifact is a bootable disk.
        output.mkdir(mode=0o700)
        blocks.rename(output / blocks.name)
        with (output / "TreeInfo.plist").open("xb") as receipt:
            plistlib.dump({"RootHash": expected, "NodeCount": count, "BlockSize": BLOCK}, receipt)
    return actual


def main():
    import argparse
    from pathlib import Path
    import plistlib
    import subprocess
    from host_install_apfs import attached_images
    from host_install_apfs_records import APFS, MappedQCOWImage, SparseBundle
    from host_install_bigsur_boot import intel_root_hash
    from host_install_bigsur_canonical import original_seal_metadata
    from host_install_bigsur_media import signed_asset

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("system", type=Path, help="New detached System sparsebundle")
    parser.add_argument("remap", type=Path)
    parser.add_argument("digest_reader", type=Path)
    parser.add_argument("digest_database", type=Path)
    parser.add_argument("reference", type=Path, nargs="?")
    parser.add_argument("qemu_img", type=Path, nargs="?")
    parser.add_argument("--check-root", action="store_true", help="Build the canonical tree and compare its root to Apple")
    parser.add_argument("--export-tree", type=Path, help="Write verified relative blocks to a new scratch directory")
    args = parser.parse_args()
    if args.export_tree and not args.check_root:
        parser.error("exporting the tree requires --check-root")
    if not args.check_root and (args.reference is None or args.qemu_img is None):
        parser.error("comparing leaves requires a stopped reference QCOW2 and qemu-img")
    if attached_images(args.system):
        raise ValueError("Detach the APFS scratch disk before building canonical leaves")
    scratch, = (v for v in APFS(SparseBundle(args.system)).volumes() if v.role == 1)
    with args.remap.open("rb") as stream:
        remap = plistlib.load(stream)
    stamp = remap.pop("CHANGE")
    oid_map = {int(key): value for key, value in remap.items()}
    with signed_asset(args.package) as (_, archive):
        root, = (n for n in archive.namelist() if n.endswith(".x86.root_hash"))
        expected = intel_root_hash(archive.read(root))
        policy = original_seal_metadata(archive, scratch)
    metadata = canonical_metadata(scratch, oid_map, stamp, policy)
    if args.check_root:
        with subprocess.Popen((args.digest_reader, args.digest_database), stdout=subprocess.PIPE) as digest:
            try:
                if args.export_tree:
                    actual = export_verified_tree(args.export_tree, metadata, digest.stdout, expected)
                else:
                    actual, _ = build_candidate_tree(metadata, digest.stdout)
            finally:
                digest.stdout.close()
            if digest.wait():
                raise ValueError("Could not read Apple's original Big Sur digest database")
        if actual != expected:
            raise ValueError(f"Candidate root hash {actual.hex()} does not match {expected.hex()}")
        print("Reproduced Apple's Intel root hash:", actual.hex())
        return
    with args.reference.open("rb") as stream, subprocess.Popen(
            (args.digest_reader, args.digest_database), stdout=subprocess.PIPE) as digest:
        reference, = (v for v in APFS(MappedQCOWImage.from_qemu(stream, args.qemu_img)).volumes() if v.role == 1)
        _, _, nodes = reference.authenticated_snapshot(expected)
        try:
            print("Authenticated Big Sur leaf blocks reproduced:", compare_leaves(metadata, digest.stdout, nodes))
        finally:
            digest.stdout.close()
        if digest.wait():
            raise ValueError("Could not read Apple's original Big Sur digest database")


if __name__ == "__main__":
    main()
