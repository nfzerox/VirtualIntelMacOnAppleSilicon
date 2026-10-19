#!/usr/bin/env python3
import importlib.util
import pathlib
import struct
import unittest

path = pathlib.Path(__file__).resolve().parents[1]/'scripts/stage-opengl-native32.py'
spec = importlib.util.spec_from_file_location('native32', path)
native = importlib.util.module_from_spec(spec)
spec.loader.exec_module(native)


def dylib(command, name):
    size = (24+len(name)+1+3)&~3
    return struct.pack('<IIIIII',command,size,24,0,0x10000,0x10000)+name+bytes(size-24-len(name))


def image(commands):
    data = b''.join(commands)
    return struct.pack('<IIIIIII',0xfeedface,7,3,6,len(commands),len(data),0)+data+b'unchanged-code'


class Native32(unittest.TestCase):
    def test_stock_and_private_are_idempotent(self):
        source = image([dylib(0xd,native.STOCK)])
        result = native.prepare(source)
        self.assertEqual(len(result),len(source))
        self.assertTrue(result.endswith(b'unchanged-code'))
        self.assertIn(native.PRIVATE+b'\0',result)
        self.assertEqual(native.prepare(result),result)

    def test_facade_is_not_mistaken_for_native(self):
        source = image([dylib(0xd,native.STOCK),dylib(0x8000001f,native.PRIVATE)])
        with self.assertRaisesRegex(ValueError,'routing facade'):
            native.prepare(source)

    def test_universal_selects_i386_only(self):
        source = image([dylib(0xd,native.STOCK)])
        other = b'not-an-i386-slice'
        header = struct.pack('>II',0xcafebabe,2)
        header += struct.pack('>IIIII',7,3,48,len(source),0)
        header += struct.pack('>IIIII',0x1000007,3,48+len(source),len(other),0)
        self.assertEqual(native.prepare(header+source+other),native.prepare(source))

    def test_unknown_identity_rejected(self):
        with self.assertRaisesRegex(ValueError,'unrecognized OpenGL'):
            native.prepare(image([dylib(0xd,b'/tmp/unrelated.dylib')]))

    def test_truncated_commands_rejected(self):
        source = image([dylib(0xd,native.STOCK)])
        with self.assertRaises(ValueError):
            native.prepare(source[:35])


if __name__ == '__main__':
    unittest.main()
