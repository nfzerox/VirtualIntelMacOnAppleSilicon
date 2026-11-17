from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


SOURCE = Path(__file__).resolve().parents[1]


@unittest.skipUnless(sys.platform == "darwin", "Compares native SystemConfiguration output")
class ComputerNameTests(unittest.TestCase):
    def test_prepared_names_match_native_preferences(self):
        with tempfile.TemporaryDirectory(prefix="vim-computer-name-") as temporary:
            root = Path(temporary)
            probe = root / "name"
            subprocess.run(("xcrun", "clang", "-fobjc-arc", "-O2", "-Wall", "-Wextra", "-Werror",
                            "-Wno-deprecated-declarations", "-mmacosx-version-min=11.0",
                            SOURCE / "probes/host-install-computer-name.m", "-framework", "Foundation",
                            "-framework", "SystemConfiguration", "-o", probe), check=True)
            result = subprocess.run((probe, root / "preferences.plist"), capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("PASS: offline guest names", result.stdout)


if __name__ == "__main__":
    unittest.main()
