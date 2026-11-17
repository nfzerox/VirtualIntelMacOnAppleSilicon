from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


SOURCE = Path(__file__).resolve().parents[1]


@unittest.skipUnless(sys.platform == "darwin", "Uses the native offline guest file adapter")
class NativeDyldIOTests(unittest.TestCase):
    def test_catalina_reads_the_final_projected_system_and_data_identities(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            system=root/"SystemVolume"
            data=root/"DataVolume"
            (system/"usr/share").mkdir(parents=True)
            (system/"System/Volumes").mkdir(parents=True)
            (system/"Library").mkdir()
            (system/"usr/share/firmlinks").write_text("/Library\tLibrary\n")
            (system/"payload").write_bytes(bytes([0x31])*65536)
            (data/"Library").mkdir(parents=True)
            (data/"Library/payload").write_bytes(bytes([0x42])*65536)
            (data/"Library/alias").symlink_to("/payload")
            (data/"Library/escape").symlink_to("/etc/passwd")
            vendor=SOURCE/"vendor/source"
            dyld=vendor/"dyld-catalina-cache/dyld3"
            probe=root/"split-root"
            subprocess.run(("xcrun","clang++","-std=c++17","-arch","arm64","-arch","x86_64",
                            "-mmacosx-version-min=11.0","-O2","-fblocks","-Wall","-Wextra","-Werror",
                            "-I",dyld,"-I",vendor/"dyld-host-compat",
                            SOURCE/"probes/host-install-dyld-split-root.cpp",
                            dyld/"ClosureFileSystemPhysical.cpp","-o",probe),check=True)
            result=subprocess.run((probe,system,data),capture_output=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn(b"final System/Data inodes",result.stdout)

    def test_guest_guards_do_not_protect_live_host_edge_pages(self):
        with tempfile.TemporaryDirectory() as directory:
            probe = Path(directory) / "dyld-pages"
            subprocess.run(
                ("xcrun", "clang++", "-std=c++17", "-arch", "arm64", "-arch", "x86_64",
                 "-mmacosx-version-min=11.0", "-O2", "-Wall", "-Wextra", "-Werror",
                 "-I", SOURCE / "vendor/source/dyld-host-compat",
                 SOURCE / "probes/host-install-dyld-pages.cpp", "-o", probe),
                check=True,
            )
            result = subprocess.run((probe,), capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(b"writable host edge pages", result.stdout)

    def test_native_adapters_preserve_slices_and_stay_in_the_guest(self):
        vendor = SOURCE / "vendor/source"
        for guest in ("mojave", "catalina"):
            with self.subTest(guest=guest), tempfile.TemporaryDirectory() as directory:
                dyld = vendor / f"dyld-{guest}-cache/dyld3"
                probe = Path(directory) / "dyld-filesystem"
                subprocess.run(
                    ("xcrun", "clang++", "-std=c++17", "-arch", "arm64", "-arch", "x86_64",
                     "-mmacosx-version-min=11.0", "-O2", "-fblocks", "-Wall", "-Wextra", "-Werror",
                     "-I", dyld, "-I", vendor / "dyld-host-compat",
                     SOURCE / "probes/host-install-dyld-filesystem.cpp",
                     dyld / "ClosureFileSystemPhysical.cpp", "-o", probe),
                    check=True,
                )
                result = subprocess.run((probe,), capture_output=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn(b"4 KiB slice", result.stdout)
