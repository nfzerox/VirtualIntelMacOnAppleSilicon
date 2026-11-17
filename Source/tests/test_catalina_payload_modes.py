from pathlib import Path
import runpy
import tarfile
import tempfile
import unittest


SOURCE = Path(__file__).resolve().parents[1]


class CatalinaPayloadModesTests(unittest.TestCase):
    def test_archive_modes_do_not_inherit_synthetic_exfat_permissions(self):
        pack = runpy.run_path(str(SOURCE / "scripts/build-catalina-gl.py"))["pack"]
        with tempfile.TemporaryDirectory(prefix="vim-payload-modes-") as temporary:
            root = Path(temporary)
            payload = root / "payload"
            payload.mkdir(mode=0o700)
            binary = payload / "Adapter"
            binary.write_bytes(b"\xcf\xfa\xed\xfe" + b"fixture")
            binary.chmod(0o700)
            resource = payload / "Info.plist"
            resource.write_bytes(b"fixture")
            resource.chmod(0o700)
            (payload / "Current").symlink_to("Adapter")
            with tarfile.open(pack(root, payload)) as archive:
                modes = {member.name: member.mode for member in archive}
            self.assertEqual(modes, {
                "payload": 0o755, "payload/Adapter": 0o755,
                "payload/Info.plist": 0o644, "payload/Current": 0o755,
            })


if __name__ == "__main__":
    unittest.main()
