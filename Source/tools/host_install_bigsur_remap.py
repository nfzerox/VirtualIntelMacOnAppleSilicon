"""Derive the Intel sealer's canonical object map from original media and a new disk."""

from collections import Counter, defaultdict
from pathlib import Path
import re
import struct

from host_install_apfs_records import INODE, OID_MASK, XATTR, u64
from host_install_bigsur_fixups import inode_paths
from host_install_bigsur_seal import mtree_entries


def timestamp_ns(value):
    match = re.fullmatch(rb"([0-9]+)\.([0-9]{1,9})", value)
    if not match:
        raise ValueError("Invalid timestamp in Apple's Big Sur seal manifest")
    return int(match[1]) * 10**9 + int(match[2].ljust(9, b"0"))


def system_objects(volume):
    streams, private = defaultdict(list), {}
    for node in volume.nodes():
        if not node.leaf:
            continue
        for entry in node.entries:
            key = u64(entry.key, 0)
            kind, oid = key >> 60, key & OID_MASK
            if kind == XATTR and struct.unpack_from("<H", entry.value)[0] & 3 == 1:
                streams[oid].append((entry.key[10:-1], u64(entry.value, 4)))
            elif kind == INODE:
                source = u64(entry.value, 8)
                if source != oid:
                    private[oid] = source
    return streams, private


def remapping_dictionary(volume, mtree, *, audit=False):
    """Map installed inode and external xattr IDs to the signed mtree's IDs.

    The Big Sur sealer consumes decimal-string keys and 64-bit integer
    values; its optional CHANGE key normalizes all five APFS timestamps.
    """
    paths = inode_paths(volume)
    streams, private = system_objects(volume)
    remap, inverse, timestamp = {}, {}, None
    differences, examples = Counter(), defaultdict(list)
    inodes = external = 0

    def bind(actual, canonical):
        if ((actual in remap and remap[actual] != canonical) or
                (canonical in inverse and inverse[canonical] != actual)):
            raise ValueError("Conflicting canonical object IDs in Big Sur System")
        if not 0 < actual <= OID_MASK or not 0 < canonical <= OID_MASK:
            raise ValueError("Invalid canonical object ID in Big Sur System")
        remap[actual] = canonical
        inverse[canonical] = actual

    def mismatch(reason, name):
        if not audit:
            raise ValueError(f"{reason}: {name!r}")
        differences[reason] += 1
        if len(examples[reason]) < 5:
            examples[reason].append(name.decode("utf-8", "backslashreplace"))

    for components, record in mtree_entries(mtree):
        name = b"/".join(components)
        actual = paths.pop(name, None)
        if actual is None:
            raise ValueError(f"Missing inode for Apple's canonical path {name!r}")
        if not name:
            times = {timestamp_ns(record[k]) for k in (b"time", b"btime", b"ctime", b"ptime")}
            if len(times) != 1:
                raise ValueError("Big Sur's canonical root timestamps do not agree")
            timestamp, = times
        for field in (b"time", b"btime", b"ctime", b"ptime", b"atime"):
            if field in record and timestamp_ns(record[field]) != timestamp:
                raise ValueError("Big Sur mtree requires per-object timestamp handling")
        if int(record[b"siblingid"]):
            raise ValueError("Big Sur mtree requires hardlink sibling remapping")
        bind(actual, int(record[b"inode"]))
        if actual in private:
            mismatch("No canonical ID for separate file stream", name)
        inodes += 1
        stream = int(record[b"xattrsdigest"].rsplit(b".", 1)[1])
        present = streams.get(actual, ())
        if len(present) != bool(stream):
            mismatch(f"External attribute count {len(present)} instead of {int(bool(stream))}", name)
            continue
        if stream:
            _, installed_stream = present[0]
            bind(installed_stream, stream)
            external += 1
    if paths or timestamp is None:
        raise ValueError("Installed paths and Apple's canonical Big Sur mtree differ")
    result = {str(actual): canonical for actual, canonical in remap.items() if actual != canonical}
    result["CHANGE"] = timestamp
    report = {"Inodes": inodes, "ExternalStreams": external, "RemappedObjects": len(result) - 1,
              "Unresolved": dict(differences), "Examples": dict(examples)}
    return result, report


def main():
    import argparse
    import json
    import plistlib
    from host_install_apfs import attached_images
    from host_install_apfs_records import APFS, SparseBundle

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("system", type=Path, help="Detached scratch System sparsebundle")
    parser.add_argument("mtree", type=Path, help="mtree.txt extracted from Apple's original signed installer")
    parser.add_argument("--output", type=Path, help="Write a new plist only when all object IDs resolve")
    args = parser.parse_args()
    if attached_images(args.system):
        raise ValueError("Detach the APFS scratch disk before deriving canonical IDs")
    volume, = (v for v in APFS(SparseBundle(args.system)).volumes() if v.role == 1)
    with args.mtree.open("rb") as manifest:
        remap, report = remapping_dictionary(volume, manifest, audit=args.output is None)
    if args.output:
        with args.output.open("xb") as output:
            plistlib.dump(remap, output, sort_keys=False)
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
