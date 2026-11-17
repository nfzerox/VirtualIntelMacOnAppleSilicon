import hashlib
from pathlib import Path
import plistlib
import stat
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
from uuid import UUID
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from host_install_hfs import Target, discard_partial_disks, setup_state, stage_account, stage_local_account_record
from host_install_hfs_recovery import gpt_header


class LionHostInstallTests(unittest.TestCase):
    def test_failed_assembly_keeps_live_disk_then_reclaims_only_detached_outputs(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            stage = root / "stage"
            disk = stage / "Disks/Staging.sparsebundle"
            disk.mkdir(parents=True)
            (disk / "Info.plist").touch()
            partial = stage / "Disks/System.qcow2"
            partial.touch()
            diagnostics = stage / "Logs/Prelink.log"
            diagnostics.parent.mkdir()
            diagnostics.touch()
            original = root / "Apple.dmg"
            original.touch()
            mounted = plistlib.dumps({"images": [{"image-path": str(original)}, {"image-path": str(disk)}]})
            with patch("host_install_hfs.check_output", return_value=mounted):
                self.assertFalse(discard_partial_disks(stage))
            self.assertTrue(disk.is_dir())
            self.assertTrue(partial.is_file())
            detached = plistlib.dumps({"images": [{"image-path": str(original)}]})
            with patch("host_install_hfs.check_output", return_value=detached):
                self.assertTrue(discard_partial_disks(stage))
            self.assertFalse(disk.exists())
            self.assertFalse(partial.exists())
            self.assertTrue(original.is_file())
            self.assertTrue(diagnostics.is_file())

    def test_staging_can_traverse_el_capitans_mode_0600_directory_record(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            target = Target(root, {})
            node = Path("private/var/db/dslocal/nodes/Default")
            target.write_plist(node / "users/root.plist", {"name": ["root"], "uid": ["0"]})
            target.write_plist(node / "groups/admin.plist", {"users": ["root"]})
            (root / node).chmod(0o600)
            target.metadata[node] = (0, 0, stat.S_IFDIR | 0o600)
            self.assertEqual(stage_local_account_record(target, "first", "First", str(UUID(int=1)),
                                                        b"example", pbkdf2=True), 501)
            self.assertEqual(stat.S_IMODE((root / node).stat().st_mode), 0o700)
            self.assertEqual(target.metadata[node], (0, 0, stat.S_IFDIR | 0o600))

    def test_yosemite_stages_minibuddy_without_enabling_automatic_login(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            target = Target(root, {})
            node = Path("private/var/db/dslocal/nodes/Default")
            target.write_plist(node / "users/root.plist", {"name": ["root"], "uid": ["0"]})
            target.write_plist(node / "groups/admin.plist", {"users": ["root"]})
            target.mkdir("System/Library/User Template/English.lproj/Library/Preferences")
            target.write_plist("System/Library/CoreServices/SystemVersion.plist",
                               {"ProductVersion": "10.10.5", "ProductBuildVersion": "14F27"})
            stage_account(target, "first", "First", b"example", {"Guest": "yosemite"}, False, False)
            prefs = plistlib.loads((root / "Users/first/Library/Preferences/com.apple.SetupAssistant.plist").read_bytes())
            self.assertEqual(prefs, {"DidSeeCloudSetup": True, "DidSeeSyncSetup": True,
                                    "DidSeeSyncSetup2": True, "LastSeenCloudProductVersion": "10.10.5",
                                    "LastSeenSyncProductVersion": "10.10.5", "LastSeenBuddyBuildVersion": "14F27"})
            self.assertTrue((root / "private/var/db/.AppleSetupDone").is_file())
            self.assertFalse((root / "private/etc/kcpassword").exists())
            self.assertFalse((root / "Library/Receipts/.SetupRegComplete").exists())
            journal = plistlib.loads((root / setup_state("yosemite") / "Account.plist").read_bytes())
            self.assertEqual(journal["Phase"], "complete")
            self.assertNotIn("AutoLoginSHA256", journal)

    def test_mojave_home_combines_the_shared_and_localized_templates(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            target = Target(root, {})
            node = Path("private/var/db/dslocal/nodes/Default")
            target.write_plist(node / "users/root.plist", {"name": ["root"], "uid": ["0"]})
            target.write_plist(node / "groups/admin.plist", {"users": ["root"]})
            templates = Path("System/Library/User Template")
            target.mkdir(templates / "English.lproj/Desktop")
            target.write(templates / "English.lproj/Library/Localized", b"English")
            target.mkdir(templates / "Non_localized/Documents", 0o700)
            target.mkdir(templates / "Non_localized/Downloads", 0o700)
            target.write(templates / "Non_localized/Library/Shared", b"Common")
            target.write_plist("System/Library/CoreServices/SystemVersion.plist",
                               {"ProductVersion": "10.14.6", "ProductBuildVersion": "18G103"})
            stage_account(target, "first", "First", b"example", {"Guest": "mojave"}, False, False)
            home = Path("Users/first")
            for directory in ("Desktop", "Documents", "Downloads"):
                self.assertTrue((root / home / directory).is_dir())
                self.assertEqual(target.metadata[home / directory][:2], (501, 20))
            self.assertEqual((root / home / "Library/Shared").read_bytes(), b"Common")
            self.assertEqual((root / home / "Library/Localized").read_bytes(), b"English")
            self.assertEqual(stat.S_IMODE(target.metadata[home / "Downloads"][2]), 0o700)

    def test_lion_local_password_is_embedded_not_an_external_shadow(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            target = Target(root, {})
            node = Path("private/var/db/dslocal/nodes/Default")
            target.write_plist(node / "users/root.plist", {"name": ["root"], "uid": ["0"]})
            target.write_plist(node / "groups/admin.plist", {"users": ["root"]})
            guid, password, salt = "00000000-0000-0000-0000-000000000001", b"example", b"salt"
            with patch("host_install_hfs.secrets.token_bytes", return_value=salt):
                self.assertEqual(stage_local_account_record(target, "first", "First", guid, password), 501)
            user = plistlib.loads((root / node / "users/first.plist").read_bytes())
            self.assertEqual(user["authentication_authority"], [";ShadowHash;HASHLIST:<SALTED-SHA512>"])
            self.assertEqual(plistlib.loads(user["ShadowHashData"][0]),
                             {"SALTED-SHA512": salt + hashlib.sha512(salt + password).digest()})
            self.assertFalse((root / "private/var/db/shadow").exists())
            admin = plistlib.loads((root / node / "groups/admin.plist").read_bytes())
            self.assertEqual(admin["users"], ["root", "first"])
            self.assertEqual(admin["groupmembers"], [guid])

    def test_mountain_lion_uses_a_nested_pbkdf2_envelope(self):
        with tempfile.TemporaryDirectory() as temporary:
            root, salt, password = Path(temporary), b"s" * 32, b"example"
            target = Target(root, {})
            node = Path("private/var/db/dslocal/nodes/Default")
            target.write_plist(node / "users/root.plist", {"name": ["root"], "uid": ["0"]})
            target.write_plist(node / "groups/admin.plist", {"users": ["root"]})
            with patch("host_install_hfs.secrets.token_bytes", return_value=salt):
                stage_local_account_record(target, "first", "First", str(UUID(int=1)), password, pbkdf2=True)
            user = plistlib.loads((root / node / "users/first.plist").read_bytes())
            algorithm = "SALTED-SHA512-PBKDF2"
            self.assertEqual(user["authentication_authority"], [f";ShadowHash;HASHLIST:<{algorithm}>"])
            record = plistlib.loads(user["ShadowHashData"][0])[algorithm]
            self.assertEqual(record["salt"], salt)
            self.assertEqual(record["entropy"], hashlib.pbkdf2_hmac("sha512", password, salt, record["iterations"], 128))

    def test_gpt_reader_validates_the_header_and_entry_crcs(self):
        entries = bytearray(128 * 128)
        block = bytearray(512)
        struct.pack_into("<8sIIIIQQQQ16sQIII", block, 0, b"EFI PART", 0x10000, 92, 0, 0,
                         1, 999, 34, 966, UUID(int=1).bytes_le, 2, 128, 128, zlib.crc32(entries))
        struct.pack_into("<I", block, 16, zlib.crc32(block[:92]))

        class Image:
            def read(self, offset, length):
                return {512: block, 1024: entries}[offset][:length]

        self.assertEqual(gpt_header(Image(), 1)[2:6], (999, 34, 966, 2))
        entries[0] = 1
        with self.assertRaisesRegex(ValueError, "partition table checksum"):
            gpt_header(Image(), 1)
        block[24] = 2
        with self.assertRaisesRegex(ValueError, "header checksum"):
            gpt_header(Image(), 1)
