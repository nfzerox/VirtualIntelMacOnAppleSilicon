from pathlib import Path
import plistlib
import runpy
import tempfile
import unittest


SOURCE = Path(__file__).resolve().parents[1]


class LaunchPolicyTests(unittest.TestCase):
    def test_only_launch_services_requirement_changes(self):
        apply = runpy.run_path(str(SOURCE / "scripts/build_launch_policy.py"))["apply_launch_minimum"]
        for format in (plistlib.FMT_BINARY, plistlib.FMT_XML):
            with self.subTest(format=format), tempfile.TemporaryDirectory() as directory:
                app = Path(directory) / "App.app"
                contents = app / "Contents"
                contents.mkdir(parents=True)
                binary = contents / "MacOS"
                binary.mkdir()
                (binary / "App").write_bytes(b"unchanged native executable")
                info = {"CFBundleExecutable": "App", "CFBundleVersion": "19",
                        "LSMinimumSystemVersion": "11.0",
                        "LSMinimumSystemVersionByArchitecture": {"x86_64": "11.0"}}
                path = contents / "Info.plist"
                path.write_bytes(plistlib.dumps(info, fmt=format))
                apply(app)
                result = plistlib.loads(path.read_bytes())
                self.assertEqual(result, {"CFBundleExecutable": "App", "CFBundleVersion": "19",
                                          "LSMinimumSystemVersion": "15.0"})
                self.assertEqual((binary / "App").read_bytes(), b"unchanged native executable")


if __name__ == "__main__":
    unittest.main()
