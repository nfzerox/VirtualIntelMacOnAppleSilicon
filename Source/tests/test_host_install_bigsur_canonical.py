import io
from pathlib import Path
import plistlib
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from host_install_apfs_records import Entry, node_records
from host_install_bigsur_canonical import SealMetadata, apple_fixup_entries, canonical_record
from host_install_bigsur_tree import TreeNode, build_candidate_tree, canonical_leaves, export_verified_tree, index_bytes


class BigSurCanonicalTests(unittest.TestCase):
    def test_exports_only_an_authenticated_relative_block_file(self):
        records = {struct.pack("<Q", (3 << 60) | n): bytes(72) for n in range(1, 210)}
        expected, _ = build_candidate_tree(records, io.BytesIO())
        with tempfile.TemporaryDirectory() as folder:
            target = Path(folder) / "tree"
            with self.assertRaisesRegex(ValueError, "does not match"):
                export_verified_tree(target, records, io.BytesIO(), bytes(32))
            self.assertFalse(target.exists())
            self.assertEqual(export_verified_tree(target, records, io.BytesIO(), expected), expected)
            report = plistlib.loads((target / "TreeInfo.plist").read_bytes())
            self.assertEqual(report["RootHash"], expected)
            self.assertEqual((target / "FSRoot.blocks").stat().st_size, report["NodeCount"] * 4096)
            with self.assertRaises(FileExistsError):
                export_verified_tree(target, records, io.BytesIO(), expected)

    def test_reads_original_protected_xattr_from_metadata_archive(self):
        contents = b"com.apple.rootless\0MobileAsset"
        attribute = struct.pack("<I", len(contents) + 4) + contents
        header = b"PATP\x06\x00AssetsFLI4\x00\x00\x02\x00XATA" + struct.pack("<H", len(attribute))
        source = b"AA01" + struct.pack("<H", len(header) + 6) + header + attribute
        fields, xattrs = next(apple_fixup_entries(io.BytesIO(source)))
        self.assertEqual(fields[b"PAT"], b"Assets")
        self.assertEqual(fields[b"FLI"], 0x20000)
        self.assertEqual(xattrs, {b"com.apple.rootless": b"MobileAsset"})

    def test_normalizes_object_ids_stamps_generation_and_stream_counters(self):
        key = struct.pack("<Q", (3 << 60) | 91)
        inode = bytearray(140)
        struct.pack_into("<QQ", inode, 0, 2, 91)
        struct.pack_into("<II", inode, 60, 9, 45)
        struct.pack_into("<HHBBH", inode, 92, 1, 40, 8, 0, 40)
        struct.pack_into("<5Q", inode, 100, 123, 4096, 0, 777, 888)
        result, data = canonical_record(Entry(0, key, 0, inode), {91: 99}, 1234, SealMetadata({91}, {}))
        self.assertEqual(result, struct.pack("<Q", (3 << 60) | 99))
        self.assertEqual(struct.unpack_from("<6Q", data), (2, 99, 1234, 1234, 1234, 1234))
        self.assertEqual(struct.unpack_from("<Q", data, 48)[0], 0x20000)
        self.assertEqual(struct.unpack_from("<II", data, 60), (0, 1))
        self.assertEqual(struct.unpack_from("<5Q", data, 100), (123, 4096, 0, 1, 1))

    def test_first_leaf_reserves_the_footer_and_index_keeps_pivot_slot(self):
        records = [(struct.pack("<Q", (3 << 60) | n), bytes(47)) for n in range(1, 200)]
        first, second, *_ = canonical_leaves(records)
        self.assertLessEqual(len(node_records(first)[3]), len(node_records(second)[3]))
        child = TreeNode(3, 0, records[0][0], bytes(32))
        pivot = records[1][0]
        data = index_bytes(TreeNode(4, 1, child.key, children=[child], pivot_hole=pivot))
        self.assertEqual(struct.unpack_from("<HHHH", data, 64), (8, 8, 80, 40))
        self.assertEqual(data[128:136], b"\xff\xff\x08\x00" + pivot[4:])
        self.assertEqual(len(node_records(data)[3]), 1)


if __name__ == "__main__":
    unittest.main()
