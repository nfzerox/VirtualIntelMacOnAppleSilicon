"""Scope Tiger's intentionally optional Rosetta imports, never weaken hosts."""
import sys
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

if __name__=='__main__':unittest.main()
