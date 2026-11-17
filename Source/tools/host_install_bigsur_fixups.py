"""Audit the original Big Sur metadata against a detached System volume."""

from collections import Counter, defaultdict
import json
import os
from pathlib import Path
import stat
import struct
import tempfile

from host_install_apfs_records import BLOCK, INODE, OID_MASK, SF_FIRMLINK, XATTR, block_checksum, u64
from host_install_bigsur_media import PAYLOAD, run_archive


MODES = {"D": stat.S_IFDIR, "F": stat.S_IFREG, "L": stat.S_IFLNK}
FIRMLINK_FIXUP_FLAGS = 0x00008000 | 0x00080000 | 0x00100000  # hidden, restricted, nounlink


def original_fixups(archive):
    with tempfile.TemporaryFile() as stream:
        run_archive(archive, PAYLOAD + "fixup.manifest", "list", "-list-format", "json", output=stream)
        stream.seek(0)
        return json.load(stream)


def inode_paths(volume):
    children = defaultdict(list)
    for parent, name, inode, _ in volume.directory_entries():
        children[parent].append((name, inode))
    pending, paths = [(b"", 2)], {}
    while pending:
        name, inode = pending.pop()
        if name in paths:
            raise ValueError("APFS volume has duplicate paths")
        paths[name] = inode
        for child, item in children.pop(inode, ()):
            pending.append((name + b"/" + child if name else child, item))
    return paths


def inode_metadata(records):
    for node in records:
        if not node.leaf:
            continue
        for entry in node.entries:
            key = u64(entry.key, 0)
            if key >> 60 == INODE:
                flags, uid, gid, mode = struct.unpack_from("<IIIH", entry.value, 68)
                yield key & OID_MASK, (uid, gid, mode, flags)


def plan_fixups(volume, rows):
    paths = inode_paths(volume)
    expected, names = {}, {}
    for row in rows:
        path = os.fsencode(row["PAT"])
        if path in (b"", b"."):
            path = b""
        if path not in paths:
            raise ValueError(f"Path from Big Sur's fixup manifest is missing: {os.fsdecode(path)}")
        oid = paths.pop(path)
        target = (row["UID"], row["GID"], MODES[row["TYP"]] | row["MOD"], row["FLG"])
        if oid in expected and expected[oid] != target:
            raise ValueError(f"Conflicting Big Sur metadata for linked inode {oid}")
        expected[oid] = target
        names[oid] = path
    if paths:
        raise ValueError(f"Unlisted paths in the Big Sur System volume: {len(paths)}")

    counts, flags, examples, edits = Counter(), Counter(), defaultdict(list), {}
    for oid, before in inode_metadata(volume.nodes()):
        after = expected.pop(oid, None)
        if after is None:
            continue  # Private APFS bookkeeping outside the root hierarchy.
        for field, a, b in zip(("Owner", "Group", "Mode", "Flags"), before, after):
            if a == b:
                continue
            counts[field] += 1
            if len(examples[field]) < 8:
                examples[field].append((os.fsdecode(names[oid]) or ".", a, b))
        if before[3] != after[3]:
            flags[f"{before[3]:08x}->{after[3]:08x}"] += 1
        if before != after:
            edits[oid] = before, after
    if expected:
        raise ValueError("No APFS inode records for some Big Sur fixups")
    return {"Inodes": len(names), "Differences": dict(counts), "FlagTransitions": dict(flags),
            "Examples": dict(examples)}, edits


