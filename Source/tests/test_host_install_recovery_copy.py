"""Opt-in native Recovery component check, not a guest OS installation."""
import os
from pathlib import Path
import plistlib
import subprocess
import sys
import tempfile
import unittest


@unittest.skipUnless(sys.platform == "darwin" and os.environ.get("VIM_RECOVERY_COMPONENT_PROBE"),
                     "Set VIM_RECOVERY_COMPONENT_PROBE to the compiled host-install-manual-account probe")
class RecoveryCopyTests(unittest.TestCase):
    def test_recovery_preserves_boot_bytes_without_source_attributes(self):
        probe = os.environ["VIM_RECOVERY_COMPONENT_PROBE"]
        with tempfile.TemporaryDirectory(prefix="vim-recovery-component-") as directory:
            root = Path(directory)
            media = root / "Media"
            media.mkdir()
            payload = b"original recovery file bytes\n"
            for name in ("BaseSystem.dmg", "BaseSystem.chunklist", "boot.efi", "kernelcache"):
                (media / name).write_bytes(payload + name.encode())
            core = media / "System/Library/CoreServices"
            core.mkdir(parents=True)
            for name in ("PlatformSupport.plist", "SystemVersion.plist"):
                (core / name).write_bytes(plistlib.dumps({"Fixture": name}))
            # A harmless source-only xattr demonstrates COPYFILE_DATA's
            # boundary without trying to manufacture a SIP-protected file.
            subprocess.run(("/usr/bin/xattr", "-w", "com.virtualintelmac.source-only",
                            "not guest metadata", media / "boot.efi"), check=True)
            mount = root / "Mount"
            mount.mkdir()
            image = root / "Recovery.sparsebundle"
            subprocess.run((probe, "--prepare-recovery", image, mount, media), check=True)
            subprocess.run(("/usr/bin/hdiutil", "attach", "-readonly", "-owners", "on", "-nobrowse", "-noautoopen",
                            "-mountpoint", mount, image), check=True, stdout=subprocess.DEVNULL)
            try:
                boot = mount / "com.apple.recovery.boot/boot.efi"
                self.assertEqual(boot.read_bytes(), (media / "boot.efi").read_bytes())
                attrs = subprocess.check_output(("/usr/bin/xattr", boot), text=True).splitlines()
                self.assertNotIn("com.virtualintelmac.source-only", attrs)
                self.assertEqual(boot.stat().st_uid, 0)
                self.assertEqual(boot.stat().st_mode & 0o777, 0o644)
            finally:
                subprocess.run(("/usr/bin/hdiutil", "detach", mount), check=True)


if __name__ == "__main__":
    unittest.main()
