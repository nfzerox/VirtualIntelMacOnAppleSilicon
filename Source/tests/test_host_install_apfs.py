import os
from pathlib import Path
import plistlib
import struct
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import host_install_apfs as apfs
from host_install_single_system_apfs import (APFS_CHECKPOINT, KEXT_REPOSITORIES, PRELINKED_CACHE, SYSTEM_KERNEL,
                                             resume_apfs_assembly, stamp_prelinked_kernel)


class HighSierraAPFSTests(unittest.TestCase):
    def test_reads_recovery_efi_from_container_inside_gpt_padding(self):
        start, end = 40, 122  # APFS uses ten blocks; the GPT has three extra sectors.
        entries = bytearray(128 * 128)
        entries[:16] = apfs.APFS.bytes_le
        struct.pack_into("<QQ", entries, 32, start, end)
        offset = start * apfs.SECTOR
        superblock, jumpstart = bytearray(4096), bytearray(4096)
        struct.pack_into("<4sIQ", superblock, 32, b"NXSB", 4096, 10)
        struct.pack_into("<Q", superblock, 0x4f8, 1)
        blob = b"MZ EFI test driver"
        struct.pack_into("<4sIII", jumpstart, 32, b"JSDR", 1, len(blob), 1)
        struct.pack_into("<QQ", jumpstart, 176, 2, 1)
        blocks = {offset: superblock, offset + 4096: jumpstart, offset + 8192: blob.ljust(4096, b"\0")}
        image = SimpleNamespace(read=lambda at, length: bytes(blocks[at][:length]))
        tables = [(None, 92, 200, 34, 166, 2, entries), (None, 92, 1, 34, 166, 167, entries.copy())]
        with patch.object(apfs, "gpt_header", side_effect=tables):
            self.assertEqual(apfs.recovery_efi_driver(image), blob)

    def test_checker_refuses_a_live_guest_disk_before_preparing_a_file(self):
        with patch.object(apfs, "attached_images", return_value=[{}]), patch.object(apfs, "run") as run:
            with self.assertRaisesRegex(ValueError, "Detach the guest disk"):
                apfs.check_with_period_fsck(Path("Guest.sparsebundle"), Path("BaseSystem"))
            run.assert_not_called()

    def test_container_copy_clips_bands_and_preserves_holes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            bands = root / "bands"
            bands.mkdir()
            (bands / "0").write_bytes(b"abcdefgh")
            (bands / "2").write_bytes(b"qrstuvwx")
            (bands / "._2").write_bytes(b"not part of the disk")
            disk = SimpleNamespace(path=root, band_size=8)
            out = root / "Container.raw"
            apfs.copy_container(disk, out, 3, 16)
            self.assertEqual(out.read_bytes(), b"defgh" + bytes(8) + b"qrs")
            with self.assertRaises(FileExistsError):
                apfs.copy_container(disk, out, 3, 16)

    def test_resume_refuses_unrelated_scratch_or_build_before_mounting(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            stage = root / ".mojave-host-stage"
            (stage / APFS_CHECKPOINT).parent.mkdir(parents=True)
            (stage / APFS_CHECKPOINT).write_bytes(plistlib.dumps({"FormatVersion": 1, "Guest": "mojave",
                                                                 "ProductBuildVersion": "18Gwrong", "DiskGiB": 48}))
            args = SimpleNamespace(guest="mojave", output=root / "New.vintelmac", resume_apfs_stage=stage, size=48)
            with patch("host_install_single_system_apfs.attach") as attach:
                with self.assertRaisesRegex(ValueError, "does not match the requested guest"):
                    resume_apfs_assembly(args)
                args.output = root / "Elsewhere" / "New.vintelmac"
                with self.assertRaisesRegex(ValueError, "new output beside it"):
                    resume_apfs_assembly(args)
                attach.assert_not_called()

    def test_startup_cache_uses_the_native_early_boot_timestamp(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for relative in KEXT_REPOSITORIES:
                (root / relative).mkdir(parents=True)
            for relative in (SYSTEM_KERNEL, PRELINKED_CACHE):
                path = root / relative
                path.parent.mkdir(parents=True)
                path.touch()
            os.utime(root / KEXT_REPOSITORIES[0], (1000, 1000))
            os.utime(root / KEXT_REPOSITORIES[1], (1010, 1010))
            for kernel_time, expected in ((1005, 1011), (1011, 1011), (1020, 1021)):
                with self.subTest(kernel_time=kernel_time):
                    os.utime(root / SYSTEM_KERNEL, (kernel_time, kernel_time))
                    stamp_prelinked_kernel(root)
                    self.assertEqual((root / PRELINKED_CACHE).stat().st_mtime, expected)

    def test_publish_checks_the_period_efi_driver_before_touching_either_gpt(self):
        entries = bytearray(128 * 128)
        entries[:16] = apfs.EFI.bytes_le
        entries[128:144] = apfs.HFS.bytes_le
        struct.pack_into("<QQ", entries, 160, 80, 159)
        offset = 80 * apfs.SECTOR
        superblock = bytearray(4096)
        superblock[32:40] = b"NXSB\x00\x10\x00\x00"
        struct.pack_into("<Q", superblock, 40, 10)
        struct.pack_into("<Q", superblock, 0x4f8, 1)
        record = bytearray(4096)
        driver = b"MZ test driver"
        struct.pack_into("<4sIII", record, 32, b"JSDR", 1, len(driver), 1)
        struct.pack_into("<QQ", record, 176, 2, 1)
        primary, backup = bytearray(512), bytearray(512)
        struct.pack_into("<Q", primary, 24, 1)
        struct.pack_into("<Q", backup, 24, 199)
        headers = [(primary, 92, 199, 34, 166, 2, entries),
                   (backup, 92, 1, 34, 166, 167, entries.copy())]
        blocks = {offset: superblock, offset + 4096: record, offset + 8192: driver.ljust(4096, b"\0")}

        with patch.object(apfs, "SparseBundle") as bundle, patch.object(apfs, "gpt_header") as header:
            volume = bundle.return_value
            volume.read.side_effect = lambda at, length: bytes(blocks[at][:length])
            with patch.object(Path, "read_bytes", return_value=b"wrong contents"):
                header.side_effect = headers
                with self.assertRaisesRegex(ValueError, "does not match original media"):
                    apfs.publish_apfs_partition(Path("New.sparsebundle"), Path("apfs.efi"))
                volume.write.assert_not_called()
            with patch.object(Path, "read_bytes", return_value=driver):
                header.side_effect = headers
                apfs.publish_apfs_partition(Path("New.sparsebundle"), Path("apfs.efi"))
            writes = volume.write.call_args_list
            self.assertEqual([call.args[0] for call in writes], [167 * 512, 199 * 512, 2 * 512, 512])
            self.assertEqual(writes[0].args[1][128:144], apfs.APFS.bytes_le)
