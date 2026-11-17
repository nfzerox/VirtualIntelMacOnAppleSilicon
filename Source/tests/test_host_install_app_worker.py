import os
import fcntl
from pathlib import Path
import plistlib
import signal
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from uuid import uuid4


APP = os.environ.get("VIM_TEST_APP_BINARY")
PASSWORD = b"disposable-test-password\nwith-a-line-break"
WORKER = '''from pathlib import Path
import errno
import plistlib
import sys
import time

assert len(sys.argv) == 3 and sys.argv[1] == "--request"
with Path(sys.argv[2]).open("rb") as stream:
    request = plistlib.load(stream)
assert set(request) == {"FormatVersion", "Guest", "TestMode"} | ({"CreateAccount"} if request["TestMode"] == "manual" else set())
assert sys.stdin.buffer.read() == (b"" if request["TestMode"] == "manual" else %r)
if request["TestMode"] == "cancel":
    for _ in range(160):
        if Path("Cancel").exists():
            time.sleep(.1)
            Path("CleanupFinished").touch()
            raise SystemExit(130)
        time.sleep(.025)
    raise SystemExit(3)
if request["TestMode"] in ("apfs", "no-space"):
    with Path("Progress.plist").open("wb") as stream:
        plistlib.dump({"Stage": "apfs", "Start": .8, "End": .9}, stream)
    raise SystemExit(errno.ENOSPC if request["TestMode"] == "no-space" else 9)
if request["TestMode"] == "fail":
    raise SystemExit(9)
if request["TestMode"] in ("timing-pass", "timing-fail"):
    for name, start in (("first", 0), ("second", .5)):
        temporary = Path("Progress.tmp")
        temporary.write_bytes(plistlib.dumps({"Stage": name, "Start": start,
                                             "End": start + .5, "EstimatedSeconds": 10}))
        temporary.replace("Progress.plist")
        time.sleep(1.2)
    if request["TestMode"] == "timing-fail":
        raise SystemExit(9)
if request["TestMode"] == "orphan":
    import os
    Path("Started").write_text(str(os.getpid()))
    while not Path("Finish").exists():
        time.sleep(.025)
Path("Prepared.vintelmac").mkdir()
''' % PASSWORD


