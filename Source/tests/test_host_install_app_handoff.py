import os
import fcntl
from pathlib import Path
import plistlib
import subprocess
import tempfile
import unittest
from uuid import uuid4


APP, QEMU_IMG = os.environ.get("VIM_TEST_APP_BINARY"), os.environ.get("VIM_TEST_QEMU_IMG")
APP_ARCH = os.environ.get("VIM_TEST_APP_ARCH")


@unittest.skipUnless(APP and QEMU_IMG, "Set VIM_TEST_APP_BINARY and VIM_TEST_QEMU_IMG for the native app test")
class AppHostInstallHandoffTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(dir=os.environ.get("VIM_PUBLISH_TEST_ROOT"))
        self.addCleanup(temp.cleanup)
        self.library = Path(temp.name)
        self.machines = self.library / "Machines"
        self.identifier = str(uuid4()).upper()
        self.job = self.machines / (".host-install-" + self.identifier)
        self.stage = self.job / "Prepared.vintelmac"
        for name in ("State", "Disks", "Firmware"):
            (self.stage / name).mkdir(parents=True)
        volume, machine, snapshot = (str(uuid4()).upper() for _ in range(3))
        self.settings = {"FormatVersion": 1, "Identifier": machine, "Guest": "bigsur", "InstallState": "installed",
                         "Disk": "Disks/System.qcow2", "NVRAM": "Firmware/NVRAM.fd", "FirmwareSource": "application",
                         "BootMediaKind": "efi-disk", "CPUCount": 4, "MemoryMiB": 4096,
                         "SSHEnabled": False, "FileSharingEnabled": False}
        self.receipt = {"FormatVersion": 1, "Phase": "awaiting-first-boot", "Guest": "bigsur", "MachineIdentifier": machine,
                        "VolumeUUID": volume, "ProductBuildVersion": "20G1443", "BootSnapshotUUID": snapshot,
                        "BigSurGraphicsStaged": True}
        self.write("Machine.plist", self.settings)
        self.write("State/HostAssembly.plist", self.receipt)
        self.write("State/InstallationTarget.plist", {"FormatVersion": 1, "VolumeUUID": volume, "Filesystem": "APFS"})
        (self.stage / "Firmware/NVRAM.fd").touch()
        self.disk = self.stage / self.settings["Disk"]
        self.qemu("create", "-f", "qcow2", self.disk, "1M")

    def write(self, name, data):
        (self.stage / name).write_bytes(plistlib.dumps(data))

    def qemu(self, *arguments):
        subprocess.run((QEMU_IMG, *arguments), check=True, capture_output=True)

    def publish(self, recover=False):
        executable = ("/usr/bin/arch", "-" + APP_ARCH, APP) if APP_ARCH else (APP,)
        return subprocess.run((*executable, "--recover-host-install" if recover else "--publish-host-install", self.job, "--library", self.library,
                               "--guest", "bigsur", "--name", "New Big Sur", "--cores", "2", "--memory-mib", "3072"),
                              capture_output=True, text=True, timeout=20)

    def test_publishes_once_and_recovers_by_job_after_rename(self):
        collision = self.machines / "New Big Sur.vintelmac"
        collision.mkdir()
        (collision / "keep").touch()
        result = self.publish()
        self.assertEqual(result.returncode, 0, result.stderr)
        published = self.machines / "New Big Sur 2.vintelmac"
        self.assertEqual(Path(result.stdout.strip()).resolve(), published.resolve())
        self.assertFalse(self.stage.exists())
        self.assertTrue((collision / "keep").exists())
        config = plistlib.loads((published / "Machine.plist").read_bytes())
        self.assertEqual((config["CPUCount"], config["MemoryMiB"], config["SSHEnabled"]), (2, 3072, False))
        renamed = self.machines / "Renamed Big Sur.vintelmac"
        published.rename(renamed)
        retry = self.publish()
        self.assertEqual(retry.returncode, 0, retry.stderr)
        self.assertEqual(Path(retry.stdout.strip()).resolve(), renamed.resolve())

    def test_does_not_publish_a_disk_with_a_backing_file(self):
        base = self.job / "Other.qcow2"
        self.disk.rename(base)
        self.qemu("create", "-f", "qcow2", "-F", "qcow2", "-b", base, self.disk)
        self.assertEqual(self.publish().returncode, 1)
        self.assertTrue(self.disk.is_file())
        self.assertFalse(list(self.machines.glob("*.vintelmac")))

    def test_does_not_publish_an_unstaged_graphics_or_wrong_volume(self):
        self.write("State/HostAssembly.plist", {**self.receipt, "BigSurGraphicsStaged": False})
        self.assertEqual(self.publish().returncode, 1)
        self.write("State/HostAssembly.plist", {**self.receipt, "VolumeUUID": str(uuid4()).upper()})
        self.assertEqual(self.publish().returncode, 1)
        self.assertTrue(self.stage.is_dir())
        self.assertFalse(list(self.machines.glob("*.vintelmac")))

    def test_recovery_adopts_completed_attempt_without_installer_or_credential(self):
        attempt = self.job / str(uuid4())
        attempt.mkdir()
        (attempt / "Worker.log").touch()
        self.stage.rename(attempt / "Prepared.vintelmac")
        result = self.publish(recover=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        published = self.machines / "New Big Sur.vintelmac"
        self.assertEqual(Path(result.stdout.strip()).resolve(), published.resolve())
        self.assertFalse((attempt / "Prepared.vintelmac").exists())
        self.assertEqual(list(self.job.glob("*/Request.plist")), [])
        renamed = self.machines / "Recovered and Renamed.vintelmac"
        published.rename(renamed)
        again = self.publish(recover=True)
        self.assertEqual(again.returncode, 0, again.stderr)
        self.assertEqual(Path(again.stdout.strip()).resolve(), renamed.resolve())

    def test_recovery_distinguishes_absence_from_invalid_completed_disk(self):
        # A committed but invalid result must not fall through to another install.
        self.write("State/HostAssembly.plist", {**self.receipt, "BigSurGraphicsStaged": False})
        invalid = self.publish(recover=True)
        self.assertEqual(invalid.returncode, 1)
        self.assertTrue(invalid.stderr)
        self.assertTrue(self.stage.is_dir())
        # An incomplete attempt stays untouched; absence explicitly requests
        # ordinary preflight, never a worker launched by this recovery API.
        partial = self.job / str(uuid4())
        partial.mkdir()
        (partial / "Worker.log").touch()
        self.stage.rename(partial / "Incomplete.vintelmac")
        absent = self.publish(recover=True)
        self.assertEqual(absent.returncode, 2, absent.stderr)
        self.assertFalse(absent.stderr)
        self.assertTrue((partial / "Incomplete.vintelmac").is_dir())
        self.assertEqual(list(self.job.glob("*/Request.plist")), [])

    def test_recovery_respects_both_app_ownership_and_orphan_worker_lease(self):
        with (self.job / ".Worker.lock").open("wb") as owner:
            fcntl.flock(owner, fcntl.LOCK_EX | fcntl.LOCK_NB)
            busy = self.publish(recover=True)
            self.assertEqual(busy.returncode, 1, busy.stderr)
            self.assertIn("still finishing", busy.stderr)
        attempt = self.job / str(uuid4())
        attempt.mkdir()
        self.stage.rename(attempt / "Prepared.vintelmac")
        with (attempt / "Worker.log").open("wb") as log:
            fcntl.flock(log, fcntl.LOCK_EX | fcntl.LOCK_NB)
            busy = self.publish(recover=True)
            self.assertEqual(busy.returncode, 1, busy.stderr)
            self.assertIn("still finishing", busy.stderr)
            self.assertTrue((attempt / "Prepared.vintelmac").is_dir())
        self.assertEqual(self.publish(recover=True).returncode, 0)


if __name__ == "__main__":
    unittest.main()
