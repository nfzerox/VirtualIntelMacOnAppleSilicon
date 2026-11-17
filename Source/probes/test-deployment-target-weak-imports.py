"""Scope Tiger's intentionally optional Rosetta imports, never weaken hosts."""
import sys
import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import deployment_target as policy
import tiger_rosetta_imports as tiger

class WeakImports(unittest.TestCase):
    def check(self, symbols, enabled=False, architecture='i386'):
        output=''.join('    '+name+' [weak-import] (from libSystem)\n' for name in symbols)
        with patch.object(policy.subprocess,'check_output',side_effect=[architecture,output]):
            (tiger.check_tiger_rosetta_imports if enabled else policy.check_weak_libsystem_imports)(Path('fixture'))

    def test_explicit_guest_adapter(self):
        self.check(tiger.TIGER_ROSETTA_WEAK,True)

    def test_default_stays_strict(self):
        with self.assertRaises(RuntimeError):self.check(['_strlen'])

    def test_new_function_still_refused(self):
        with self.assertRaises(RuntimeError):self.check(['_strlen','_pipe2'],True)

    def test_other_architecture_refused(self):
        with self.assertRaises(RuntimeError):self.check(['_strlen'],True,'arm64')

class ConfigureTimestamps(unittest.TestCase):
    def test_configure_and_sdk_filter_restore_identical_header(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'config-host.h'
            original=b'#undef CONFIG_DUP3 /* dup3: newer than macOS 11.0 */\n'
            path.write_bytes(original)
            os.utime(path,ns=(1700000000000000000,1700000000000000000))
            previous=path.stat().st_mtime_ns
            with policy.preserve_unchanged_config_timestamp(path):
                path.write_text('#define CONFIG_DUP3 1\n')
                with patch.object(policy,'unavailable_functions',return_value=['dup3']):
                    self.assertEqual(policy.undefine_unavailable(path,'CONFIG_','arm64'),['dup3'])
            self.assertEqual(path.read_bytes(),original)
            self.assertEqual(path.stat().st_mtime_ns,previous)

    def test_actual_header_change_keeps_new_timestamp(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'config-host.h';path.write_text('#define CONFIG_A 1\n')
            os.utime(path,ns=(1700000000000000000,1700000000000000000))
            with policy.preserve_unchanged_config_timestamp(path):
                path.write_text('#define CONFIG_A 0\n')
                os.utime(path,ns=(1700000002000000000,1700000002000000000))
            self.assertEqual(path.stat().st_mtime_ns,1700000002000000000)

    def test_first_configure_keeps_new_header(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'config-host.h'
            with policy.preserve_unchanged_config_timestamp(path):
                path.write_text('#define CONFIG_A 1\n')
            self.assertEqual(path.read_text(),'#define CONFIG_A 1\n')

    def test_failed_configure_does_not_rewind_time(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'config-host.h';path.write_text('#define CONFIG_A 1\n')
            os.utime(path,ns=(1700000000000000000,1700000000000000000))
            with self.assertRaises(RuntimeError):
                with policy.preserve_unchanged_config_timestamp(path):
                    path.write_text('#define CONFIG_A 1\n')
                    os.utime(path,ns=(1700000002000000000,1700000002000000000))
                    raise RuntimeError('configure failed')
            self.assertEqual(path.stat().st_mtime_ns,1700000002000000000)

if __name__=='__main__':unittest.main()
