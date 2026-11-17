import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import host_install_bigsur_system as system


class BigSurSystemTests(unittest.TestCase):
    def test_native_compression_fixups_do_not_replace_other_metadata(self):
        with patch.object(system, "run_archive") as run:
            system.restore_archive_compression("signed asset", Path("/scratch/System"))
        run.assert_called_once_with("signed asset", "AssetData/payloadv2/fixup.manifest",
                                    "check-and-fix", "-d", Path("/scratch/System"),
                                    "-include-type", "f", "-exclude-field", "uid,gid,mod,acl,flg", "-v", authorize=True)

    def test_expected_comparison_status_does_not_accept_a_crash(self):
        command = ("aa", "check-and-fix")
        with patch.object(system, "run_archive", side_effect=system.subprocess.CalledProcessError(1, command)):
            self.assertEqual(system.restore_archive_compression(None, Path("/scratch/System")), 1)
        with patch.object(system, "run_archive", side_effect=system.subprocess.CalledProcessError(-9, command)):
            with self.assertRaises(system.subprocess.CalledProcessError):
                system.restore_archive_compression(None, Path("/scratch/System"))

    def test_original_stubs_supply_metadata_and_match_the_firmlink_text(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / system.FIRMLINKS).parent.mkdir(parents=True)
            mapping = {"Applications": "Applications", "Library": "Library", "Users": "Users",
                       "private": "private", "System/Library/Caches": "Other/Caches"}
            (root / system.FIRMLINKS).write_text("/AppleInternal\tAppleInternal\n" + "".join(
                f"/{source}\t{dest}\n" for source, dest in mapping.items()))
            rows = [{"PAT": source, "TYP": "D", "UID": 0, "GID": 80, "MOD": 0o775,
                     "FLG": system.SF_FIRMLINK} for source in mapping]
            members = {"AssetData/payloadv2/firmlinks_payload": b"archive"}
            archive = SimpleNamespace(read=members.__getitem__)
            with patch.object(system.subprocess, "check_output", return_value=json.dumps(rows)):
                layout = system.firmlink_layout(archive, root)
            self.assertEqual(layout[Path("System/Library/Caches")], (Path("Other/Caches"), 0, 80, 0o775))
            with patch.object(system.subprocess, "check_output", return_value=json.dumps(rows[:-1])):
                with self.assertRaisesRegex(ValueError, "lists disagree"):
                    system.firmlink_layout(archive, root)


if __name__ == "__main__":
    unittest.main()