@unittest.skipUnless(APP, "Set VIM_TEST_APP_BINARY to run the native app worker protocol")
class AppHostInstallWorkerTests(unittest.TestCase):
    def run_worker(self, mode, guest="worker-protocol-fixture", binary=None):
        temp = tempfile.TemporaryDirectory(dir=os.environ.get("VIM_PUBLISH_TEST_ROOT"))
        self.addCleanup(temp.cleanup)
        root = Path(temp.name)
        source, worker, request = (root / name for name in ("Source.dmg", "worker", "Input.plist"))
        source.write_bytes(b"original installer fixture")
        worker.write_text(f"#!{sys.executable}\n" + WORKER)
        worker.chmod(0o700)
        # The protocol fixture does not install an OS. Do not teach the real
        # Create window that Tiger's installation takes a few milliseconds.
        description = {"FormatVersion": 1, "Guest": guest, "TestMode": mode}
        if mode == "manual":
            description["CreateAccount"] = False
        request.write_bytes(plistlib.dumps(description))
        result = subprocess.run((binary or APP, "--test-host-install-worker", worker,
                                 "cancel" if mode == "cancel" else "complete", request),
                                cwd=root, input=b"" if mode == "manual" else PASSWORD, capture_output=True, timeout=12)
        self.assertEqual(source.read_bytes(), b"original installer fixture")
        for data in (result.stdout, result.stderr, (root / "Worker.log").read_bytes(),
                     (root / "Request.plist").read_bytes()):
            self.assertNotIn(b"disposable-test-password", data)
        return root, result

    def test_passes_credential_privately_and_retains_prepared_result(self):
        root, result = self.run_worker("complete")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((root / "Prepared.vintelmac").is_dir())

    def test_manual_account_setup_needs_no_secret(self):
        root, result = self.run_worker("manual")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((root / "Prepared.vintelmac").is_dir())

    def test_cancel_waits_for_worker_cleanup(self):
        root, result = self.run_worker("cancel")
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertTrue((root / "Cancel").is_file())
        self.assertTrue((root / "CleanupFinished").is_file())
        self.assertFalse((root / "Prepared.vintelmac").exists())

    def test_worker_failure_is_not_treated_as_ready(self):
        root, result = self.run_worker("fail")
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertFalse((root / "Prepared.vintelmac").exists())

    def test_quick_worker_failure_reports_the_final_stage(self):
        _, result = self.run_worker("apfs")
        self.assertEqual(result.returncode, 1)
        self.assertIn(b"preparing the APFS volumes", result.stderr)

    def test_worker_space_failure_takes_priority_over_apfs_stage(self):
        root, result = self.run_worker("no-space")
        self.assertEqual(result.returncode, 1)
        self.assertIn(b"free space", result.stderr)
        self.assertNotIn(b"APFS volumes", result.stderr)
        self.assertFalse((root / "Prepared.vintelmac").exists())

    def timing_history(self, mode):
        # Only synthetic UUID-scoped preferences are touched. No real guest's
        # history, user setting, password or library is read or changed.
        guest = "worker-protocol-fixture-" + uuid4().hex
        # Ask the real worker adapter to record its CPU/filesystem/update
        # timing namespace; don't duplicate Darwin statfs layout in Python.
        seed, seed_result = self.run_worker("complete", guest)
        self.assertEqual(seed_result.returncode, 0, seed_result.stderr)
        key = plistlib.loads((seed / "Timing.plist").read_bytes())["TimingKey"]
        domain = "com.mac.virtualintelmac"
        def defaults(*args):
            return subprocess.run(("/usr/bin/defaults", *args), capture_output=True, text=True)
        try:
            for stage in ("first", "second"):
                result = defaults("write", domain, key + "-" + stage, "-float", "100")
                self.assertEqual(result.returncode, 0, result.stderr)
            root, result = self.run_worker(mode, guest)
            timings = plistlib.loads((root / "Timing.plist").read_bytes())
            self.assertEqual([stage["Stage"] for stage in timings["Stages"]], ["first", "second"])
            self.assertEqual(result.returncode, 0 if mode == "timing-pass" else 1, result.stderr)
            for stage in ("first", "second"):
                stored = defaults("read", domain, key + "-" + stage)
                self.assertEqual(stored.returncode, 0, stored.stderr)
                seconds = float(stored.stdout)
                if mode == "timing-pass":
                    self.assertGreater(seconds, 60)
                    self.assertLess(seconds, 65)
                else:
                    self.assertEqual(seconds, 100)
        finally:
            for suffix in ("", "-stages", "-first", "-second"):
                defaults("delete", domain, key + suffix)

    def test_failed_attempt_does_not_poison_successful_phase_history(self):
        self.timing_history("timing-fail")

    def test_successful_attempt_learns_every_completed_phase(self):
        self.timing_history("timing-pass")

    def test_absent_or_unreadable_timing_resource_never_blocks_installation(self):
        # The real production worker adapter, isolated from the user's bundle
        # and preference domain. No actual OS install or source resource edit.
        for malformed in (False, True):
            with self.subTest(malformed=malformed), tempfile.TemporaryDirectory() as folder:
                domain = 'com.virtualintelmac.timing-optional-' + uuid4().hex
                contents = Path(folder) / 'TimingOptional.app/Contents'
                binary = contents / 'MacOS/TimingOptional'
                binary.parent.mkdir(parents=True)
                shutil.copyfile(APP, binary)
                binary.chmod(0o700)
                (contents / 'Info.plist').write_bytes(plistlib.dumps({
                    'CFBundleExecutable': binary.name, 'CFBundleIdentifier': domain,
                    'CFBundlePackageType': 'APPL'}))
                if malformed:
                    resources = contents / 'Resources'
                    resources.mkdir()
                    (resources / 'HostInstallTimings.plist').write_bytes(b'not a property list')
                try:
                    root, result = self.run_worker('complete', 'mavericks', str(binary))
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertTrue((root / 'Prepared.vintelmac').is_dir())
                finally:
                    subprocess.run(('/usr/bin/defaults', 'delete', domain), capture_output=True)

    def recover(self, job):
        return subprocess.run((APP, "--test-recover-host-install", job),
                              capture_output=True, timeout=10)

    def clean(self, job):
        return subprocess.run((APP, "--test-clean-host-install", job),
                              capture_output=True, timeout=60)

    def test_retry_retires_only_regenerable_scratch_and_preserves_records(self):
        with tempfile.TemporaryDirectory(dir=os.environ.get("VIM_PUBLISH_TEST_ROOT")) as folder:
            root = Path(folder)
            attempt = root / str(uuid4())
            attempt.mkdir()
            original = root / "Original.dmg"
            original.write_bytes(b"original installer")
            request = {"FormatVersion": 1, "Guest": "mojave", "Media": {"installer": str(original)}}
            (attempt / "Request.plist").write_bytes(plistlib.dumps(request))
            (attempt / "Worker.log").write_bytes(b"diagnostic log")
            (attempt / "Staging.sparsebundle").mkdir()
            (attempt / "Staging.sparsebundle/band").write_bytes(b"incomplete disk")
            (attempt / "Staging.vintelmac").mkdir()
            (attempt / "System.raw").write_bytes(b"incomplete APFS disk")
            (attempt / "System.tree").write_bytes(b"unpublished sealed filesystem tree")
            (attempt / "CustomNotes").write_bytes(b"keep")
            with (attempt / "Worker.log").open("rb") as lease:
                fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
                blocked = self.clean(root)
                self.assertNotEqual(blocked.returncode, 0)
                self.assertTrue((attempt / "Staging.sparsebundle").exists())
            result = self.clean(root)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertFalse((attempt / "Staging.sparsebundle").exists())
            self.assertFalse((attempt / "Staging.vintelmac").exists())
            self.assertFalse((attempt / "System.raw").exists())
            self.assertFalse((attempt / "System.tree").exists())
            self.assertEqual(original.read_bytes(), b"original installer")
            self.assertEqual(plistlib.loads((attempt / "Request.plist").read_bytes()), request)
            self.assertEqual((attempt / "Worker.log").read_bytes(), b"diagnostic log")
            self.assertEqual((attempt / "CustomNotes").read_bytes(), b"keep")
            self.assertEqual(self.clean(root).returncode, 0)

    def test_retry_keeps_an_original_selected_inside_an_attempt(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            attempt = root / str(uuid4())
            attempt.mkdir()
            original = attempt / "System.dmg"
            original.write_bytes(b"user-selected original")
            (attempt / "Request.plist").write_bytes(plistlib.dumps(
                {"FormatVersion": 1, "Media": {"installer": str(original)}}))
            (attempt / "Worker.log").touch()
            result = self.clean(root)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(original.read_bytes(), b"user-selected original")

    def test_retry_normally_detaches_its_real_staging_image_before_removal(self):
        with tempfile.TemporaryDirectory(dir=os.environ.get("VIM_PUBLISH_TEST_ROOT")) as folder, \
                tempfile.TemporaryDirectory(prefix="VIM-HostInstall.") as mount_folder:
            root = Path(folder)
            attempt = root / str(uuid4())
            attempt.mkdir()
            mount = Path(mount_folder) / "Target"
            mount.mkdir()
            original = root / "Original.dmg"
            original.write_bytes(b"original installer")
            (attempt / "Request.plist").write_bytes(plistlib.dumps(
                {"FormatVersion": 1, "Guest": "mojave", "Media": {"installer": str(original)}}))
            (attempt / "Worker.log").touch()
            (attempt / "MountDirectory.plist").write_bytes(plistlib.dumps({"Path": mount_folder}))
            image = attempt / "Staging.sparsebundle"
            subprocess.run(("/usr/bin/hdiutil", "create", "-size", "64m", "-type", "SPARSEBUNDLE",
                            "-fs", "HFS+", "-volname", "VIM Retry Fixture", str(image)),
                           check=True, capture_output=True, timeout=30)
            attached = subprocess.run(("/usr/bin/hdiutil", "attach", "-plist", "-nobrowse", "-owners", "off",
                                       "-mountpoint", str(mount), str(image)),
                                      check=True, capture_output=True, timeout=30)
            device = plistlib.loads(attached.stdout)["system-entities"][0]["dev-entry"]
            try:
                self.assertTrue(mount.is_mount())
                result = self.clean(root)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertFalse(mount.is_mount())
                self.assertFalse(image.exists())
                self.assertTrue(original.exists())
                self.assertTrue((attempt / "Worker.log").exists())
            finally:
                if mount.is_mount():
                    subprocess.run(("/usr/bin/hdiutil", "detach", device), check=True,
                                   capture_output=True, timeout=30)

    def test_retry_detaches_nested_raw_before_its_sparse_workspace(self):
        with tempfile.TemporaryDirectory(dir=os.environ.get("VIM_PUBLISH_TEST_ROOT")) as folder, \
                tempfile.TemporaryDirectory(prefix="VIM-HostInstall.") as mount_folder:
            root, mounts = Path(folder), Path(mount_folder)
            attempt = root / str(uuid4())
            attempt.mkdir()
            original = root / "Original.dmg"
            original.write_bytes(b"original installer")
            (attempt / "Request.plist").write_bytes(plistlib.dumps(
                {"FormatVersion": 1, "Guest": "bigsur", "Media": {"assistant": str(original)}}))
            (attempt / "Worker.log").touch()
            (attempt / "MountDirectory.plist").write_bytes(plistlib.dumps({"Path": mount_folder}))
            (attempt / "DiskSeed.sparsebundle").mkdir()
            workspace = attempt / "RawScratch.sparsebundle"
            outer, inner = mounts / "AssistantRaw", mounts / "System"
            outer.mkdir()
            inner.mkdir()
            subprocess.run(("hdiutil", "create", "-size", "2g", "-type", "SPARSEBUNDLE", "-fs", "APFS",
                            "-volname", "VIM Nested Retry", workspace), check=True, capture_output=True, timeout=30)
            mount = subprocess.run(("hdiutil", "attach", "-plist", "-nobrowse", "-owners", "off",
                                    "-mountpoint", outer, workspace), check=True, capture_output=True, timeout=30)
            outer_device = plistlib.loads(mount.stdout)["system-entities"][0]["dev-entry"]
            inner_device = None
            try:
                raw = outer / "System.dmg"
                subprocess.run(("hdiutil", "create", "-size", "64m", "-type", "UDIF", "-fs", "HFS+",
                                "-volname", "VIM Empty Child", raw), check=True, capture_output=True, timeout=30)
                child = subprocess.run(("hdiutil", "attach", "-plist", "-nobrowse", "-owners", "off",
                                        "-mountpoint", inner, raw), check=True, capture_output=True, timeout=30)
                inner_device = plistlib.loads(child.stdout)["system-entities"][0]["dev-entry"]
                self.assertTrue(outer.is_mount())
                self.assertTrue(inner.is_mount())
                result = self.clean(root)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertFalse(outer.is_mount())
                self.assertFalse(inner.is_mount())
                self.assertFalse(workspace.exists())
                self.assertFalse((attempt / "DiskSeed.sparsebundle").exists())
                self.assertEqual(original.read_bytes(), b"original installer")
                self.assertTrue((attempt / "Worker.log").exists())
            finally:
                if inner_device and inner.is_mount():
                    subprocess.run(("hdiutil", "detach", inner_device), check=True, capture_output=True, timeout=30)
                if outer.is_mount():
                    subprocess.run(("hdiutil", "detach", outer_device), check=True, capture_output=True, timeout=30)

    def test_worker_lease_survives_app_death_and_recovers_completed_disk(self):
        with tempfile.TemporaryDirectory(dir=os.environ.get("VIM_PUBLISH_TEST_ROOT")) as folder:
            job = Path(folder)
            attempt = job / str(uuid4())
            attempt.mkdir()
            worker, request = attempt / "worker", attempt / "Input.plist"
            worker.write_text(f"#!{sys.executable}\n" + WORKER)
            worker.chmod(0o700)
            request.write_bytes(plistlib.dumps({"FormatVersion": 1, "Guest": "worker-protocol-fixture", "TestMode": "orphan"}))
            parent = subprocess.Popen((APP, "--test-host-install-worker", worker, "complete", request),
                                      cwd=attempt, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                      start_new_session=True)
            try:
                parent.stdin.write(PASSWORD)
                parent.stdin.close()
                deadline = time.monotonic() + 8
                while not (attempt / "Started").exists():
                    self.assertIsNone(parent.poll())
                    self.assertLess(time.monotonic(), deadline, "worker did not start")
                    time.sleep(.025)
                parent.kill()
                parent.wait(timeout=5)
                with (attempt / "Worker.log").open("rb") as log:
                    with self.assertRaises(BlockingIOError):
                        fcntl.flock(log, fcntl.LOCK_EX | fcntl.LOCK_NB)
                    busy = self.recover(job)
                    self.assertEqual(busy.returncode, 1, busy.stderr)
                    self.assertIn(b"still finishing", busy.stderr)
                    self.assertFalse((job / "Prepared.vintelmac").exists())
                    (attempt / "Finish").touch()
                    # Observe actual lease release, not merely a completion
                    # marker that could precede worker/child-tool retirement.
                    while True:
                        try:
                            fcntl.flock(log, fcntl.LOCK_EX | fcntl.LOCK_NB)
                            fcntl.flock(log, fcntl.LOCK_UN)
                            break
                        except BlockingIOError:
                            self.assertLess(time.monotonic(), deadline, "worker did not retire")
                            time.sleep(.025)
                ready = self.recover(job)
                self.assertEqual(ready.returncode, 0, ready.stderr)
                self.assertTrue((job / "Prepared.vintelmac").is_dir())
                self.assertFalse((attempt / "Prepared.vintelmac").exists())
                self.assertTrue((attempt / "Request.plist").is_file())
                self.assertEqual(self.recover(job).returncode, 0)
            finally:
                # Only the test process group, including a failed fixture's
                # orphan; never a real installer or host application.
                try:
                    os.killpg(parent.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                parent.wait(timeout=5)
                parent.stdout.close()
                parent.stderr.close()

    def test_recovery_keeps_incomplete_attempts_and_never_overwrites_result(self):
        with tempfile.TemporaryDirectory() as folder:
            job = Path(folder)
            stage = job / "Prepared.vintelmac"
            stage.mkdir()
            (stage / "keep").write_bytes(b"previous completed disk")
            attempt = job / str(uuid4())
            attempt.mkdir()
            (attempt / "Worker.log").touch()
            completed = attempt / "Prepared.vintelmac"
            completed.mkdir()
            (completed / "keep").write_bytes(b"other completed disk")
            partial = job / str(uuid4())
            partial.mkdir()
            (partial / "Worker.log").touch()
            (partial / "Staging.sparsebundle").mkdir()
            result = self.recover(job)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((stage / "keep").read_bytes(), b"previous completed disk")
            self.assertEqual((completed / "keep").read_bytes(), b"other completed disk")
            self.assertTrue((partial / "Staging.sparsebundle").is_dir())


if __name__ == "__main__":
    unittest.main()
