from pathlib import Path
import plistlib
import subprocess
import sys
import tempfile
import unittest


WORKER = Path(__file__).resolve().parents[1] / "tools/host-install-development-worker"


class TigerDevelopmentWorkerTests(unittest.TestCase):
    def setUp(self):
        scratch = tempfile.TemporaryDirectory()
        self.addCleanup(scratch.cleanup)
        self.work = Path(scratch.name)
        self.source = self.work / "Original.dmg"
        self.source.write_bytes(b"not mounted by these tests")
        app = self.work / "Virtual Intel Mac.app"
        runtime = app / "Contents/Resources/Runtime"
        support = runtime / "OpenGLGuests/tiger/Support.plist"
        support.parent.mkdir(parents=True)
        support.touch()
        qemu = runtime / "bin/qemu-img"
        qemu.parent.mkdir()
        qemu.touch(mode=0o700)
        self.request = {"FormatVersion": 1, "Guest": "tiger", "Media": {"diskimage": str(self.source)},
                        "DiskGiB": 40, "Username": "test", "AutomaticLogin": True, "RemoteLogin": False,
                        "FileSharing": False, "Application": str(app)}

    def run_worker(self):
        request = self.work / "Request.plist"
        request.write_bytes(plistlib.dumps(self.request))
        result = subprocess.run((sys.executable, WORKER, "--request", request), cwd=self.work,
                                input=b"private-fixture", capture_output=True, timeout=10)
        self.assertNotIn(b"private-fixture", result.stderr)
        self.assertEqual(self.source.read_bytes(), b"not mounted by these tests")
        self.assertFalse((self.work / "Prepared.vintelmac").exists())
        self.assertFalse(list(self.work.glob(".tiger-host-*")))
        return result

    def test_cancels_before_creating_a_disk(self):
        (self.work / "Cancel").touch()
        self.assertEqual(self.run_worker().returncode, 130)

    def test_smb_request_is_admitted_and_still_cancellable(self):
        self.request["FileSharing"] = True
        (self.work / "Cancel").touch()
        self.assertEqual(self.run_worker().returncode, 130)

    def test_refuses_a_prepared_guest_boot_image_role(self):
        self.request["Media"] = {"boot-iso": str(self.source)}
        result = self.run_worker()
        self.assertEqual(result.returncode, 1)
        self.assertIn(b"original Tiger", result.stderr)


if __name__ == "__main__":
    unittest.main()
