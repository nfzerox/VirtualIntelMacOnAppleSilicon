import os
from pathlib import Path
import plistlib
import subprocess
import sys
import tempfile
import unittest


SOURCE = Path(__file__).resolve().parents[1]
BOOT = "AssetData/boot/"
TICKET = BOOT + "Firmware/Manifests/Cryptex1Boot/apticket.x86legacyap.im4m"
SIDECARS = {
    "AppVolume": "Firmware/app.dmg.root_hash",
    "AppTrustCache": "Firmware/app.dmg.trustcache",
    "SystemVolume": "Firmware/os.dmg.root_hash",
    "SystemTrustCache": "Firmware/os.dmg.trustcache",
}
MEMBERS = [
    BOOT + "RestoreVersion.plist",
    BOOT + "SystemVersion.plist",
    *[BOOT + path for path in SIDECARS.values()],
    TICKET,
    "AssetData/payloadv2/image_patches/cryptex-app",
    "AssetData/payloadv2/image_patches/cryptex-system-x86_64",
]


@unittest.skipUnless(sys.platform == "darwin", "Uses the native Foundation plan")
class NativeCryptexTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        folder = tempfile.TemporaryDirectory()
        cls.addClassCleanup(folder.cleanup)
        cls.scratch = Path(folder.name)
        cls.probe = cls.scratch / "plan"
        subprocess.run(
            ("xcrun", "clang", "-fobjc-arc", "-Wall", "-Wextra", "-Werror",
             "-framework", "Foundation", SOURCE / "probes/host-install-cryptex-plan.m",
             SOURCE / "tools/HostInstall/VIMAssistantCryptex.m", "-o", cls.probe),
            check=True,
        )

    def fixture(self):
        return {"BuildIdentities": [
            {"Info": {"Variant": "Customer Software Update"}},
            {"Info": {"DeviceClass": "x86legacyap", "Variant": "CustomerInstall"},
             "Manifest": {"Cryptex1," + key: {"Info": {"Path": value}}
                          for key, value in SIDECARS.items()}},
            {"Info": {"Variant": "RecoveryBoot"}},
        ]}

    def run_plan(self, members, manifest):
        with tempfile.TemporaryDirectory(dir=self.scratch) as folder:
            listing = Path(folder) / "members.txt"
            source = Path(folder) / "original.plist"
            listing.write_text("\n".join(members), encoding="utf-8")
            source.write_bytes(plistlib.dumps(manifest))
            return subprocess.run((self.probe, listing, source), capture_output=True)

    def test_maps_installed_files_and_preserves_non_ota_identities(self):
        manifest = self.fixture()
        reply = self.run_plan(MEMBERS, manifest)
        self.assertEqual(reply.returncode, 0, reply.stderr)
        plan = plistlib.loads(reply.stdout)
        self.assertEqual(plan["BuildManifest"]["BuildIdentities"], manifest["BuildIdentities"][1:])
        self.assertEqual(plan["Files"]["os.dmg.root_hash"], BOOT + SIDECARS["SystemVolume"])
        self.assertEqual(plan["Files"]["apticket.x86legacyap.im4m"], TICKET)
        self.assertEqual(plan["ImageClones"], {"os.clone.dmg": "os.dmg"})

    def test_missing_or_duplicate_intel_ticket_and_unsafe_sidecar_are_rejected(self):
        self.assertNotEqual(self.run_plan([m for m in MEMBERS if m != TICKET], self.fixture()).returncode, 0)
        self.assertNotEqual(self.run_plan([*MEMBERS, TICKET], self.fixture()).returncode, 0)
        manifest = self.fixture()
        manifest["BuildIdentities"][1]["Manifest"]["Cryptex1,SystemVolume"]["Info"]["Path"] = "Firmware/../os.dmg.root_hash"
        self.assertNotEqual(self.run_plan(MEMBERS, manifest).returncode, 0)

    @unittest.skipUnless(os.getenv("VIM_VENTURA_CRYPTEX_FIXTURES"), "Original Ventura media fixture not configured")
    def test_original_installer_reproduces_reference_manifest(self):
        media = Path(os.environ["VIM_VENTURA_CRYPTEX_FIXTURES"])
        reply = subprocess.run((self.probe, media / "ArchiveMembers.txt", media / "Boot-BuildManifest.plist"),
                               capture_output=True)
        self.assertEqual(reply.returncode, 0, reply.stderr)
        plan = plistlib.loads(reply.stdout)
        reference = Path(os.environ["VIM_VENTURA_CRYPTEX_REFERENCE"])
        self.assertEqual(plan["BuildManifest"], plistlib.loads(reference.read_bytes()))
        self.assertEqual(len(plan["Files"]), 25)


if __name__ == "__main__":
    unittest.main()
