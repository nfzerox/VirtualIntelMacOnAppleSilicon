from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


SOURCE = Path(__file__).resolve().parents[1]


@unittest.skipUnless(sys.platform == "darwin", "Uses the Objective-C runtime")
class TextureViewSwizzleTests(unittest.TestCase):
    def test_shared_transport_preserves_view_state(self):
        with tempfile.TemporaryDirectory(prefix="vim-swizzle-test-") as temporary:
            probe = Path(temporary) / "swizzle"
            subprocess.run((
                "xcrun", "clang", "-mmacosx-version-min=11.0", "-fobjc-arc",
                "-O2", "-Wall", "-Wextra", "-Werror", "-framework", "Foundation",
                SOURCE / "probes/pvg-texture-view-swizzle.m", "-o", probe,
            ), check=True)
            result = subprocess.run((probe,), capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("PASS: identity, nonidentity, nested views", result.stdout)


if __name__ == "__main__":
    unittest.main()
