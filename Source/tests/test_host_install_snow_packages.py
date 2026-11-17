from datetime import datetime
from pathlib import Path
import plistlib
import stat
import sys
import tempfile
import unittest
from xml.etree import ElementTree as ET

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from host_install_snow_packages import receipt
from host_install_tiger_packages import catalog_permissions


class SnowReceiptsTests(unittest.TestCase):
    def test_packagekit_receipt_roundtrip(self):
        info = ET.fromstring(
            '<pkg-info identifier="com.apple.pkg.BaseSystem" version="10.6.0.1.1.1238328574">'
            '<groups><group>com.apple.FindSystemFiles.pkg-group</group></groups></pkg-info>')
        data = receipt("BaseSystem", info, 123, {"Volumes": "test"})
        self.assertEqual(plistlib.loads(plistlib.dumps(data, fmt=plistlib.FMT_BINARY)), data)
        self.assertEqual(data["InstallDate"], datetime(1970, 1, 1, 0, 2, 3))
        self.assertEqual(data["InstallPrefixPath"], "/")
        self.assertEqual(data["PackageFileName"], "BaseSystem.pkg")
        self.assertEqual(data["PackageGroups"], ["com.apple.FindSystemFiles.pkg-group"])
        self.assertEqual(data["PackageIdentifier"], "com.apple.pkg.BaseSystem")
        self.assertEqual(data["PackageVersion"], "10.6.0.1.1.1238328574")
        self.assertEqual(data["PathACLs"], {"Volumes": "test"})

    def test_consumed_appledouble_may_leave_no_extended_attribute(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "document").touch()
            mode = (0, 0, stat.S_IFREG | 0o644)
            metadata = {Path("document"): mode, Path("._document"): mode}
            self.assertEqual(catalog_permissions(root, metadata),
                             {(root / "document").stat().st_ino: mode})
            with self.assertRaises(FileNotFoundError):
                catalog_permissions(root, {Path("._missing"): mode})
