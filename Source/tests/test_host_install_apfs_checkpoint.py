from pathlib import Path
import struct
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from host_install_apfs_checkpoint import Checkpoint, SpaceMap, checksummed, clear_runs, next_window
from host_install_apfs_free_queue import FreeQueue, QUEUE_HEADER, encode_node
from host_install_apfs_records import APFS, BLOCK, u64
from host_install_apfs_seed import JUMPSTART_FIELD, JUMPSTART_OBJECT, jumpstart_transaction


def object_header(oid, xid, kind, subtype=0):
    block = bytearray(BLOCK)
    struct.pack_into("<QQII", block, 8, oid, xid, kind, subtype)
    return block


def sample_container():
    nx = object_header(1, 41, 0x80000001)
    struct.pack_into("<QQ", nx, 88, 1100, 42)
    struct.pack_into("<IIQQ6I", nx, 104, 10, 10, 1, 11, 2, 1, 0, 2, 0, 1)
    struct.pack_into("<Q", nx, 152, 1024)
    cp = object_header(1, 41, 0x4000000c)
    struct.pack_into("<6I3Q", cp, 32, 1, 1, 0x80000005, 0, BLOCK, 0, 0, 1024, 11)
    sm = object_header(1024, 41, 0x80000005)
    struct.pack_into("<4I", sm, 32, BLOCK, 128, 2, 0)
    struct.pack_into("<QQIIQI", sm, 48, 256, 2, 1, 0, 86, 400)
    struct.pack_into("<QIIQQ", sm, 152, 16, 1, 4, 100, 104)
    struct.pack_into("<HHIII", sm, 320, 1, 3, 420, 428, 430)
    struct.pack_into("<Q", sm, 400, 105)
    struct.pack_into("<QH4H", sm, 420, 41, 0, 0xffff, 2, 3, 0xffff)
    cib = object_header(105, 41, 0x40000007)
    struct.pack_into("<IIQQIIQQQIIQ", cib, 32, 0, 2, 41, 0, 128, 8, 106,
                     41, 128, 128, 78, 107)
    old_bitmap = (0b1110).to_bytes(BLOCK, "little")
    storage = {0: checksummed(nx), 1: checksummed(cp), 2: checksummed(nx), 11: checksummed(sm),
               100: old_bitmap, 105: checksummed(cib), 106: ((1 << 120) - 1).to_bytes(BLOCK, "little"),
               107: (((1 << 50) - 1) << 78).to_bytes(BLOCK, "little")}
    container = APFS.__new__(APFS)
    container.read = storage.__getitem__
    container.superblock, container.xid, container.blocks = storage[0], 41, 256
    return container, storage


def efi_driver():
    driver = bytearray(b"x" * (BLOCK + 29))
    driver[:2] = b"MZ"
    struct.pack_into("<I", driver, 0x3c, 64)
    driver[64:70] = b"PE\0\0\x64\x86"
    struct.pack_into("<H", driver, 64 + 24, 0x20b)
    struct.pack_into("<H", driver, 64 + 92, 11)
    return bytes(driver)


class APFSCheckpointTests(unittest.TestCase):
    def test_bitmap_limits_and_checkpoint_wrap(self):
        self.assertEqual(list(clear_runs(b"\x25", 6)), [(1, 1), (3, 2)])
        self.assertEqual(next_window(9, 2, 10, [5, 6]), [0, 1])
        with self.assertRaisesRegex(ValueError, "Insufficient"):
            next_window(9, 2, 10, [0, 1])

    def test_checkpoint_reserves_across_chunks_and_preserves_prior_epoch(self):
        container, storage = sample_container()
        checkpoint = Checkpoint(container)
        space = SpaceMap(checkpoint)
        self.assertEqual(list(space.free_runs()), [(120, 86)])
        plan = space.allocation(80)
        self.assertEqual((plan.start, plan.count), (120, 80))
        self.assertEqual(plan.retired_ip, [106, 107, 105])
        self.assertFalse(storage.keys() & plan.writes.keys())
        publication = dict(checkpoint.blocks(ephemeral={(0, space.oid): plan.spaceman}))
        self.assertNotEqual(storage[0], publication[0])
        self.assertEqual(storage[2], container.superblock)
        storage.update(plan.writes)
        storage.update(publication)
        container.superblock, container.xid = storage[0], u64(storage[0], 16)
        self.assertEqual(list(SpaceMap(Checkpoint(container)).free_runs()), [(200, 6)])
        self.assertEqual(container.xid, 42)

    def test_jumpstart_reserves_driver_and_record_in_the_same_checkpoint(self):
        container, storage = sample_container()
        driver = efi_driver()
        with patch("host_install_apfs_seed.retirement_checkpoint", return_value=[(0, b"new anchor")]) as commit:
            writes = jumpstart_transaction(container, driver)
        checkpoint, allocation = commit.call_args.args
        address = allocation.start
        self.assertEqual(allocation.count, 3)
        self.assertEqual(u64(commit.call_args.kwargs["superblock"], JUMPSTART_FIELD), address)
        self.assertEqual(struct.unpack_from("<QQII4sIII", writes[address], 8),
                         (address, checkpoint.xid, JUMPSTART_OBJECT, 0, b"JSDR", 1, len(driver), 1))
        self.assertEqual(struct.unpack_from("<QQ", writes[address], 176), (address + 1, 2))
        self.assertEqual(writes[address + 1] + writes[address + 2][:29], driver)
        self.assertEqual(writes[address + 2][29:], bytes(BLOCK - 29))
        self.assertEqual(list(writes)[-1], 0)
        self.assertEqual(storage[0], container.superblock)

    def test_jumpstart_rejects_other_architectures_and_existing_drivers(self):
        container, _ = sample_container()
        arm = bytearray(efi_driver())
        struct.pack_into("<H", arm, 68, 0xaa64)
        with self.assertRaisesRegex(ValueError, "Intel APFS EFI"):
            jumpstart_transaction(container, arm)
        existing = bytearray(container.superblock)
        struct.pack_into("<Q", existing, JUMPSTART_FIELD, 5)
        container.superblock = existing
        with self.assertRaisesRegex(ValueError, "already contains"):
            jumpstart_transaction(container, efi_driver())

    def test_free_queue_ghosts_and_growth_round_trip(self):
        spaceman = bytearray(BLOCK)
        QUEUE_HEADER.pack_into(spaceman, 200, 3, 1026, 41, 8)
        storage = {1026: encode_node(1026, 41, 0, [(41, 100, 1), (41, 200, 2)], (2, 1))}
        cp = SimpleNamespace(xid=42, container=SimpleNamespace(blocks=4000),
                             read_ephemeral=lambda oid: storage[oid])
        queue = FreeQueue(cp, spaceman, 0)
        with self.assertRaisesRegex(ValueError, "Invalid APFS free queue block range"):
            queue.adding(spaceman, [(100, 1)], 1100)
        changes, discard, next_oid = queue.adding(spaceman, [(500 + i * 2, 1) for i in range(130)], 1100)
        self.assertEqual((len(changes), discard, next_oid), (3, set(), 1102))
        storage.update({oid: block for (_, oid), block in changes.items()})
        new = FreeQueue(cp, spaceman, 0)
        self.assertEqual(len(new.rows), 132)
        self.assertEqual(new.rows[-1], (42, 758, 1))
        self.assertEqual(sum(row[2] for row in new.rows), 133)


if __name__ == "__main__":
    unittest.main()
