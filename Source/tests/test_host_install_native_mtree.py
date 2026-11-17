import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


SOURCE = Path(__file__).resolve().parents[1]
ROOT = (
    "/set type=file xattrsdigest=none.0\n"
    ". type=dir time=1.0 btime=1.0 ctime=1.0 ptime=1.0 inode=2 siblingid=0\n"
)


@unittest.skipUnless(sys.platform == "darwin", "Uses the native installer manifest parser")
class NativeMtreeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        workspace = tempfile.TemporaryDirectory()
        cls.addClassCleanup(workspace.cleanup)
        cls.scratch = Path(workspace.name)
        cls.probe = cls.scratch / "mtree"
        subprocess.run(
            ("xcrun", "clang", "-fobjc-arc", "-Wall", "-Wextra", "-Werror",
             "-framework", "Foundation", SOURCE / "probes/host-install-mtree.m",
             SOURCE / "tools/HostInstall/VIMBigSurMtree.m", "-o", cls.probe),
            check=True,
        )

    def run_manifest(self, lines):
        manifest = self.scratch / "fixture.mtree"
        manifest.write_text(ROOT + lines + "..\n", encoding="utf-8")
        return subprocess.run((self.probe, manifest), capture_output=True)

    def test_reads_two_signed_paths_to_the_same_inode(self):
        result = self.run_manifest("one inode=21 siblingid=30\ntwo inode=21 siblingid=29\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(b"entries=3 links=2", result.stdout)

    def test_rejects_directory_siblings_and_out_of_range_ids(self):
        for record in ("dir type=dir inode=21 siblingid=30\n..\n",
                       f"file inode=21 siblingid={1 << 60}\n"):
            self.assertNotEqual(self.run_manifest(record).returncode, 0)

    @unittest.skipUnless(os.getenv("VIM_MONTEREY_SEAL_MTREE"), "Original Monterey seal not configured")
    def test_reads_the_full_original_monterey_manifest(self):
        result = subprocess.run((self.probe, os.environ["VIM_MONTEREY_SEAL_MTREE"]), capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(b"entries=572807 links=99404", result.stdout)


if __name__ == "__main__":
    unittest.main()
