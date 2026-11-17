import io
from pathlib import Path
import struct
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import DEFAULT, patch
from uuid import uuid4

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import host_install_bigsur as installer


class BigSurAssemblyTests(unittest.TestCase):
    def test_recovery_only_reuses_detached_compression_verified_system(self):
        group = uuid4()
        system = SimpleNamespace(role=1, sealed=False, snapshot_count=0,
                                 superblock=bytes(0x3f0) + group.bytes, nodes=lambda: [])
        data = SimpleNamespace(role=64, uuid=group)
        with tempfile.TemporaryDirectory() as folder, patch.multiple(
            installer, **{name: DEFAULT for name in ("attached_images", "APFS", "detached_image",
                                                       "compare_archive_compression", "original_fixups")}
        ) as mocks:
            work = Path(folder)
            image = work / "System.dmg"
            image.touch()
            mocks["attached_images"].return_value = []
            mocks["APFS"].return_value.volumes.return_value = [system, data]
            mocks["compare_archive_compression"].return_value = {"Differences": {}}
            self.assertEqual(installer.recovered_extraction_group(image, None, work, 0), str(group).upper())
            mocks["compare_archive_compression"].return_value = {"Differences": {"ResourceSize": 1}}
            with self.assertRaisesRegex(ValueError, "did not complete"):
                installer.recovered_extraction_group(image, None, work, 0)
            for kind in (installer.INODE, installer.XATTR):
                key = struct.pack("<QH", kind << 60, len(installer.PLACEHOLDER)) + installer.PLACEHOLDER
                inode = bytes(68) + struct.pack("<I", installer.SF_FIRMLINK)
                system.nodes = lambda: [SimpleNamespace(leaf=True, entries=[SimpleNamespace(key=key, value=inode)])]
                with self.assertRaisesRegex(ValueError, "Do not remount"):
                    installer.recovered_extraction_group(image, None, work, 0)
            mocks["attached_images"].return_value = ["attached"]
            with self.assertRaisesRegex(ValueError, "detached"):
                installer.recovered_extraction_group(image, None, work, 0)

    def test_host_mounts_finish_before_any_detached_edits(self):
        order = []
        with tempfile.TemporaryDirectory() as folder, patch.multiple(
            installer, **{name: DEFAULT for name in (
                "signed_asset", "inspect_asset", "format_native_seed", "attached_gpt", "create_split_volume_group",
                "stage_system_volume", "stage_data_image", "stage_boot_assets", "project_mutable_volumes",
                "stage_boot_selector", "finalize_system_volume", "extract_seal_inputs", "APFS", "detached_image",
                "remapping_dictionary", "original_seal_metadata", "export_verified_tree", "canonical_metadata",
                "recovery_efi_driver", "publish_native_jumpstart", "assemble", "write_bundle_blocks", "run")}
        ) as mocks, patch.object(installer.subprocess, "Popen") as popen:
            work = Path(folder)
            image = work / "System.sparsebundle"
            media, app, tools = (work / name for name in ("Original.pkg", "Runtime", "HostTools"))
            archive = object()
            group = "new group"
            mocks["signed_asset"].return_value.__enter__.return_value = (None, archive)
            mocks["inspect_asset"].return_value = {"Build": "20G1443"}
            mocks["attached_gpt"].return_value.__enter__.return_value = ("disk1", [])
            mocks["create_split_volume_group"].return_value = ({}, group)
            def stage(*args, with_system, decoder, already_extracted):
                self.assertEqual(decoder, tools / "host-install-pbzx")
                self.assertFalse(already_extracted)
                order.append("mount-system")
                with_system(work / "SystemRoot")
                order.append("detach-system")
                return "reserved stubs"
            mocks["stage_system_volume"].side_effect = stage
            mocks["stage_data_image"].return_value = work / "Data.sparsebundle"
            expected = b"apple signed digest"
            mocks["stage_boot_assets"].return_value = expected
            for operation in ("project_mutable_volumes", "stage_boot_selector", "finalize_system_volume", "write_bundle_blocks"):
                mocks[operation].side_effect = lambda *args, name=operation: order.append(name)
            mocks["publish_native_jumpstart"].side_effect = lambda *args: (
                order.append("publish_native_jumpstart") or SimpleNamespace(image=image, offset=0))
            manifest = work / "mtree.txt"
            manifest.touch()
            mocks["extract_seal_inputs"].return_value = manifest, work / "digest.db"
            mocks["APFS"].return_value.volumes.return_value = [SimpleNamespace(role=1)]
            mocks["remapping_dictionary"].return_value = ({"CHANGE": 42, "12": 16}, {})
            reader = popen.return_value.__enter__.return_value
            reader.stdout = io.BytesIO()
            reader.wait.return_value = 0
            mocks["run"].side_effect = lambda *args: (work / "Recovery.raw").touch()
            mocks["recovery_efi_driver"].return_value = b"Apple Intel EFI"
            result = installer.prepare_system(image, media, app, tools, work, ("mac", "Test", b"secret", True, False), 32)
            self.assertEqual(result, expected)
            self.assertEqual(order, ["mount-system", "detach-system", "project_mutable_volumes", "stage_boot_selector",
                                     "finalize_system_volume", "publish_native_jumpstart", "write_bundle_blocks"])
            mocks["signed_asset"].assert_called_once_with(media)
            mocks["format_native_seed"].assert_called_once_with(image, 32, flat=True)
            self.assertEqual(mocks["stage_data_image"].call_args.kwargs["host_tools"], tools)
            self.assertEqual(mocks["stage_data_image"].call_args.args[4]["VolumeUUID"], group)
            mocks["publish_native_jumpstart"].assert_called_once_with(image, b"Apple Intel EFI")


if __name__ == "__main__":
    unittest.main()
