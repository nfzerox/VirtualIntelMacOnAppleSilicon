from pathlib import Path
import stat
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from host_install_catalina import TEMPLATE, materialize_data_template, parse_firmlinks
from host_install_hfs import Target


MANIFEST = ("/AppleInternal\tAppleInternal\n/Applications\tApplications\n/Library\tLibrary\n"
            "/Users\tUsers\n/private\tprivate\n/usr/local\tusr/local\n")


class CatalinaProjectionTests(unittest.TestCase):
    def test_firmlink_manifest_is_relative_and_non_overlapping(self):
        self.assertEqual(parse_firmlinks(MANIFEST)[Path("usr/local")], Path("usr/local"))
        self.assertNotIn(Path("AppleInternal"), parse_firmlinks(MANIFEST))
        for suffix in ("/Users" + "/nested\tOthers\n", "/x\tprivate/a\n", "/x\t../outside\n",
                       "/x\t/absolute\n", "/x\tUsers\n"):
            with self.subTest(suffix=suffix), self.assertRaises(ValueError):
                parse_firmlinks(MANIFEST + suffix)

    def test_native_data_template_is_visible_in_combined_staging_root(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            target = Target(root, {})
            target.write("usr/share/firmlinks", MANIFEST.encode())
            record = Path("private/var/db/dslocal/root.plist")
            target.write(TEMPLATE / record, b"stock local record", 0o600)
            target.metadata[TEMPLATE / record] = (0, 0, stat.S_IFREG | 0o600)
            protected = TEMPLATE / record.parent
            (root / protected).chmod(0o600)
            target.metadata[protected] = (0, 0, stat.S_IFDIR | 0o600)
            target.mkdir(TEMPLATE / "Applications/Safari.app")
            target.mkdir("System/Applications/Utilities/System Information.app")
            target.mkdir(TEMPLATE / "mnt")
            materialize_data_template(target)
            self.assertEqual((root / record).read_bytes(), b"stock local record")
            self.assertEqual(target.metadata[record], (0, 0, stat.S_IFREG | 0o600))
            self.assertEqual(target.metadata[record.parent], (0, 0, stat.S_IFDIR | 0o600))
            self.assertTrue((root / "Applications/Safari.app").is_dir())
            self.assertTrue((root / "Applications/Utilities/System Information.app").is_symlink())
            self.assertFalse((root / "mnt").exists())


if __name__ == "__main__":
    unittest.main()
