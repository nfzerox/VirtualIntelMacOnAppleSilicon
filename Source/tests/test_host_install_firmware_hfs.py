"""Host-built startup caches may use HFS extent overflow on any release."""
from pathlib import Path
import runpy
import struct
import subprocess
import sys
import tempfile
import unittest


SOURCE = Path(__file__).resolve().parents[1]
FAT32 = runpy.run_path(str(SOURCE / "scripts/efi-fat32.py"))["FAT32"]


class StartupCacheReaderTests(unittest.TestCase):
    def test_all_opengl_profiles_package_the_overflow_capable_hfs_reader(self):
        reader = SOURCE / "prebuilt/PassthroughOpenGL/Firmware/OpenCore/OpenHfsPlus.efi"
        if not reader.is_file():
            self.skipTest("prebuilt firmware inputs are unavailable")
        expected = reader.read_bytes()
        with tempfile.TemporaryDirectory(prefix="vim-hfs-profiles-") as temporary:
            for guest in ("leopard", "snowleopard", "lion", "mountainlion", "mavericks",
                          "yosemite", "elcapitan", "sierra", "highsierra"):
                with self.subTest(guest=guest):
                    output = Path(temporary) / guest
                    result = subprocess.run((sys.executable, SOURCE / "scripts/build-opengl-firmware.py",
                                             "--guest", guest, "--output", output), capture_output=True)
                    self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace"))
                    data = (output / "EFI.img").read_bytes()
                    first, last = struct.unpack_from("<QQ", data, 1056)
                    fat = FAT32(bytearray(data[first * 512:(last + 1) * 512]))
                    self.assertEqual(fat.read("EFI/OC/Drivers/OpenHfsPlus.efi"), expected)
