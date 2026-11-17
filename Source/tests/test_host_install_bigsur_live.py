import importlib.util
from pathlib import Path
import unittest


SPEC = importlib.util.spec_from_file_location("verify_bigsur_live", Path(__file__).resolve().parents[1] /
                                              "scripts/verify-host-install-bigsur-live.py")
PROBE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PROBE)
BOOT = "10000000-1234-1234-1234-123456789123"
GROUP = "20000000-1234-1234-1234-123456789123"
SNAPSHOT = "30000000-1234-1234-1234-123456789123"
MACHINE = "40000000-1234-1234-1234-123456789123"
FOLLOW_UP = "50000000-1234-1234-1234-123456789123"
BUILD = "20G1443"


class BigSurLiveEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.disk = Path("System.qcow2").resolve()
        self.blocks = [{"device": "macos", "removable": False, "inserted": {"image": {"filename": str(self.disk)}}},
                       {"device": "pflash1", "inserted": {"image": {"filename": "NVRAM.fd"}}},
                       {"removable": True}]
        self.assembly = {"Phase": "graphics-verified", "MachineIdentifier": MACHINE, "ProductBuildVersion": BUILD,
                         "VolumeUUID": GROUP, "BootSnapshotUUID": SNAPSHOT, "GraphicsVerification": {
                             "MachineIdentifier": MACHINE, "MetalCommandVerified": True, "BootSessionUUID": BOOT,
                             "ProductBuildVersion": BUILD, "VolumeUUID": GROUP}}
        self.report = {"Guest": "bigsur", "InstalledSystemCandidate": True, "KernelMatchesSystem": True,
                       "DesktopRunning": 1, "LiveRoot": True, "ProcessMachine": "x86_64", "ConsoleUID": 501,
                       "ProductBuildVersion": BUILD, "VolumeUUID": GROUP, "BootSessionUUID": BOOT}
        self.output = f'''Build: {BUILD}
Boot session: {BOOT}
Console: mac 501
/dev/disk2s3s1 on / (apfs, sealed, local, read-only, journaled)
   APFS Snapshot UUID:        {SNAPSHOT}
   APFS Volume Group:         {GROUP}
   Sealed:                    Broken
    +-> Volume disk2s3
    |   Sealed:                    Broken
    |   Snapshot:                  {SNAPSHOT}
    |   Snapshot Disk:             disk2s3s1
    |   Snapshot Sealed:           Yes
Authenticated Root status: enabled
    "IOGLBundleName" = "AppleMetalOpenGLRenderer"
'''

    def verify(self, **kwargs):
        PROBE.check_live(self.assembly, {"Identifier": MACHINE}, "mac", self.report, self.output, self.blocks,
                         self.disk, **kwargs)

    def test_sealed_mounted_snapshot_even_when_live_system_is_unsealed(self):
        self.verify()

    def test_detects_restart_between_app_and_guest_reports(self):
        self.output = self.output.replace(BOOT, MACHINE)
        with self.assertRaisesRegex(ValueError, "restarted"):
            self.verify()

    def test_rejects_loginwindow_or_recovery(self):
        self.report["DesktopRunning"] = False
        with self.assertRaisesRegex(ValueError, "live Intel desktop"):
            self.verify()

    def test_does_not_accept_a_different_sealed_snapshot(self):
        self.output = self.output.replace("Snapshot:                  " + SNAPSHOT, "Snapshot:                  " + MACHINE)
        with self.assertRaisesRegex(ValueError, "snapshot is not sealed"):
            self.verify()

    def test_rejects_install_media_even_on_an_accelerated_desktop(self):
        self.blocks[-1]["inserted"] = {"image": {"filename": "Recovery.iso"}}
        with self.assertRaisesRegex(ValueError, "no inserted removable media"):
            self.verify()

    def test_follow_up_needs_a_different_boot_and_a_new_readback(self):
        previous = {"Assembly": self.assembly, "LiveReport": self.report.copy(),
                    "Blocks": [*self.blocks]}
        metal = dict(self.assembly["GraphicsVerification"])
        with self.assertRaisesRegex(ValueError, "new boot"):
            self.verify(previous=previous, metal=metal)
        self.report["BootSessionUUID"] = FOLLOW_UP
        self.output = self.output.replace(BOOT, FOLLOW_UP)
        with self.assertRaisesRegex(ValueError, "new boot"):
            self.verify(previous=previous, metal=metal)
        metal["BootSessionUUID"] = FOLLOW_UP
        self.verify(previous=previous, metal=metal)
        self.blocks[1] = {"device": "pflash1", "inserted": {"image": {"filename": "Other.fd"}}}
        with self.assertRaisesRegex(ValueError, "original pflash1"):
            self.verify(previous=previous, metal=metal)


if __name__ == "__main__":
    unittest.main()
