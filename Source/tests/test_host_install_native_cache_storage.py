from hashlib import sha256
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest


SOURCE = Path(__file__).resolve().parents[1]


@unittest.skipUnless(sys.platform == "darwin", "Uses Apple's native cache decoder")
class NativeCacheStorageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        workspace = tempfile.TemporaryDirectory()
        cls.addClassCleanup(workspace.cleanup)
        cls.scratch = Path(workspace.name)
        cls.probe = cls.scratch / "cache-storage"
        subprocess.run(
            ("xcrun", "clang", "-fobjc-arc", "-O2", "-Wall", "-Wextra", "-Werror",
             "-framework", "Foundation", "-lcompression",
             SOURCE / "probes/host-install-cache-storage.m",
             SOURCE / "tools/HostInstall/VIMBigSurMtree.m", "-o", cls.probe),
            check=True,
        )

    def check_fork(self, payload, data, *, bad_digest=False, extra=False):
        fork, digests = self.scratch / "fork", self.scratch / "digests"
        fork.write_bytes(struct.pack("<II", 8, 8 + len(payload)) + payload)
        digest = sha256(data.ljust(4096, b"\0")).digest()
        if bad_digest:
            digest = bytes([digest[0] ^ 1]) + digest[1:]
        encoded = struct.pack("<QI", 0, 4096) + digest
        if extra:
            encoded += struct.pack("<QI", 4096, 4096) + digest
        digests.write_bytes(encoded)
        return subprocess.run((self.probe, fork, str(len(data)), digests), capture_output=True)

    def test_final_partial_raw_page_checks_its_signed_zero_padding(self):
        data = b"x" * 70
        result = self.check_fork(b"\x06" + data, data)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_lzvn_slot_can_be_wider_than_its_logical_tail(self):
        data = b"a" * 70
        slot = bytes.fromhex("680161f02ce1610600000000000000") + bytes(200)
        result = self.check_fork(slot, data)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotEqual(self.check_fork(slot, data, bad_digest=True).returncode, 0)
        self.assertNotEqual(self.check_fork(slot, data, extra=True).returncode, 0)
        self.assertNotEqual(self.check_fork(slot[:-1] + b"\x01", data).returncode, 0)

    def test_uncompressed_system_data_detects_a_zeroed_disk_range(self):
        data = b"wallpaper" * 2000 + b"tail"
        fork, digests = self.scratch / "plain", self.scratch / "plain-digests"
        fork.write_bytes(data)
        rows = b""
        for offset in range(0, len(data), 16384):
            part=data[offset:offset+16384]
            length=(len(part)+4095)&~4095
            rows += struct.pack("<QI",offset,length)+sha256(part.ljust(length,b"\0")).digest()
        digests.write_bytes(rows)
        command=(self.probe,fork,str(len(data)),digests,"--plain")
        result=subprocess.run(command,capture_output=True)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        fork.write_bytes(data[:4096]+bytes(4096)+data[8192:])
        self.assertNotEqual(subprocess.run(command,capture_output=True).returncode,0)

    @unittest.skipUnless(os.getenv("VIM_MONTEREY_CACHE_FIXTURE"), "Original cache fixture not configured")
    def test_original_monterey_cache_against_all_signed_digests(self):
        fixture = Path(os.environ["VIM_MONTEREY_CACHE_FIXTURE"])
        result = subprocess.run(
            (self.probe, fixture / "x86_64.fork", "799457280", fixture / "x86_64.digests"),
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
