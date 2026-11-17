from pathlib import Path
import stat
import struct
import sys
from types import SimpleNamespace
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from host_install_apfs_records import BLOCK, Entry, Node, SF_FIRMLINK, block_checksum
from host_install_bigsur_fixups import compare_archive_compression, fixup_blocks, plan_fixups


class BigSurFixupTests(unittest.TestCase):
    def test_audits_native_resource_fork_length_independently_from_file_flags(self):
        def attr(name, value):
            key = struct.pack("<QH", (4 << 60) | 3, len(name) + 1) + name + b"\0"
            return Entry(0, key, 0, value)

        header = struct.pack("<HH4sIQ", 2, 16, b"fpmc", 8, 171)
        fork = struct.pack("<HHQQ", 1, 48, 4, 146) + bytes(32)
        node = Node(5, b"", True, [attr(b"com.apple.decmpfs", header), attr(b"com.apple.ResourceFork", fork)])
        volume = SimpleNamespace(directory_entries=lambda: [(2, b"file", 3, 8)], nodes=lambda: [node])
        rows = [{"PAT": "file", "AFT": 8, "AFR": 146}]
        self.assertEqual(compare_archive_compression(volume, rows)["Differences"], {})
        rows[0]["AFR"] = 147
        self.assertEqual(compare_archive_compression(volume, rows)["Differences"], {"ResourceSize": 1})

    def test_only_unsealed_firmlink_flags_can_be_restored(self):
        flags = SF_FIRMLINK
        dir_mode = stat.S_IFDIR | 0o755
        data = bytearray(BLOCK)
        struct.pack_into("<IIIH", data, 168, flags, 0, 0, dir_mode)
        entry = Entry(0, struct.pack("<Q", (3 << 60) | 7), 100, bytes(data[100:192]))
        node = Node(15, bytes(data), True, [entry])
        volume = SimpleNamespace(sealed=False, snapshot_count=0, nodes=lambda: [node])
        before = (0, 0, dir_mode, flags)
        updated = fixup_blocks(volume, {7: (before, (0, 0, dir_mode, flags | 0x100000))})[15]
        self.assertEqual(struct.unpack_from("<I", updated, 168)[0], flags | 0x100000)
        self.assertEqual(struct.unpack_from("<Q", updated)[0], block_checksum(updated))
        with self.assertRaisesRegex(ValueError, "more than the expected"):
            fixup_blocks(volume, {7: (before, (501, 0, dir_mode, flags))})
        volume.snapshot_count = 1
        with self.assertRaisesRegex(ValueError, "snapshotted"):
            fixup_blocks(volume, {})

    def test_records_exact_flag_differences_and_rejects_path_loss(self):
        root = {"PAT": "", "TYP": "D", "UID": 0, "GID": 0, "MOD": 0o755, "FLG": 0}
        file = {"PAT": "file", "TYP": "F", "UID": 0, "GID": 0, "MOD": 0o644, "FLG": 0x80020}

        def inode(oid, mode, flags):
            record = bytearray(92)
            struct.pack_into("<IIIH", record, 68, flags, 0, 0, mode)
            return Entry(0, struct.pack("<Q", (3 << 60) | oid), 0, bytes(record))

        node = Node(15, b"", True, [inode(2, stat.S_IFDIR | 0o755, 0), inode(3, stat.S_IFREG | 0o644, 0x20)])
        volume = SimpleNamespace(directory_entries=lambda: [(2, b"file", 3, 8)], nodes=lambda: [node])
        report, edits = plan_fixups(volume, (root, file))
        self.assertEqual(report["Inodes"], 2)
        self.assertEqual(report["FlagTransitions"], {"00000020->00080020": 1})
        self.assertEqual(edits[3][1][3], 0x80020)
        with self.assertRaisesRegex(ValueError, "Unlisted paths"):
            plan_fixups(volume, (root,))


if __name__ == "__main__":
    unittest.main()