def fixup_blocks(volume, edits):
    """Prepare only signed firmlink BSD-flag edits; never change file flags."""
    if volume.sealed or volume.snapshot_count:
        raise ValueError("Cannot restore Big Sur flags on a sealed or snapshotted volume")
    if any(before[:3] != after[:3] or not stat.S_ISDIR(before[2]) or not before[3] & SF_FIRMLINK or
           not after[3] & SF_FIRMLINK or (before[3] ^ after[3]) & ~FIRMLINK_FIXUP_FLAGS
           for before, after in edits.values()):
        raise ValueError("Big Sur needs more than the expected signed firmlink flag fixes")
    pending, blocks = dict(edits), {}
    for node in volume.nodes():
        if not node.leaf:
            continue
        data = None
        for entry in node.entries:
            key = u64(entry.key, 0)
            change = pending.pop(key & OID_MASK, None) if key >> 60 == INODE else None
            if change is None:
                continue
            before, after = change
            flags, uid, gid, mode = struct.unpack_from("<IIIH", entry.value, 68)
            if before != (uid, gid, mode, flags):
                raise ValueError("A Big Sur firmlink changed after its original metadata was checked")
            if data is None:
                data = bytearray(node.data)
            struct.pack_into("<I", data, entry.value_offset + 68, after[3])
        if data is not None:
            struct.pack_into("<Q", data, 0, block_checksum(data))
            blocks[node.address] = data
    if pending:
        raise ValueError("Missing Big Sur firmlink records during finalization")
    return blocks


def compare_archive_compression(volume, rows):
    """Check archive AFT (decmpfs type) and AFR (resource length) on disk."""
    paths = inode_paths(volume)
    expected = {paths[os.fsencode(row["PAT"])]: (row["PAT"], row["AFT"], row["AFR"])
                for row in rows if "AFT" in row}
    types, resources = {}, {}
    for node in volume.nodes():
        if not node.leaf:
            continue
        for entry in node.entries:
            key = u64(entry.key, 0)
            oid = key & OID_MASK
            if key >> 60 != XATTR or oid not in expected:
                continue
            name = entry.key[10:-1]
            flags, size = struct.unpack_from("<HH", entry.value)
            if name == b"com.apple.decmpfs":
                if flags & 3 != 2 or size < 16 or entry.value[4:8] != b"fpmc":
                    raise ValueError("Unexpected Big Sur compression header")
                types[oid] = struct.unpack_from("<I", entry.value, 8)[0]
            elif name == b"com.apple.ResourceFork":
                resources[oid] = u64(entry.value, 12) if flags & 3 == 1 else size
    counts, examples = Counter(), []
    for oid, (name, kind, size) in expected.items():
        before = types.get(oid, 0), resources.get(oid, 0)
        if before != (kind, size):
            counts[f"{before[0]}->{kind}" if before[0] != kind else "ResourceSize"] += 1
            if len(examples) < 8:
                examples.append((name, before, (kind, size)))
    return {"Entries": len(expected), "Differences": dict(counts), "Examples": examples}


def main():
    import argparse
    from host_install_apfs import attached_images
    from host_install_apfs_records import APFS, SparseBundle
    from host_install_bigsur_media import signed_asset

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("system", type=Path, help="Detached, newly generated System sparsebundle")
    parser.add_argument("--restore-signed-firmlink-flags", action="store_true",
                        help="Restore Apple's original BSD flags in this unpublished scratch image")
    parser.add_argument("--compression", action="store_true",
                        help="Also compare original compressed representation and resource lengths")
    args = parser.parse_args()
    if attached_images(args.system):
        raise ValueError("Detach the APFS scratch disk before auditing fixups")
    image = SparseBundle(args.system)
    container = APFS(image)
    volume, = (v for v in container.volumes() if v.role == 1)
    with signed_asset(args.package) as (_, archive):
        rows = original_fixups(archive)
    report, edits = plan_fixups(volume, rows)
    if args.restore_signed_firmlink_flags and edits:
        blocks = fixup_blocks(volume, edits)
        if attached_images(args.system):
            raise ValueError("The scratch disk was attached while preparing Big Sur fixups")
        for address, block in blocks.items():
            image.write(container.offset + BLOCK * address, block)
        container = APFS(image)
        volume, = (v for v in container.volumes() if v.role == 1)
        after, remaining = plan_fixups(volume, rows)
        if remaining:
            raise ValueError("Big Sur fixup readback did not match Apple's manifest")
        report = {"Before": report, "After": after, "ChangedBlocks": len(blocks)}
    if args.compression:
        report["Compression"] = compare_archive_compression(volume, rows)
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
