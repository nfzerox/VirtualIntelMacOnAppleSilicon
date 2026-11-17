import io
from pathlib import Path
import struct
import sys
from types import SimpleNamespace
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from host_install_bigsur_seal import compare_tree, decode_mtree, digest_records, mtree_entries


MTREE = (
    b"# manifest\n/set type=file xattrsdigest=none.0\n. type=dir inode=2\n"
    b"System type=dir \\\n   inode=101\n"
    b"App\\040Name type=link inode=102\n..\n"
    b"Library type=dir inode=103\nFile inode=104\n..\n..\n"
)


class BigSurSealTests(unittest.TestCase):
    def test_converts_aligned_signed_digest_rows_and_skips_auxiliary_records(self):
        digest = bytes(range(32))
        value = struct.pack("<HB", 4, len(digest)) + digest
        row = lambda offset: struct.pack("<QQ", 21, offset) + value + b"\0"
        result = list(digest_records(io.BytesIO(row(0) + row(2) + row(16384))))
        self.assertEqual(result, [
            (struct.pack("<QQ", (13 << 60) | 21, (1 << 56)), value),
            (struct.pack("<QQ", (13 << 60) | 21, (1 << 56) | 16384), value),
        ])
        with self.assertRaisesRegex(ValueError, "Truncated"):
            list(digest_records(io.BytesIO(row(0)[:-1])))

    def test_native_mtree_hierarchy_and_filename_escapes(self):
        entries = dict(mtree_entries(io.BytesIO(MTREE)))
        self.assertEqual(entries[b"System", b"App Name"][b"type"], b"link")
        self.assertEqual(entries[b"Library", b"File"][b"type"], b"file")
        with self.assertRaisesRegex(ValueError, "Incomplete"):
            list(mtree_entries(io.BytesIO(MTREE[:-3])))

    def test_inventory_separates_missing_and_wrong_entries_from_unreachable_metadata(self):
        entries = [(2, b"System", 11, 4), (11, b"App Name", 12, 8),
                   (2, b"Library", 13, 4), (2, b"Unexpected", 14, 4),
                   (14, b"Child", 15, 8), (1, b"private data", 99, 4)]
        result = compare_tree(SimpleNamespace(directory_entries=lambda: entries), io.BytesIO(MTREE))
        self.assertEqual(result["Matched"], 3)
        self.assertEqual(result["MissingExamples"], ["Library/File"])
        self.assertEqual(result["WrongTypeExamples"], ["System/App Name"])
        self.assertEqual(result["UnexpectedExamples"], ["Unexpected", "Unexpected/Child"])

    def test_stream_rejects_truncated_and_non_intel_mtree(self):
        def element(tag, value):
            return bytes((tag, len(value))) + value

        apple_archive = b"AA01 fixture"
        pbze = struct.pack(">4sQQQ", b"pbze", 4096, len(apple_archive), len(apple_archive)) + apple_archive
        parts = (element(0x16, b"IM4P"), element(0x16, b"xmtr"), element(0x16, b"0"), element(4, pbze))
        im4p = element(0x30, b"".join(parts))
        self.assertEqual(list(decode_mtree(io.BytesIO(im4p))), [apple_archive])
        with self.assertRaisesRegex(ValueError, "Truncated"):
            list(decode_mtree(io.BytesIO(im4p[:-1])))
        with self.assertRaisesRegex(ValueError, "Not a Big Sur Intel"):
            list(decode_mtree(io.BytesIO(im4p.replace(b"xmtr", b"amtr"))))


if __name__ == "__main__":
    unittest.main()
