"""Compare a media-built Big Sur System's canonical records to its signed format."""

from collections import Counter, defaultdict
from dataclasses import dataclass
import struct
import subprocess
import tempfile

from host_install_apfs_records import DREC, INODE, OID_MASK, XATTR, u64
from host_install_bigsur_fixups import inode_paths
from host_install_bigsur_media import PAYLOAD, run_archive


DSTREAM_ID = 6
FILE_EXTENT = 8
FILE_INFO = 13
FAST_PROMOTE = 0x20000
ROOTLESS = b"com.apple.rootless\0"


@dataclass
class SealMetadata:
    fast_promote: set[int]
    rootless: dict[int, bytes]


def apple_fixup_entries(stream):
    """Read metadata-only AA01 records emitted by Apple's archive converter."""
    while magic := stream.read(4):
        if magic != b"AA01":
            raise ValueError("Expected a Big Sur AA01 metadata record")
        header_length, = struct.unpack("<H", stream.read(2))
        if header_length < 6:
            raise ValueError("Invalid Big Sur AA01 header")
        data = stream.read(header_length - 6)
        if len(data) != header_length - 6:
            raise ValueError("Truncated Big Sur AA01 header")
        offset, fields = 0, {}
        while offset < len(data):
            tag, kind = data[offset:offset + 3], data[offset + 3:offset + 4]
            offset += 4
            if kind in (b"1", b"2", b"3", b"4", b"8", b"A", b"B", b"C"):
                width = {b"A": 2, b"B": 4, b"C": 8}.get(kind, int(kind) if kind.isdigit() else 0)
                end = offset + width
                value = int.from_bytes(data[offset:end], "little")
            elif kind == b"P":
                width, = struct.unpack_from("<H", data, offset)
                offset += 2
                end = offset + width
                value = data[offset:end]
            else:
                raise ValueError(f"Unexpected AA01 fixup field {tag!r}/{kind!r}")
            if end > len(data) or tag in fields:
                raise ValueError("Invalid AA01 fixup field")
            fields[tag] = value
            offset = end
        size = fields.get(b"XAT", 0)
        blob = stream.read(size)
        if len(blob) != size or any(fields.get(key) for key in (b"DAT", b"YEC")):
            raise ValueError("Unexpected data in AA01 metadata stream")
        attributes, offset = {}, 0
        while offset < len(blob):
            length, = struct.unpack_from("<I", blob, offset)
            end = offset + length
            if length < 6 or end > len(blob):
                raise ValueError("Invalid Big Sur Apple Archive attribute")
            name, value = blob[offset + 4:end].split(b"\0", 1)
            if name in attributes:
                raise ValueError("Duplicate Big Sur Apple Archive attribute")
            attributes[name] = value
            offset = end
        yield fields, attributes


def original_seal_metadata(archive, volume):
    import io
    paths = inode_paths(volume)
    fast_promote, rootless = set(), {}

    def append(stream):
        for fields, xattrs in apple_fixup_entries(stream):
            oid = paths[fields[b"PAT"]]
            if fields.get(b"FLI", 0) & FAST_PROMOTE:
                fast_promote.add(oid)
            if ROOTLESS[:-1] in xattrs:
                rootless[oid] = xattrs[ROOTLESS[:-1]]

    # fixup.manifest contains the original protected bytes as well as the
    # archive's inode flags. No native host security attribute is changed.
    with tempfile.TemporaryFile() as output:
        run_archive(archive, PAYLOAD + "fixup.manifest", "convert", "-a", "raw", output=output)
        output.seek(0)
        append(output)
    raw = subprocess.check_output(("/usr/bin/aa", "convert", "-a", "raw"),
                                  input=archive.read(PAYLOAD + "firmlinks_payload"))
    append(io.BytesIO(raw))
    return SealMetadata(fast_promote, rootless)


def canonical_record(entry, oid_map, stamp, policy=None):
    raw = u64(entry.key, 0)
    kind, oid = raw >> 60, raw & OID_MASK
    if kind == FILE_EXTENT:
        return None  # Sealed volumes store physical file extents in a separate tree.
    if kind not in (INODE, XATTR, DSTREAM_ID, DREC):
        raise ValueError(f"Unexpected unsealed Big Sur record type {kind}")
    key = struct.pack("<Q", (kind << 60) | oid_map.get(oid, oid)) + entry.key[8:]
    value = bytearray(entry.value)
    if kind == INODE:
        parent, private = struct.unpack_from("<QQ", value)
        struct.pack_into("<6Q", value, 0, oid_map.get(parent, parent), oid_map.get(private, private), *([stamp] * 4))
        if policy is not None:
            # apfs_sealvolume resets the protection class, write generation,
            # and I/O counters in the inode's dstream. Preserve the media's
            # FAST_PROMOTE bit, which the current host does not always set.
            internal = u64(value, 48) & ~FAST_PROMOTE
            struct.pack_into("<Q", value, 48, internal | (FAST_PROMOTE if oid in policy.fast_promote else 0))
            struct.pack_into("<II", value, 60, 0, 1)
            if len(value) > 92:
                count, _ = struct.unpack_from("<HH", value, 92)
                payload = 96 + 4 * count
                for index in range(count):
                    field, _, length = struct.unpack_from("<BBH", value, 96 + 4 * index)
                    if payload + length > len(value):
                        raise ValueError("Invalid Big Sur inode extended field")
                    if field == 8:
                        if length != 40:
                            raise ValueError("Unexpected Big Sur inode stream size")
                        struct.pack_into("<QQ", value, payload + 24, 1, 1)
                    payload += (length + 7) & ~7
    elif kind == XATTR and struct.unpack_from("<H", value)[0] & 3 == 1:
        stream = u64(value, 4)
        struct.pack_into("<Q", value, 4, oid_map.get(stream, stream))
        if policy is not None:
            if len(value) != 52:
                raise ValueError("Unexpected Big Sur extended-attribute stream size")
            struct.pack_into("<QQ", value, 36, 1, 1)
    elif kind == DREC:
        child = u64(value, 0)
        struct.pack_into("<QQ", value, 0, oid_map.get(child, child), stamp)
    return key, bytes(value)


