from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


SOURCE = Path(__file__).resolve().parents[1]


@unittest.skipUnless(sys.platform == "darwin", "Uses the native shipping worker")
class ManualHostAccountTests(unittest.TestCase):
    def test_manual_setup_does_not_create_accounts_or_skip_buddy(self):
        with tempfile.TemporaryDirectory() as folder:
            scratch = Path(folder)
            probe = scratch / "manual-account"
            target = scratch / "Target"
            target.mkdir()
            marker = target / "original"
            marker.write_bytes(b"untouched original fixture")
            sources = [SOURCE / "probes/host-install-manual-account.m",
                       SOURCE / "tools/host-install-pbzx.c",
                       SOURCE / "tools/host-install-bigsur-seal.c",
                       SOURCE / "tools/host-install-bigsur-digest.c"]
            sources += sorted(path for path in (SOURCE / "tools/HostInstall").glob("*.m")
                              if path.name != "install-worker.m")
            subprocess.run(("xcrun", "clang", "-fobjc-arc", "-fobjc-exceptions",
                            "-Wall", "-Wextra", "-Werror", "-DVIM_BIGSUR_DIGEST_LIBRARY",
                            "-I", SOURCE / "vendor/xz-api", "-framework", "Foundation",
                            "-framework", "Security", "-lsqlite3", "-lz", "-llzma.5",
                            "-lcompression", "-larchive", "-lAppleArchive", *sources,
                            "-o", probe), check=True)
            subprocess.run((probe, target), check=True)
            subprocess.run((probe, '--authenticated-home'), check=True)
            subprocess.run((probe, '--initial-screen-policy'), check=True)
            self.assertEqual(list(target.iterdir()), [marker])
            self.assertEqual(marker.read_bytes(), b"untouched original fixture")
            appledouble = scratch / "AppleDouble"
            appledouble.mkdir()
            subprocess.run((probe, '--appledouble', appledouble), check=True)
            for present in ((), ('RecoveryHDMeta.dmg',),
                            ('RecoveryHDMeta.dmg', 'UpdatedRecoveryHDMeta.dmg')):
                with self.subTest(recovery_work=present):
                    for name in present:
                        (target / name).write_bytes(b'owned scratch image')
                    subprocess.run((probe, '--cleanup-recovery', target), check=True)
                    self.assertEqual(list(target.iterdir()), [marker])
                    self.assertEqual(marker.read_bytes(), b"untouched original fixture")


if __name__ == "__main__":
    unittest.main()
