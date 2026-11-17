from pathlib import Path
import plistlib
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile


SOURCE = Path(__file__).resolve().parents[1]
BASE = ("Applications", "Library", "Users", "private")


def directory(path, target):
    name = path.encode()
    header = (b"TYP1D" + b"PATP" + struct.pack("<H", len(name)) + name +
              b"UID1\0GID1\0MOD2" + struct.pack("<H", 0o755) +
              b"FLG4" + struct.pack("<I", 0x800000))
    attrs = b""
    if target is not None:
        content = b"com.apple.fs.firmlink\0" + target.encode() + b"\0"
        attrs = struct.pack("<I", len(content) + 4) + content
        header += b"XATA" + struct.pack("<H", len(attrs))
    return b"YAA1" + struct.pack("<H", len(header) + 6) + header + attrs


@unittest.skipUnless(sys.platform == "darwin", "Uses native Apple Archive")
class NativeFirmlinkTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        scratch = tempfile.TemporaryDirectory()
        cls.addClassCleanup(scratch.cleanup)
        cls.probe = Path(scratch.name) / "firmlinks"
        subprocess.run(("xcrun", "clang", "-fobjc-arc", "-Wall", "-Wextra", "-Werror",
                        "-framework", "Foundation", "-lAppleArchive", "-larchive", "-lcompression", "-llzma.5",
                        "-I", SOURCE / "vendor/xz-api", SOURCE / "probes/host-install-firmlinks.m",
                        SOURCE / "tools/HostInstall/VIMBigSurFirmlinks.m",
                        SOURCE / "tools/HostInstall/VIMBigSurArchive.m",
                        SOURCE / "tools/HostInstall/VIMBigSurBootAssets.m",
                        SOURCE / "tools/HostInstall/VIMCatalinaDataLayout.m",
                        SOURCE / "tools/HostInstall/VIMAppleDouble.m",
                        SOURCE / "tools/HostInstall/VIMGuestPath.m",
                        SOURCE / "tools/host-install-pbzx.c", "-o", cls.probe), check=True)

    def run_plan(self, root, rows, text=None, reserve=False):
        archive = root / "Original.zip"
        with zipfile.ZipFile(archive, "w") as file:
            file.writestr("AssetData/payloadv2/firmlinks_payload", b"".join(directory(*row) for row in rows))
        system = root / "System"
        manifest = system / "usr/share/firmlinks"
        manifest.parent.mkdir(parents=True)
        manifest.write_text(text or "".join(f"/{name}\t{name}\n" for name in BASE))
        args = ("--reserve", archive, system) if reserve else (archive, manifest)
        return system, subprocess.run((self.probe, *args), capture_output=True)

    def test_generic_optional_entries_do_not_create_unsigned_system_stubs(self):
        with tempfile.TemporaryDirectory() as temp:
            text = "".join(f"/{name}\t{name}\n" for name in (*BASE, "AppleInternal", "pkg"))
            system, result = self.run_plan(Path(temp), [(name, name) for name in BASE], text, reserve=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            plan = plistlib.loads(result.stdout)
            self.assertEqual(plan["FirmlinkCount"], len(BASE))
            self.assertFalse((system / "pkg").exists())
            self.assertFalse((system / "AppleInternal").exists())
            self.assertEqual((system / "usr/share/firmlinks").read_text(), text)
            for name in BASE:
                value = subprocess.check_output(("/usr/bin/xattr", "-px", "com.apple.fs.firmlinj", system / name))
                self.assertEqual(bytes.fromhex(value.decode()), name.encode() + b"\0")

    def test_actual_internal_entry_is_not_discarded_by_release_specific_rules(self):
        with tempfile.TemporaryDirectory() as temp:
            rows = [(name, name) for name in (*BASE, "AppleInternal")]
            text = "".join(f"/{name}\t{target}\n" for name, target in rows)
            _, result = self.run_plan(Path(temp), rows, text)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("AppleInternal", plistlib.loads(result.stdout)["Targets"])

    def test_missing_mapping_or_conflicting_target_is_still_rejected(self):
        for extra in (("unmapped", "unmapped"), ("Applications", "OtherApps")):
            with self.subTest(extra=extra), tempfile.TemporaryDirectory() as temp:
                rows = [(name, name) for name in BASE if name != extra[0]] + [extra]
                _, result = self.run_plan(Path(temp), rows)
                self.assertEqual(result.returncode, 1)
                self.assertIn(b"targets disagree", result.stderr)

    def test_firmlink_target_attribute_is_required_and_cannot_escape_data(self):
        for target in (None, "../outside", "/absolute"):
            with self.subTest(target=target), tempfile.TemporaryDirectory() as temp:
                rows = [(name, name) for name in BASE if name != "Applications"] + [("Applications", target)]
                _, result = self.run_plan(Path(temp), rows)
                self.assertEqual(result.returncode, 1)
                self.assertIn(b"payload is invalid", result.stderr)


if __name__ == "__main__":
    unittest.main()
