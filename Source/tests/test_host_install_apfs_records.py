from hashlib import sha256
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch
from uuid import UUID

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import host_install_apfs_records as records


class FirmlinkTests(unittest.TestCase):
    def test_authenticated_tree_checks_child_hashes_and_root_relative_ids(self):
        def block(flags, level, value):
            data = bytearray(records.BLOCK)
            struct.pack_into("<HHIHH", data, 32, flags, level, 1, 0, 8)
            end = records.BLOCK - (40 if flags & 1 else 0)
            struct.pack_into("<HHHH", data, 56, 0, 8, len(value), len(value))
            data[64:72] = struct.pack("<Q", 2)
            data[end - len(value):end] = value
            return bytes(data)

        leaf = block(0x12, 0, b"payload")
        index = block(0x18, 1, struct.pack("<Q", 5) + sha256(leaf).digest())
        root = block(0x19, 2, struct.pack("<Q", 3) + sha256(index).digest())
        objects = {7: root, 10: index, 12: leaf}
        nodes = list(records.hashed_tree(objects.__getitem__, int, 7, sha256(root).digest()))
        self.assertEqual([n.address for n in nodes], [7, 10, 12])
        self.assertEqual(nodes[-1].entries[0].value, b"payload")
        objects[12] = leaf[:300] + b"x" + leaf[301:]
        with self.assertRaisesRegex(ValueError, "node hash mismatch"):
            list(records.hashed_tree(objects.__getitem__, int, 7, sha256(root).digest()))

    def test_signed_snapshot_requires_matching_seal_and_snapshot_record(self):
        root_hash = bytes(range(32))
        snapshot_uuid = UUID(int=937)
        xid = 81
        superblock = bytearray(records.BLOCK)
        superblock[32:36] = b"APSB"
        for offset, value in ((56, 0x20), (152, 14), (216, 1), (0x3e8, 12), (0x400, 13)):
            struct.pack_into("<Q", superblock, offset, value)
        meta, extension = bytearray(records.BLOCK), bytearray(records.BLOCK)
        struct.pack_into("<IIIIQ", meta, 32, 1, 0, 1, 128, 0)
        meta[128:160] = root_hash
        struct.pack_into("<I4xQ", extension, 32, 1, xid)
        extension[48:64] = snapshot_uuid.bytes
        name = b"com.apple.os.update-" + root_hash.hex().upper().encode() + b"\0"
        entry = records.Entry(0, struct.pack("<Q", (1 << 60) | xid), 0, bytes(50) + name)
        node = records.Node(14, b"", True, [entry])
        container = SimpleNamespace(object=lambda _, kind: bytes({0x1e: meta, 0x1d: extension}[kind]),
                                    tree=lambda *_, **__: (node,))
        volume = records.Volume(container, bytes(superblock))
        volume.resolve = lambda oid: oid
        self.assertEqual(volume.signed_root_snapshot(root_hash), snapshot_uuid)
        with self.assertRaisesRegex(ValueError, "does not match Apple's"):
            volume.signed_root_snapshot(bytes(32))
        struct.pack_into("<Q", extension, 40, xid + 1)
        with self.assertRaisesRegex(ValueError, "does not match its seal identity"):
            volume.signed_root_snapshot(root_hash)

    def test_maps_qcow_reference_ranges_without_copying_holes(self):
        extents = [dict(start=0, length=3, depth=0, zero=False, offset=5),
                   dict(start=3, length=4, depth=0, zero=True),
                   dict(start=7, length=3, depth=0, zero=False, offset=1)]
        with tempfile.TemporaryFile() as source:
            source.write(b"abcdefghij")
            source.flush()
            image = records.MappedQCOWImage(source, extents)
            self.assertEqual(image.read(1, 8), b"gh\0\0\0\0bc")
            self.assertEqual(image.read(10, 0), b"")
            self.assertEqual(list(image.allocated_ranges(1, 8)), [(1, 2), (7, 2)])
            with self.assertRaisesRegex(ValueError, "outside"):
                image.read(8, 3)
            with self.assertRaisesRegex(ValueError, "backed"):
                records.MappedQCOWImage(source, [{**extents[0], "depth": 1}])
            with self.assertRaisesRegex(ValueError, "file offset"):
                records.MappedQCOWImage(source, [{**extents[0], "compressed": True}])

    def test_block_checksum_roundtrip(self):
        block = bytearray(records.BLOCK)
        struct.pack_into("<IQ", block, 16, 19, 0x1122334455667788)
        checksum = records.block_checksum(block)
        struct.pack_into("<Q", block, 0, checksum)
        self.assertEqual(records.block_checksum(block), checksum)
        block[-1] = 1
        self.assertNotEqual(records.block_checksum(block), checksum)

    def test_changes_only_the_expected_inode_and_reserved_attribute(self):
        uid = UUID(int=1)
        oid = 73
        destination = b"Applications\0"
        inode = bytearray(92)
        struct.pack_into("<IH", inode, 76, 0, 0o040755)
        key = struct.pack("<QH", (4 << 60) | oid, len(records.PLACEHOLDER)) + records.PLACEHOLDER
        value = struct.pack("<HH", 2, len(destination)) + destination
        leaf = records.Node(12, bytes(records.BLOCK), True, [
            records.Entry(60, struct.pack("<Q", (3 << 60) | oid), 220, bytes(inode)),
            records.Entry(96, key, 400, value),
        ])
        separator = records.Node(11, bytes(records.BLOCK), False, [records.Entry(96, key, 400, b"")])
        volume = SimpleNamespace(uuid=uid, role=1, snapshot_count=0,
                                 nodes=lambda: iter((leaf, separator)))
        with patch.object(records, "attached_images", return_value=[]), patch.object(records, "detached_image") as bundle, \
                patch.object(records, "APFS") as apfs:
            apfs.return_value.volumes.return_value = [volume]
            apfs.return_value.offset = 4096
            with self.assertRaisesRegex(ValueError, "Unexpected firmlink attribute"):
                records.finalize_firmlinks(Path("disk.sparsebundle"), uid, {oid: b"Wrong\0"})
            bundle.return_value.write.assert_not_called()
            records.finalize_firmlinks(Path("disk.sparsebundle"), uid, {oid: destination})
            changes = dict(call.args for call in bundle.return_value.write.call_args_list)
            self.assertEqual(set(changes), {4096 + 11 * 4096, 4096 + 12 * 4096})
            for block in changes.values():
                self.assertEqual(records.u64(block, 0), records.block_checksum(block))
                self.assertEqual(block[106:106 + len(records.FIRMLINK)], records.FIRMLINK)
            self.assertEqual(records.u32(changes[4096 + 12 * 4096], 288), records.SF_FIRMLINK)


if __name__ == "__main__":
    unittest.main()
