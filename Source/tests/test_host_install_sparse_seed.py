"""Exercise the native thin GPT preparation without installing an OS."""
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib


SOURCE = Path(__file__).resolve().parents[1]


@unittest.skipUnless(sys.platform == "darwin", "Uses Apple's empty disk formatter and APFS holes")
class SparseInstallSeedTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.workspace = tempfile.TemporaryDirectory(dir=os.getenv("VIM_SPARSE_TEST_ROOT"))
        cls.addClassCleanup(cls.workspace.cleanup)
        cls.root = Path(cls.workspace.name)
        cls.probe = cls.root / "seed-probe"
        subprocess.run(("xcrun", "clang", "-fobjc-arc", "-O2", "-Wall", "-Wextra", "-Werror",
                        "-framework", "Foundation", "-lz", SOURCE / "probes/host-install-sparse-seed.m",
                        SOURCE / "tools/HostInstall/VIMHFSCatalog.m", "-o", cls.probe), check=True)
        cls.seed = cls.root / "Seed.sparsebundle"
        subprocess.run(("hdiutil", "create", "-size", "8g", "-type", "SPARSEBUNDLE", "-layout", "GPTSPUD",
                        "-fs", "Journaled HFS+", "-volname", "Empty Install Seed", cls.seed),
                       check=True, capture_output=True)

    def read_gpt(self, image, sector):
        with image.open("rb") as stream:
            stream.seek(sector * 512)
            header = bytearray(stream.read(512))
            self.assertEqual(header[:8], b"EFI PART")
            length, expected = struct.unpack_from("<II", header, 12)
            struct.pack_into("<I", header, 16, 0)
            self.assertEqual(zlib.crc32(header[:length]), expected)
            table, count, stride, expected = struct.unpack_from("<QIII", header, 72)
            stream.seek(table * 512)
            entries = stream.read(count * stride)
            self.assertEqual(zlib.crc32(entries), expected)
        return header, entries

    def test_virtual_capacity_does_not_allocate_it_and_gpt_copies_agree(self):
        allocated = []
        for gib in (128, 512):
            with self.subTest(gib=gib):
                image = self.root / f"Raw{gib}.dmg"
                result = subprocess.run((self.probe, "--from-bundle", self.seed, image, str(gib)), capture_output=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                info = image.stat()
                self.assertEqual(info.st_size, gib << 30)
                self.assertLess(info.st_blocks * 512, 32 << 20)
                allocated.append(info.st_blocks * 512)
                last = info.st_size // 512 - 1
                primary, entries = self.read_gpt(image, 1)
                backup, mirrored = self.read_gpt(image, last)
                self.assertEqual(entries, mirrored)
                self.assertEqual(struct.unpack_from("<Q", primary, 32)[0], last)
                self.assertEqual(struct.unpack_from("<QQ", backup, 24), (last, 1))
                self.assertEqual(primary[56:72], backup[56:72])
                start, end = struct.unpack_from("<QQ", entries, 128 + 32)
                self.assertLess(start, end)
                self.assertEqual((end + 1) % 8, 0)
                self.assertGreater(end * 512, (gib - 1) << 30)
                self.assertEqual(entries[:16], bytes.fromhex("28732ac11ff8d211ba4b00a0c93ec93b"))
                with image.open("rb") as stream:
                    stream.seek(450)
                    self.assertEqual(stream.read(1), b"\xee")
                    stream.seek(458)
                    self.assertEqual(struct.unpack("<I", stream.read(4))[0], min(last, 0xffffffff))
                # Never interpret shrinking as permission to resize an installed system.
                refused = subprocess.run((self.probe, image, str(gib - 1)), capture_output=True)
                self.assertNotEqual(refused.returncode, 0)
                self.assertEqual(image.stat().st_size, info.st_size)
        self.assertLess(abs(allocated[1] - allocated[0]), 1 << 20)


if __name__ == "__main__":
    unittest.main()
