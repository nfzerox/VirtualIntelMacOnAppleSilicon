"""Stream authenticated metadata from an existing VM for layout tests only."""

import argparse
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from host_install_apfs_records import APFS, MappedQCOWImage, hashed_tree, u64


def replay(path, qemu_img, root_hash, omit_digests, output):
    # Deliberately open the reference only for reading. The frozen tree can
    # match the installer even when a later change broke the live seal.
    with path.open("rb") as stream:
        container = APFS(MappedQCOWImage.from_qemu(stream, qemu_img))
        system, = (v for v in container.volumes() if v.role == 1)
        volume = system.superblock
        ext = container.object(system.resolve(u64(volume, 0x3e8)), 0x1d)
        xid = u64(ext, 40)
        name = b"com.apple.os.update-" + root_hash.hex().upper().encode("ascii") + b"\0"
        record, = (e.value for n in container.tree(u64(volume, 152), int, subtype=16)
                   if n.leaf for e in n.entries if u64(e.key, 0) == (1 << 60) | xid
                   and e.value[50:] == name)
        frozen = container.object(u64(record, 8), 13)
        if (frozen[32:36] != b"APSB" or frozen[240:256] != system.uuid.bytes
                or u64(frozen, 16) != xid or not u64(frozen, 56) & 0x20):
            raise ValueError("The reference does not have the expected frozen System volume")
        resolve = container.object_map(u64(volume, 128), xid, headerless=True)
        for node in hashed_tree(container.read, resolve, u64(frozen, 136), root_hash):
            if node.leaf:
                output.write(b"".join(
                    struct.pack("<HH", len(e.key), len(e.value)) + e.key + e.value
                    for e in node.entries if not omit_digests or e.key[7] >> 4 != 13
                ))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path, help="Standalone QCOW2; always opened read-only")
    parser.add_argument("qemu_img", type=Path)
    parser.add_argument("signed_root", help="SHA-256 root taken from the original Apple installer")
    parser.add_argument("--omit-digests", action="store_true", help="Replay with the installer's digest.db")
    args = parser.parse_args()
    try:
        root_hash = bytes.fromhex(args.signed_root)
    except ValueError:
        parser.error("signed_root must be a hexadecimal SHA-256 digest")
    if len(root_hash) != 32:
        parser.error("signed_root must be a SHA-256 digest")
    replay(args.reference, args.qemu_img, root_hash, args.omit_digests, sys.stdout.buffer)


if __name__ == "__main__":
    main()
