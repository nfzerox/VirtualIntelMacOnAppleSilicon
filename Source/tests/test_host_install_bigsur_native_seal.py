import ctypes
import errno
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


TOOLS = Path(__file__).resolve().parents[1] / "tools"
PROGRESS = ctypes.CFUNCTYPE(ctypes.c_bool, ctypes.c_uint64, ctypes.c_void_p)


@unittest.skipUnless(sys.platform == "darwin", "Uses Apple's native archive reader")
class NativeIntelSealTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        folder = tempfile.TemporaryDirectory()
        cls.addClassCleanup(folder.cleanup)
        output = Path(folder.name) / "seal.dylib"
        subprocess.run(("xcrun", "clang", "-dynamiclib", "-Wall", "-Wextra", "-Werror",
                        TOOLS / "host-install-bigsur-seal.c", "-lAppleArchive", "-o", output), check=True)
        cls.library = ctypes.CDLL(str(output), use_errno=True)
        cls.library.VIMExtractIntelSealArchive.argtypes = (ctypes.c_int, ctypes.c_int, PROGRESS, ctypes.c_void_p)
        cls.library.VIMExtractIntelSealArchive.restype = ctypes.c_int

    def setUp(self):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.root = Path(folder.name)
        self.source, self.result = self.root / "source", self.root / "result"
        self.source.mkdir()
        self.result.mkdir()
        self.digest = b"a digest record\0" * 40000
        self.mtree = b". type=dir\n..\n"
        (self.source / "digest.db").write_bytes(self.digest)
        (self.source / "mtree.txt").write_bytes(self.mtree)

    def archive(self):
        path = self.root / "archive"
        subprocess.run(("/usr/bin/aa", "archive", "-a", "raw", "-d", self.source, "-o", path), check=True)
        return path

    def extract(self, source, progress=None):
        destination = os.open(self.result, os.O_RDONLY | os.O_DIRECTORY)
        try:
            with source.open("rb") as data:
                status = self.library.VIMExtractIntelSealArchive(data.fileno(), destination,
                                                                 PROGRESS(progress) if progress else PROGRESS(), None)
                return ctypes.get_errno() if status else 0
        finally:
            os.close(destination)

    def test_extracts_only_two_expected_members_with_private_mode(self):
        self.assertEqual(self.extract(self.archive()), 0)
        self.assertEqual((self.result / "digest.db").read_bytes(), self.digest)
        self.assertEqual((self.result / "mtree.txt").read_bytes(), self.mtree)
        self.assertEqual((self.result / "digest.db").stat().st_mode & 0o777, 0o600)

    def test_existing_file_and_link_are_never_overwritten(self):
        target = self.result / "mtree.txt"
        existing = self.root / "existing"
        existing.write_text("keep")
        target.symlink_to(existing)
        self.assertEqual(self.extract(self.archive()), errno.EEXIST)
        self.assertEqual(existing.read_text(), "keep")
        self.assertTrue(target.is_symlink())
        self.assertFalse((self.result / "digest.db").exists())

    def test_cancellation_drops_only_partial_seal_inputs(self):
        existing = self.result / "unrelated"
        existing.write_text("keep")
        self.assertEqual(self.extract(self.archive(), lambda written, _: written < 256 * 1024), errno.ECANCELED)
        self.assertEqual(list(self.result.iterdir()), [existing])
        self.assertEqual(existing.read_text(), "keep")

    def test_unknown_or_missing_member_cannot_leave_a_usable_pair(self):
        (self.source / "mtree.txt").rename(self.source / "other.txt")
        self.assertEqual(self.extract(self.archive()), errno.EBADMSG)
        self.assertEqual(list(self.result.iterdir()), [])


if __name__ == "__main__":
    unittest.main()
