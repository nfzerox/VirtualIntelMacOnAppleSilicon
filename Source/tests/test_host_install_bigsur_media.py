import io
import lzma
from pathlib import Path
import plistlib
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import host_install_bigsur_media as media
from host_install_bigsur_boot import CORE, FIRMWARE, KC, intel_root_hash, stage_intel_seal_assets, stage_recovery_manifests


def pbzx(contents, compress=False):
    stored = lzma.compress(contents) if compress else contents
    return b"pbzx" + struct.pack(">QQQ", len(contents), len(contents), len(stored)) + stored


def cache(architecture):
    fields = media.DYLD + architecture
    return pbzx(b"YAA1" + struct.pack("<H", 6 + len(fields)) + fields +
                b"dyld_v1" + architecture.rjust(8, b" ") + b"\0")


class BigSurAssetTests(unittest.TestCase):
    def test_publishes_original_seal_members_under_installed_preboot_names(self):
        data = io.BytesIO()
        root = FIRMWARE + "Example.dmg.x86."
        prefix = FIRMWARE + "Manifests/Preboot/"
        with zipfile.ZipFile(data, "w") as archive:
            for kind in ("root_hash", "mtree"):
                archive.writestr(root + kind, kind.encode())
                signature = "usr/standalone/OS.dmg." + kind + ".x86legacyap.im4m"
                archive.writestr(prefix + signature, b"Intel Preboot manifest")
                archive.writestr(prefix.replace("Preboot", "Boot") + signature, b"different boot context")
        with tempfile.TemporaryDirectory() as tmp, zipfile.ZipFile(data) as archive:
            preboot = Path(tmp)
            stage_intel_seal_assets(archive, root + "root_hash", preboot)
            for kind in ("root_hash", "mtree"):
                member = preboot / ("usr/standalone/OS.dmg." + kind)
                self.assertEqual(member.read_bytes(), kind.encode())
                self.assertEqual(member.with_name(member.name + ".x86legacyap.im4m").read_bytes(),
                                 b"Intel Preboot manifest")

    def test_recovery_manifests_match_native_flat_group_layout(self):
        archive_bytes = io.BytesIO()
        prefix = FIRMWARE + "Manifests/RecoveryBoot/"
        paths = ("Firmware/BaseSystem.dmg.x86.trustcache", CORE + "boot.efi", CORE + "bootbase.efi", KC)
        with zipfile.ZipFile(archive_bytes, "w") as archive:
            for path in paths:
                archive.writestr(prefix + path + ".x86legacyap.im4m", path.encode())
                archive.writestr(prefix.replace("RecoveryBoot", "Preboot") + path + ".j137ap.im4m",
                                 b"not a Recovery signature")
        with tempfile.TemporaryDirectory() as folder, zipfile.ZipFile(archive_bytes) as archive:
            recovery = Path(folder)
            stage_recovery_manifests(archive, recovery)
            self.assertEqual({p.name: p.read_bytes() for p in recovery.iterdir()},
                             {Path(path).name + ".x86legacyap.im4m": path.encode() for path in paths})

    def test_intel_seal_payload_validates_architecture_and_truncation(self):
        def element(tag, data):
            return bytes((tag, len(data))) + data

        def img4(kind):
            payload = struct.pack("<QII", 2, 1, 32) + expected
            return element(0x30, b"".join(element(0x16, value) for value in (b"IM4P", kind, b"Test")) +
                           element(0x04, payload))

        expected = bytes(range(32))
        self.assertEqual(intel_root_hash(img4(b"xsys")), expected)
        with self.assertRaisesRegex(ValueError, "Unsupported"):
            intel_root_hash(img4(b"asys"))
        with self.assertRaisesRegex(ValueError, "Truncated"):
            intel_root_hash(img4(b"xsys")[:-1])

    def test_prefix_handles_raw_and_xz_blocks(self):
        contents = b"YAA1" + b"\x00" * 4096
        for compress in (False, True):
            encoded = pbzx(contents, compress)
            self.assertEqual(media.first_record_prefix(io.BytesIO(encoded)), contents)
            with self.assertRaisesRegex(ValueError, "Truncated"):
                media.first_record_prefix(io.BytesIO(encoded[:-1]))

    def test_payload_stream_reads_every_block_and_accepts_empty_companion(self):
        first, last = b"YAA1" + b"a" * 80, b"z" * 32
        stream = pbzx(first) + pbzx(last)[12:]
        self.assertEqual(list(media.decode_pbzx(io.BytesIO(stream))), [first, last])
        self.assertEqual(list(media.decode_pbzx(io.BytesIO(stream[:12]))), [])
        with self.assertRaisesRegex(ValueError, "Truncated"):
            list(media.decode_pbzx(io.BytesIO(stream[:-1])))

    def test_staging_restores_native_template_mode_after_success_or_failure(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            account = root / media.ACCOUNT_TEMPLATE
            account.mkdir(parents=True, mode=0o600)

            def extract(*_):
                self.assertEqual(account.stat().st_mode & 0o777, 0o700)

            with patch.object(media, "run_archive", side_effect=extract):
                media.apply_main_chunks(None, root, [{"Member": "test"}])
            self.assertEqual(account.stat().st_mode & 0o777, 0o600)
            with patch.object(media, "run_archive", side_effect=RuntimeError("failed extract")):
                with self.assertRaisesRegex(RuntimeError, "failed extract"):
                    media.apply_main_chunks(None, root, [{"Member": "test"}])
            self.assertEqual(account.stat().st_mode & 0o777, 0o600)

    def test_inventory_requires_matching_release_and_preserves_intel_caches(self):
        data = io.BytesIO()
        root_hash = "AssetData/boot/Firmware/Example.dmg.x86.root_hash"
        with zipfile.ZipFile(data, "w") as archive:
            archive.writestr("AssetData/Info.plist", plistlib.dumps({"ProductVersion": "11.7.11", "Build": "20G1443"}))
            archive.writestr(media.PAYLOAD + "payload_chunks.txt", "0: 512\n1: 1024\n")
            archive.writestr(media.PAYLOAD + "payload.000", cache(b"x86_64"))
            archive.writestr(media.PAYLOAD + "payload.001", cache(b"x86_64h"))
            archive.writestr(media.PAYLOAD + "firmlinks_payload", b"fake manifest for mocked aa")
            archive.writestr(root_hash, b"root hash fixture")
        with zipfile.ZipFile(data) as archive, patch.object(media.subprocess, "check_output",
                                                            return_value=b'[{"PAT":"Applications"}]'):
            with self.assertRaisesRegex(ValueError, "disagree"):
                media.inspect_asset({"OSVersion": "11.7.10", "Build": "20G1427"}, archive)
            result = media.inspect_asset({"OSVersion": "11.7.11", "Build": "20G1443"}, archive)
            self.assertEqual(result["SharedCaches"], {"x86_64": media.PAYLOAD + "payload.000",
                                                     "x86_64h": media.PAYLOAD + "payload.001"})
            self.assertEqual(result["FirmlinkStubs"], ["Applications"])
            self.assertEqual(result["IntelRootHashes"], [root_hash])


if __name__ == "__main__":
    unittest.main()
