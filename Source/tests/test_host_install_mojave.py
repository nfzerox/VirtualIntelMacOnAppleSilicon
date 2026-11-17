import plistlib
from pathlib import Path
import stat
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from host_install_hfs import Target
import host_install_mojave as mojave


class MojaveHostGraphicsTests(unittest.TestCase):
    def test_catalina_preserves_only_the_known_renderer_alias(self):
        with tempfile.TemporaryDirectory() as folder:
            work = Path(folder)
            root, payload = work / "root", work / "payload"
            root.mkdir()
            target = Target(root, {})
            alias = payload / mojave.CATALINA_ALIAS
            renderer = alias.with_name("AppleMetalOpenGLRenderer.bundle")
            renderer.mkdir(parents=True)
            alias.symlink_to(renderer.name)
            targets = work / "targets.txt"
            targets.write_text(f"/{mojave.CATALINA_ALIAS}\n")
            with patch.object(mojave, "CATALINA_TARGETS", targets):
                mojave.publish_graphics(target, payload, "19H15", guest="catalina")
            self.assertEqual((root / mojave.CATALINA_ALIAS).readlink(), Path(renderer.name))
            self.assertTrue(stat.S_ISLNK(target.metadata[mojave.CATALINA_ALIAS][2]))

    def test_stock_bundle_and_catalog_metadata_follow_the_backup(self):
        with tempfile.TemporaryDirectory() as folder:
            work = Path(folder)
            root = work / "root"
            root.mkdir()
            target = Target(root, {})
            bundle = Path("System/Library/Extensions/Graphics.kext")
            old = bundle / "stock"
            binary = Path("System/Library/Frameworks/Graphics.framework/Versions/A/Graphics")
            target.write(old, b"native system file", 0o644)
            target.write(binary, b"native binary", 0o755)
            old_attributes = target.metadata[old]
            payload = work / "payload"
            (payload / bundle).mkdir(parents=True)
            (payload / bundle / "driver").write_bytes(b"replacement driver")
            (payload / bundle / "driver").chmod(0o700)
            (payload / binary).parent.mkdir(parents=True)
            (payload / binary).write_bytes(b"replacement binary")
            (payload / binary).chmod(0o600)
            targets = work / "targets.txt"
            targets.write_text(f"/{bundle}\n/{binary}\n")

            with patch.object(mojave, "TARGETS", targets):
                mojave.publish_graphics(target, payload, "18G103")

            stock = mojave.STOCK / "18G103" / old
            self.assertEqual((root / stock).read_bytes(), b"native system file")
            self.assertEqual(target.metadata[stock], old_attributes)
            self.assertNotIn(old, target.metadata)
            self.assertEqual((root / bundle / "driver").read_bytes(), b"replacement driver")
            self.assertEqual(target.metadata[bundle / "driver"], (0, 0, stat.S_IFREG | 0o755))
            self.assertEqual((root / binary).read_bytes(), b"replacement binary")
            self.assertEqual(target.metadata[binary], (0, 0, stat.S_IFREG | 0o644))
            policy = root / "Library/Preferences/com.apple.security.libraryvalidation.plist"
            self.assertIs(plistlib.loads(policy.read_bytes())["DisableLibraryValidation"], True)
