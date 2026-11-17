import io
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from host_install_apfs_records import APFS, BLOCK, block_checksum, node_records, u64
from host_install_bigsur_extents import Extent, FEXT, extent_records, tree_block_count, write_tree
from host_install_bigsur_snapshot import OMAP, SNAP_META, allocation_runs, omap_record, variable_leaf


class BigSurExtentTests(unittest.TestCase):
    def test_coalesces_only_physically_contiguous_ranges(self):
        extents = [Extent(90, 0, BLOCK, 600), Extent(90, BLOCK, BLOCK, 601),
                   Extent(90, 2 * BLOCK, BLOCK, 800), Extent(91, 0, BLOCK, 801)]
        records = list(extent_records(reversed(extents)))
        self.assertEqual([(struct.unpack("<QQ", k), struct.unpack("<QQ", v)) for k, v in records],
                         [((90, 0), (2 * BLOCK, 600)), ((90, 2 * BLOCK), (BLOCK, 800)),
                          ((91, 0), (BLOCK, 801))])
        with self.assertRaisesRegex(ValueError, "Overlapping"):
            list(extent_records([*extents, Extent(90, BLOCK, BLOCK, 604)]))

    def test_round_trips_a_physical_tree_with_an_index(self):
        rows = list(extent_records(Extent(i + 300, 0, BLOCK, i + 1000) for i in range(300)))
        output = io.BytesIO()
        info = write_tree(output, rows, start=990, xid=42)
        disk = APFS.__new__(APFS)
        tree = output.getvalue()
        disk.read = lambda address: tree[(address - 990) * BLOCK:(address - 989) * BLOCK]
        root = disk.read(info["RootBlock"])
        flags, level, _, _ = node_records(root, fixed=True)
        self.assertEqual((flags, level, u64(root, 8), u64(root, 16)), (5, 1, info["RootBlock"], 42))
        self.assertEqual(u64(root, 0), block_checksum(root))
        self.assertEqual(struct.unpack_from("<2Q", root, BLOCK - 16), (len(rows), info["BlockCount"]))
        readback = [(e.key, e.value) for node in disk.tree(info["RootBlock"], int, subtype=FEXT, fixed=True)
                    if node.leaf for e in node.entries]
        self.assertEqual(readback, rows)

    def test_seal_transaction_uses_fixed_node_capacity_and_native_snapshot_rows(self):
        records = [omap_record(3000 + i, 42, 7000 + i, 8) for i in range(112)]
        output = io.BytesIO()
        info = write_tree(output, records, start=99, xid=42, subtype=OMAP)
        self.assertEqual(info["BlockCount"], tree_block_count(112))
        self.assertEqual(info["BlockCount"], 2)  # 112 fit in a leaf, but not with the root footer.
        self.assertEqual(struct.unpack_from("<H", output.getvalue(), 42)[0], 448)
        row = (struct.pack("<Q", 1 << 60 | 42), bytes(135))
        root = variable_leaf(200, 42, SNAP_META, 0x52, [row])
        entry, = node_records(root)[3]
        self.assertEqual((entry.key, entry.value), row)
        self.assertEqual(list(allocation_runs({101, 103, 5, 102})), [(5, 1), (101, 3)])


if __name__ == "__main__":
    unittest.main()
