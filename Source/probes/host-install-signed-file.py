#!/usr/bin/env python3
"""Read one uncompressed signed System stream in a stopped Big Sur disk.

Development forensics only. Never mounts, repairs or writes a guest image.
Also reports prior unsealed checkpoint maps when those are still available.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import struct
import sys

SOURCE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SOURCE / "tools"))
from host_install_apfs_records import APFS, BLOCK, OID_MASK, block_checksum, u32, u64
from host_install_bigsur_cache_storage import extent_reader
from host_install_bigsur_file_storage import inode_stream, verify_plain_stream


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("disk", type=Path)
    parser.add_argument("--qemu-img", type=Path)
    parser.add_argument("--root-hash", required=True)
    parser.add_argument("--inode", type=int, required=True)
    args = parser.parse_args()
    root = bytes.fromhex(args.root_hash)
    if len(root) != 32:
        parser.error("The externally supplied Intel root hash must be 32 bytes.")
    spec = importlib.util.spec_from_file_location("disk_verifier", SOURCE / "scripts/verify-host-install-bigsur-disk.py")
    verifier = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(verifier)
    report = {"Inode": args.inode, "RootHash": root.hex()}
    with verifier.open_disk(args.disk, args.qemu_img) as disk:
        container = APFS(disk)
        system, = (volume for volume in container.volumes() if volume.role == 1)
        _, frozen, nodes = system.authenticated_snapshot(root)
        stream, digests = None, []
        for node in nodes:
            for row in node.entries if node.leaf else ():
                tag = u64(row.key, 0)
                if tag >> 60 == 3 and tag & OID_MASK == args.inode:
                    stream = inode_stream(row.value)
                if tag >> 60 == 13 and tag & OID_MASK == args.inode:
                    algorithm_offset = u64(row.key, 8)
                    pages, width = struct.unpack_from("<HB", row.value)
                    if algorithm_offset >> 56 != 1 or width != 32 or len(row.value) != 35:
                        raise ValueError("Unsupported signed file digest")
                    digests.append((algorithm_offset & ((1 << 56) - 1), pages * BLOCK, row.value[3:]))
        if not stream or stream[0] != args.inode or not digests:
            raise ValueError("This probe requires an uncompressed, inode-owned signed stream")
        extents = []
        for node in container.tree(u64(frozen, 0x408), int, subtype=31, fixed=True):
            for row in node.entries if node.leaf else ():
                if u64(row.key, 0) == stream[0]:
                    count, physical = struct.unpack("<QQ", row.value)
                    extents.append((u64(row.key, 8), count, physical))
        report.update(SystemUUID=str(system.uuid), LogicalBytes=stream[1],
                      Extents=sorted(extents), DigestCount=len(digests), ContainerOffset=container.offset)
        try:
            verify_plain_stream(extent_reader(disk, container.offset, extents), stream[1], digests)
            report["Valid"] = True
        except ValueError as failure:
            report.update(Valid=False, Error=str(failure))

        start, count = u64(container.superblock, 112), u32(container.superblock, 104)
        prior = {}
        for address in range(start, start + count):
            block = container.read(address)
            xid = u64(block, 16)
            if block[32:40] == container.superblock[32:40] and u64(block, 0) == block_checksum(block) and xid < container.xid:
                prior[xid] = block
        report["PriorMaps"] = []
        for xid, block in sorted(prior.items(), reverse=True)[:3]:
            previous = APFS.__new__(APFS)
            previous.image, previous.offset, previous.blocks = disk, container.offset, container.blocks
            previous.superblock, previous.xid = block, xid
            try:
                volume, = (v for v in previous.volumes() if v.uuid == system.uuid)
                if volume.sealed:
                    continue
                rows = []
                for node in volume.nodes():
                    for row in node.entries if node.leaf else ():
                        tag = u64(row.key, 0)
                        if tag >> 60 == 8 and tag & OID_MASK == stream[0]:
                            length, physical, crypto = struct.unpack("<QQQ", row.value)
                            rows.append((u64(row.key, 8), length, physical, crypto))
                report["PriorMaps"].append({"Transaction": xid, "Extents": sorted(rows)})
            except (KeyError, ValueError) as failure:
                report["PriorMaps"].append({"Transaction": xid, "Error": str(failure)})
    print(json.dumps(report, indent=2))
    return 0 if report["Valid"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
