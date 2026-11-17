from concurrent.futures import ThreadPoolExecutor
import ctypes
import errno
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from threading import Barrier
import unittest


SOURCE = Path(__file__).resolve().parents[1] / "VirtualIntelMac/VIMPackagePublication.c"


@unittest.skipUnless(sys.platform == "darwin", "Uses Darwin's exclusive rename")
class PackagePublicationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        folder = tempfile.TemporaryDirectory()
        cls.addClassCleanup(folder.cleanup)
        library = Path(folder.name) / "publication.dylib"
        subprocess.run(("xcrun", "clang", "-dynamiclib", "-Wall", "-Wextra", "-Werror", SOURCE,
                        "-o", library), check=True)
        cls.library = ctypes.CDLL(str(library), use_errno=True)
        cls.library.VIMPublishPreparedPackage.argtypes = (ctypes.c_char_p, ctypes.c_char_p)
        cls.library.VIMPublishPreparedPackage.restype = ctypes.c_int

    def publish(self, prepared, destination):
        return ctypes.get_errno() if self.library.VIMPublishPreparedPackage(os.fsencode(prepared),
                                                                          os.fsencode(destination)) else 0

    def setUp(self):
        folder = tempfile.TemporaryDirectory(dir=os.environ.get("VIM_PUBLISH_TEST_ROOT"))
        self.addCleanup(folder.cleanup)
        self.root = Path(folder.name)
        self.destination = self.root / "Mac.vintelmac"

    def prepare(self, value):
        stage = self.root / (".preparing-" + value)
        stage.mkdir()
        (stage / "Machine.plist").write_text(value)
        return stage

    def test_publishes_prepared_bundle_in_one_move(self):
        stage = self.prepare("new")
        self.assertEqual(self.publish(stage, self.destination), 0)
        self.assertFalse(stage.exists())
        self.assertEqual((self.destination / "Machine.plist").read_text(), "new")

    def test_existing_directory_or_link_is_never_replaced(self):
        stage = self.prepare("new")
        self.destination.mkdir()
        self.assertEqual(self.publish(stage, self.destination), errno.EEXIST)
        self.assertEqual(list(self.destination.iterdir()), [])
        manifest = self.destination / "Machine.plist"
        manifest.write_text("existing")
        self.assertEqual(self.publish(stage, self.destination), errno.EEXIST)
        self.assertEqual(manifest.read_text(), "existing")
        # Also reject a dangling symlink on filesystems that provide them.
        link = self.root / "Dangling.vintelmac"
        try:
            link.symlink_to(self.root / "nonexistent")
        except OSError as failure:
            if failure.errno not in (errno.ENOTSUP, errno.EOPNOTSUPP):
                raise
        else:
            self.assertEqual(self.publish(stage, link), errno.EEXIST)
            self.assertTrue(link.is_symlink())
        self.assertEqual((stage / "Machine.plist").read_text(), "new")

    def test_concurrent_attempts_never_clobber_another_machine(self):
        stages = [self.prepare(str(i)) for i in range(8)]
        start = Barrier(len(stages))

        def publish(stage):
            start.wait(timeout=10)
            return self.publish(stage, self.destination)

        with ThreadPoolExecutor(max_workers=len(stages)) as workers:
            outcomes = list(workers.map(publish, stages))
        self.assertEqual(sorted(outcomes), [0] + [errno.EEXIST] * (len(stages) - 1))
        self.assertEqual((self.destination / "Machine.plist").read_text(), str(outcomes.index(0)))
        self.assertEqual([stage.exists() for stage in stages], [outcome != 0 for outcome in outcomes])


if __name__ == "__main__":
    unittest.main()
