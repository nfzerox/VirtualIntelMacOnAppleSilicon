from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


SOURCE = Path(__file__).resolve().parents[1]


@unittest.skipUnless(sys.platform == "darwin", "Uses the native detached APFS reader")
class NativeCatalogTests(unittest.TestCase):
    def test_compression_attributes_support_embedded_and_stream_storage(self):
        with tempfile.TemporaryDirectory(prefix="vim-catalog-test-") as temporary:
            probe = Path(temporary) / "catalog"
            subprocess.run((
                "xcrun", "clang", "-mmacosx-version-min=11.0", "-fobjc-arc", "-fblocks",
                "-O2", "-Wall", "-Wextra", "-Werror", "-framework", "Foundation",
                "-Wl,-dead_strip", "-lz", SOURCE / "probes/apfs-catalog-read.m",
                SOURCE / "tools/HostInstall/VIMDiskImageIO.m",
                SOURCE / "tools/HostInstall/VIMHFSCatalog.m", "-o", probe,
            ), check=True)
            result = subprocess.run((probe, "--self-test"), capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("PASS: embedded/stream xattrs", result.stdout)


if __name__ == "__main__":
    unittest.main()
