#!/usr/bin/env python3
"""Architecture assembly and legacy facade migration, without guest writes."""
import pathlib
import runpy
import struct
import sys
import unittest

scripts = pathlib.Path(__file__).resolve().parents[1]/'scripts'
sys.path.insert(0, str(scripts))
gl = runpy.run_path(str(scripts/'stage-opengl-universal.py'))
native = gl['native32']


class Universal(unittest.TestCase):
    def test_roundtrip_architectures_and_bytes(self):
        parts = [(7, 3, 12, b'native32'), (0x1000007, 3, 12, b'donor64')]
        self.assertEqual(gl['slices'](gl['combine'](parts)), parts)

    def test_bad_slice_bounds(self):
        data = gl['combine']([(7, 3, 12, b'native')])
        with self.assertRaises(ValueError):
            gl['slices'](data[:-1])

    def test_restore_old_private_entry_identity(self):
        name = native['OLD_PRIVATE']
        size = (24+len(name)+1+3)&~3
        command = struct.pack('<IIIIII', 13, size, 24, 0, 0, 0)+name+b'\0'*(size-24-len(name))
        data = struct.pack('<IIIIIII', 0xfeedface, 7, 3, 6, 1, size, 0)+command+bytes(128)
        restored = native['prepare'](data, native['STOCK'])
        self.assertEqual(len(data), len(restored))
        self.assertIn(native['STOCK']+b'\0', restored)
        self.assertNotIn(native['OLD_PRIVATE']+b'\0', restored)


if __name__ == '__main__':
    unittest.main()
