from hashlib import sha256
from pathlib import Path
import struct
import sys
import tempfile
from types import SimpleNamespace
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from host_install_apfs import copy_container
from host_install_apfs_records import Entry, MappedQCOWImage
from host_install_bigsur_cache_storage import require_backed_range, require_stream_storage, verify_lzvn_fork
from host_install_bigsur_file_storage import verify_plain_stream, verify_sealed_file_data
from host_install_hfs_metadata import RawDisk


class BigSurCacheStorageTests(unittest.TestCase):
    def test_fext_coverage_rejects_a_short_sparsebundle_band(self):
        with tempfile.TemporaryDirectory() as folder:
            band = Path(folder) / "123"
            band.write_bytes(bytes(4096))
            image = SimpleNamespace(segments=lambda at, count: [(band, at, count)])
            with self.assertRaisesRegex(ValueError, "Backing file 123"):
                require_stream_storage(image, 0, 3, 512, [(0, 4096, 1)])

    def test_raw_disk_write_and_checker_copy_use_file_offsets(self):
        with tempfile.TemporaryDirectory() as folder:
            source, copy = (Path(folder) / name for name in ("disk.dmg", "container.raw"))
            source.write_bytes(bytes(12288))
            disk = RawDisk(source)
            try:
                disk.write(4096, b"system")
                require_stream_storage(disk, 0, 3, 6, [(0, 4096, 1)])
                copy_container(disk, copy, 4096, 8192)
                self.assertEqual(copy.read_bytes(), b"system" + bytes(8192 - 6))
                with self.assertRaisesRegex(ValueError, "outside the raw"):
                    disk.write(12287, b"overrun")
            finally:
                disk.close()

    def test_qcow_checker_copy_reads_virtual_offsets_and_allows_encoded_zeros(self):
        extents = [dict(start=0, length=3, depth=0, zero=False, offset=5),
                   dict(start=3, length=4, depth=0, zero=True),
                   dict(start=7, length=3, depth=0, zero=False, offset=1)]
        with tempfile.TemporaryDirectory() as folder, tempfile.TemporaryFile() as source:
            source.write(b"abcdefghij")
            source.flush()
            disk = MappedQCOWImage(source, extents)
            require_backed_range(disk, 1, 8)
            with self.assertRaisesRegex(ValueError, "outside the QCOW2"):
                require_backed_range(disk, 1, 10)
            copy = Path(folder) / "Container.raw"
            copy_container(disk, copy, 1, 8)
            self.assertEqual(copy.read_bytes(), b"gh\0\0\0\0bc")

    def test_cache_contents_match_signed_logical_bytes_not_fork_metadata(self):
        data = b"a" * 4096 + b"b" * 4096
        stored = struct.pack("<II", 8, 9 + len(data)) + b"\x06" + data
        digests = [(0, 4096, sha256(data[:4096]).digest()),
                   (4096, 4096, sha256(data[4096:]).digest())]
        verify_lzvn_fork(lambda at, size: stored[at:at + size], len(stored), len(data), digests)
        bad = stored[:-1] + b"c"
        with self.assertRaisesRegex(ValueError, "data hash mismatch at 0x1000"):
            verify_lzvn_fork(lambda at, size: bad[at:at + size], len(bad), len(data), digests)
        with self.assertRaisesRegex(ValueError, "do not cover"):
            verify_lzvn_fork(lambda at, size: stored[at:at + size], len(stored), len(data), digests[:1])

    def test_padded_lzvn_slot_retains_exact_authenticated_contents(self):
        # A native LZFSE encoder's LZVN payload for 70 bytes, ending in EOS.
        lzvn = bytes.fromhex("680161f02ce1610600000000000000")
        data = b"a" * 70
        def check(slot):
            fork = struct.pack("<II", 8, 8 + len(slot)) + slot
            verify_lzvn_fork(lambda at, size: fork[at:at + size], len(fork), len(data),
                             [(0, 4096, sha256(data.ljust(4096, b"\0")).digest())])
        check(lzvn)
        check(lzvn + bytes(16))
        check(lzvn + bytes(200))
        with self.assertRaisesRegex(ValueError, "could not be decoded"):
            check(lzvn + b"\x01")

    def test_uncompressed_partial_page_uses_original_bytes_and_zero_padding(self):
        data = b"x" * 4096 + b"tail"
        hashes = [(0, 8192, sha256(data.ljust(8192, b"\0")).digest())]
        verify_plain_stream(lambda at, size: data[at:at + size], len(data), hashes)
        with self.assertRaisesRegex(ValueError, "data hash mismatch"):
            verify_plain_stream(lambda at, size: data[at:at + size - 1] + b"?", len(data), hashes)
        with self.assertRaisesRegex(ValueError, "do not cover"):
            verify_plain_stream(lambda at, size: data[at:at + size], len(data), hashes[1:])

    def test_full_audit_reads_raw_and_compressed_streams_not_only_cache_paths(self):
        block = 4096
        raw, logical = b"Plain non-cache executable", b"Another non-cache file"
        resource = struct.pack("<II", 8, 9 + len(logical)) + b"\x06" + logical
        inode = bytearray(140)
        struct.pack_into("<Q", inode, 8, 5)
        struct.pack_into("<HHBBHQ", inode, 92, 1, 40, 8, 0, 40, len(raw))

        def metadata(kind, oid, value, suffix=b""):
            return Entry(0, struct.pack("<Q", (kind << 60) | oid) + suffix, 0, value)

        def digest(oid, data):
            return metadata(13, oid, b"\1\0\x20" + sha256(data.ljust(block, b"\0")).digest(), struct.pack("<Q", 1 << 56))

        def xattr(owner, name, data):
            return metadata(4, owner, data, struct.pack("<H", len(name)) + name)

        nodes = [SimpleNamespace(leaf=True, entries=[metadata(3, 5, bytes(inode)), digest(5, raw),
            xattr(6, b"com.apple.decmpfs\0", struct.pack("<HH4sIQ", 2, 16, b"fpmc", 8, len(logical))),
            xattr(6, b"com.apple.ResourceFork\0", struct.pack("<HH6Q", 1, 48, 7, len(resource), block, 0, 0, 0)),
            digest(7, logical)])]
        extents = [Entry(0, struct.pack("<QQ", oid, 0), 0, struct.pack("<QQ", block, physical))
                   for oid, physical in ((5, 1), (7, 2))]
        frozen = bytearray(block)
        struct.pack_into("<Q", frozen, 0x408, 3)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "Disk.raw"
            path.write_bytes(bytes(block) + raw.ljust(block, b"\0") + resource.ljust(block, b"\0"))
            disk = RawDisk(path)
            try:
                container = SimpleNamespace(image=disk, offset=0, blocks=3,
                    tree=lambda *_, **__: [SimpleNamespace(leaf=True, entries=extents)])
                volume = SimpleNamespace(container=container, authenticated_snapshot=lambda _: (None, frozen, nodes))
                self.assertEqual(verify_sealed_file_data(volume, b"ignored"),
                                 {"VerifiedSignedStreams": 2, "VerifiedSignedDataDigests": 2})
                disk.write(block, b"X")
                with self.assertRaisesRegex(ValueError, "stream 0x5 failed:.*hash mismatch"):
                    verify_sealed_file_data(volume, b"ignored")
            finally:
                disk.close()


if __name__ == "__main__":
    unittest.main()
