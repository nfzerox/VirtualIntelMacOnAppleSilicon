import io
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import host_install_bigsur_remap as remap


MTREE = (
    b"/set type=file xattrsdigest=none.0\n"
    b". type=dir inode=2 siblingid=0 time=123.001 btime=123.001 ctime=123.001 ptime=123.001\n"
    b"original inode=41 siblingid=0 xattrsdigest=hash.42\n..\n"
)


class BigSurRemapTests(unittest.TestCase):
    def test_uses_disk_inode_and_external_stream_identity(self):
        volume = SimpleNamespace(directory_entries=lambda: [(2, b"original", 72, 8)])
        with patch.object(remap, "system_objects", return_value=({72: [(b"com.apple.ResourceFork", 73)]}, {})):
            result, report = remap.remapping_dictionary(volume, io.BytesIO(MTREE))
        self.assertEqual(result, {"72": 41, "73": 42, "CHANGE": 123001000000})
        self.assertEqual(report["ExternalStreams"], 1)
        self.assertEqual(report["Unresolved"], {})

    def test_audit_reports_missing_stream_but_publish_mode_refuses_it(self):
        volume = SimpleNamespace(directory_entries=lambda: [(2, b"original", 72, 8)])
        with patch.object(remap, "system_objects", return_value=({}, {})):
            _, report = remap.remapping_dictionary(volume, io.BytesIO(MTREE), audit=True)
            self.assertEqual(report["Unresolved"], {"External attribute count 0 instead of 1": 1})
            with self.assertRaisesRegex(ValueError, "External attribute count"):
                remap.remapping_dictionary(volume, io.BytesIO(MTREE))


if __name__ == "__main__":
    unittest.main()
