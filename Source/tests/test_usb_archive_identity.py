"""Keep all backend code in the compatibility identity, not ar timestamps."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location('usb_builder', Path(__file__).resolve().parents[1]/'scripts/build-usb-helper.py')
BUILDER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUILDER)


def archive(payload, date='1', owner='501'):
    header = (f'{"code.o/":<16}{date:<12}{owner:<6}{"20":<6}{"100644":<8}{len(payload):<10}`\n').encode()
    return b'!<arch>\n'+header+payload+(b'\n' if len(payload)%2 else b'')


class ArchiveIdentity(unittest.TestCase):
    def test_metadata_changes_do_not_require_helper_upgrade(self):
        with tempfile.TemporaryDirectory() as directory:
            paths = [Path(directory)/str(i) for i in range(3)]
            for path, data in zip(paths, [archive(b'code'), archive(b'code','999','502'), archive(b'changed')]):
                path.write_bytes(data)
            self.assertEqual(BUILDER.static_archive_hash(paths[0]), BUILDER.static_archive_hash(paths[1]))
            self.assertNotEqual(BUILDER.static_archive_hash(paths[0]), BUILDER.static_archive_hash(paths[2]))

    def test_truncation_is_not_accepted_as_compatible(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'bad.a'
            for data in (b'', archive(b'odd')[:-1], archive(b'code')[:-2]):
                path.write_bytes(data)
                with self.assertRaises(RuntimeError):
                    BUILDER.static_archive_hash(path)


if __name__ == '__main__':
    unittest.main()