def metadata_records(nodes):
    for node in nodes:
        if node.leaf:
            yield from node.entries


def canonical_metadata(volume, oid_map, stamp, policy):
    source = {}
    for entry in metadata_records(volume.nodes()):
        record = canonical_record(entry, oid_map, stamp, policy)
        if record is not None:
            key, value = record
            if key in source:
                raise ValueError("Duplicate candidate canonical record")
            source[key] = value
    if policy is not None:
        for actual, value in policy.rootless.items():
            key = struct.pack("<QH", (XATTR << 60) | oid_map.get(actual, actual), len(ROOTLESS)) + ROOTLESS
            source[key] = struct.pack("<HH", 2, len(value)) + value
    return source


def compare_canonical_records(volume, oid_map, stamp, reference_nodes, policy=None):
    """Compare, never copy, authenticated reference records."""
    source = canonical_metadata(volume, oid_map, stamp, policy)
    matched, changed, missing, extra, fields = Counter(), Counter(), Counter(), Counter(), defaultdict(Counter)
    generations, inode_flags = Counter(), Counter()
    missing_attributes = []
    for entry in metadata_records(reference_nodes):
        kind = u64(entry.key, 0) >> 60
        if kind == FILE_INFO:
            continue  # Independently derived and checked against Apple's digest.db.
        candidate = source.pop(entry.key, None)
        if candidate is None:
            missing[kind] += 1
            if kind == XATTR and len(missing_attributes) < 10:
                missing_attributes.append((u64(entry.key, 0) & OID_MASK,
                                           entry.key[10:-1].decode("utf-8", "backslashreplace"), len(entry.value)))
        elif candidate == entry.value:
            matched[kind] += 1
        else:
            changed[kind] += 1
            if kind == INODE:
                a, b = (struct.unpack_from("<I", data, 64)[0] for data in (candidate, entry.value))
                if a != b:
                    generations[a, b] += 1
                a, b = (u64(data, 48) for data in (candidate, entry.value))
                if a != b:
                    inode_flags[a ^ b] += 1
            if len(candidate) != len(entry.value):
                fields[kind]["length"] += 1
            else:
                for field in {i // 4 * 4 for i, (a, b) in enumerate(zip(candidate, entry.value)) if a != b}:
                    fields[kind][str(field)] += 1
    for key in source:
        extra[u64(key, 0) >> 60] += 1
    return {"Matched": dict(matched), "Changed": dict(changed), "Missing": dict(missing),
            "Extra": dict(extra), "Fields": {kind: dict(counts) for kind, counts in fields.items()},
            "MissingAttributes": missing_attributes, "GenerationTransitions": generations.most_common(20),
            "InternalFlagXOR": inode_flags.most_common(20)}


def main():
    import argparse
    import json
    from pathlib import Path
    import plistlib
    from host_install_apfs import attached_images
    from host_install_apfs_records import APFS, MappedQCOWImage, SparseBundle
    from host_install_bigsur_boot import intel_root_hash
    from host_install_bigsur_media import signed_asset

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("system", type=Path, help="New detached System sparsebundle")
    parser.add_argument("remap", type=Path, help="Canonical plist generated from this disk and original media")
    parser.add_argument("reference", type=Path, help="Stopped QCOW2, opened read-only")
    parser.add_argument("qemu_img", type=Path)
    parser.add_argument("--apply-media-rules", action="store_true",
                        help="Compare the full media-derived seal-normalization policy (read-only)")
    args = parser.parse_args()
    if attached_images(args.system):
        raise ValueError("Detach the new APFS System before comparing its canonical records")
    scratch, = (v for v in APFS(SparseBundle(args.system)).volumes() if v.role == 1)
    with signed_asset(args.package) as (_, archive):
        root, = (n for n in archive.namelist() if n.endswith(".x86.root_hash"))
        expected = intel_root_hash(archive.read(root))
        policy = original_seal_metadata(archive, scratch) if args.apply_media_rules else None
    with args.remap.open("rb") as stream:
        remap = plistlib.load(stream)
    stamp = remap.pop("CHANGE")
    oid_map = {int(k): v for k, v in remap.items()}
    with args.reference.open("rb") as stream:
        reference, = (v for v in APFS(MappedQCOWImage.from_qemu(stream, args.qemu_img)).volumes() if v.role == 1)
        _, _, nodes = reference.authenticated_snapshot(expected)
        print(json.dumps(compare_canonical_records(scratch, oid_map, stamp, nodes, policy), indent=2))


if __name__ == "__main__":
    main()
