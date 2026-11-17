import json
from pathlib import Path
import subprocess
import sys
import unittest
from unittest.mock import call, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import host_install_hfs as installer


class DiskExportTests(unittest.TestCase):
    @patch.object(installer, "check_output")
    @patch.object(installer, "run")
    def test_conversion_requires_independent_qcow2_and_read_only_check(self, run, inspect):
        inspect.return_value = json.dumps({"format": "qcow2", "virtual-size": 4096})
        installer.convert_staged_disk("qemu-img", "raw", "qcow2", 4096)
        self.assertEqual(run.call_args_list, [call("qemu-img", "convert", "-f", "raw", "-O", "qcow2", "raw", "qcow2"),
                                              call("qemu-img", "check", "-q", "-f", "qcow2", "qcow2")])
        inspect.assert_called_once_with("qemu-img", "info", "--output=json", "qcow2")

    @patch.object(installer, "check_output")
    @patch.object(installer, "run")
    def test_disk_with_parent_is_not_publishable(self, run, inspect):
        inspect.return_value = json.dumps({"format": "qcow2", "virtual-size": 4096, "backing-filename": "parent"})
        with self.assertRaisesRegex(ValueError, "not standalone"):
            installer.convert_staged_disk("qemu-img", "raw", "qcow2", 4096)
        self.assertEqual(run.call_count, 1)

    @patch.object(installer, "check_output")
    @patch.object(installer, "run")
    def test_corrupt_export_cannot_complete_conversion(self, run, inspect):
        inspect.return_value = json.dumps({"format": "qcow2", "virtual-size": 4096})
        run.side_effect = [None, subprocess.CalledProcessError(2, "qemu-img check")]
        with self.assertRaises(subprocess.CalledProcessError):
            installer.convert_staged_disk("qemu-img", "raw", "qcow2", 4096)


if __name__ == "__main__":
    unittest.main()
