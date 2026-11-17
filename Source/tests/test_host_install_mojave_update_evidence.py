import importlib.util
from pathlib import Path
import unittest


SPEC = importlib.util.spec_from_file_location("verify_mojave_update", Path(__file__).resolve().parents[1] /
                                              "scripts/verify-mojave-host-install-update.py")
PROBE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PROBE)


class MojaveUpdateEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.machine = {"Guest": "mojave", "Identifier": "vm"}
        self.assembly = {"ProductBuildVersion": PROBE.BASE, "VolumeUUID": "disk", "MachineIdentifier": "vm"}
        self.baseline = {"Build": PROBE.BASE, "Boot session": "before", "Console": "mac 501",
                         "GeneratedUID": "account", "Marker UID": "501", "Marker SHA-256": "original"}
        self.live = {**self.baseline, "Build": PROBE.TARGET, "Boot session": "after"}
        self.metal = {"MetalCommandVerified": True, "ProductBuildVersion": PROBE.TARGET,
                      "VolumeUUID": "disk", "BootSessionUUID": "after"}
        self.report = {**self.metal, "Guest": "mojave", "InstalledSystemCandidate": 1,
                       "LiveRoot": 1, "KernelMatchesSystem": 1, "DesktopRunning": 1}
        self.repair = {"Phase": "graphics-verified", "TransactionID": "repair", "PostBootVerification": {
            **self.metal, "TransactionID": "repair", "VerifiedAfterReboot": True}}

    def check(self, previous=None):
        return PROBE.check_update(self.machine, self.baseline, self.assembly, self.repair, self.report,
                                  self.live, self.metal, previous)

    def test_requires_a_new_boot_and_preserves_repair_receipt(self):
        proof = self.check()
        self.assertFalse(proof["SubsequentNormalBoot"])
        with self.assertRaisesRegex(ValueError, "new session"):
            self.check(proof)
        self.live["Boot session"] = self.metal["BootSessionUUID"] = self.report["BootSessionUUID"] = "later"
        self.assertTrue(self.check(proof)["SubsequentNormalBoot"])
        self.repair["PostBootVerification"]["BootSessionUUID"] = "later"
        self.assertTrue(self.check(proof)["SubsequentNormalBoot"])

    def test_looks_at_live_metal_not_only_an_old_receipt(self):
        self.metal["MetalCommandVerified"] = False
        with self.assertRaisesRegex(ValueError, "Metal command/readback"):
            self.check()

    def test_rejects_a_recreated_account_or_modified_file(self):
        for key in ("GeneratedUID", "Marker SHA-256"):
            with self.subTest(key=key):
                self.live[key] = "different"
                with self.assertRaisesRegex(ValueError, "not preserved"):
                    self.check()
                self.live[key] = self.baseline[key]
