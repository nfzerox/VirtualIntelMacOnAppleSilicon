from pathlib import Path
import ctypes
import struct
import subprocess
import sys
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[1]


@unittest.skipUnless(sys.platform == 'darwin', 'Native Mach-O preparation')
class PeriodKextcacheTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.work.cleanup)
        cls.root = Path(cls.work.name)
        cls.probe = cls.root / 'probe'
        subprocess.run(('xcrun', 'clang', '-fobjc-arc', '-Wall', '-Wextra', '-Werror',
                        '-framework', 'Foundation', SOURCE / 'probes/host-install-period-kextcache.m',
                        SOURCE / 'tools/HostInstall/VIMPeriodKextcache.m', '-o', cls.probe), check=True)

    def macho(self, name=b'/usr/lib/libdz.dylib', weak=True, cpu=0x01000007):
        payload = name + b'\0'
        length = (24 + len(payload) + 7) & ~7
        command = struct.pack('<6I', 0x80000018 if weak else 0xc, length, 24, 0, 0, 0)
        command += payload.ljust(length - 24, b'\0')
        return struct.pack('<8I', 0xfeedfacf, cpu, 3, 2, 1, len(command), 0, 0) + command

    def invoke(self, data):
        path = self.root / 'private-copy'
        path.write_bytes(data)
        result = subprocess.run((self.probe, path), capture_output=True)
        return result, path.read_bytes()

    def test_redirects_only_dependency_name_without_changing_instructions(self):
        original = self.macho() + b'original instruction bytes'
        result, prepared = self.invoke(original)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, b'redirected\n')
        self.assertEqual(len(prepared), len(original))
        self.assertEqual(prepared[:56], original[:56])
        self.assertIn(b'@loader_path/dz.dylib\0', prepared)
        self.assertTrue(prepared.endswith(b'original instruction bytes'))

    def test_other_libraries_strong_dependencies_and_architectures_unchanged(self):
        for data in (self.macho(b'/usr/lib/libSystem.B.dylib'), self.macho(weak=False),
                     self.macho(cpu=0x0100000c)):
            result, prepared = self.invoke(data)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, b'unchanged\n')
            self.assertEqual(prepared, data)

    def test_fat_binary_changes_only_x86_slice(self):
        arm, intel = self.macho(cpu=0x0100000c), self.macho()
        header = struct.pack('>2I', 0xcafebabe, 2)
        header += struct.pack('>5I', 0x0100000c, 0, 48, len(arm), 0)
        header += struct.pack('>5I', 0x01000007, 3, 48+len(arm), len(intel), 0)
        result, prepared = self.invoke(header + arm + intel)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(prepared[:48+len(arm)], header+arm)
        self.assertIn(b'@loader_path/dz.dylib\0', prepared[48+len(arm):])

    def test_incomplete_tables_fail_without_writing(self):
        malformed = [b'short', self.macho()[:40], struct.pack('>2I', 0xcafebabe, 100)]
        bad = bytearray(self.macho());struct.pack_into('<I', bad, 36, 0)
        malformed.append(bytes(bad))
        for data in malformed:
            result, prepared = self.invoke(data)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(prepared, data)

    def test_volume_root_matches_iokits_physical_bundle_prefix(self):
        target = self.root / 'Target'
        target.mkdir(exist_ok=True)
        alias = self.root / 'VolumeAlias'
        alias.symlink_to(target)
        for path in (alias, target):
            result = subprocess.run((self.probe, '--volume-root', path),
                                    capture_output=True, text=True, check=True)
            self.assertEqual(result.stdout.strip(), str(target.resolve()))
        result = subprocess.run((self.probe, '--volume-root', self.root/'Missing'),
                                capture_output=True)
        self.assertNotEqual(result.returncode, 0)

    def test_offline_service_uses_apple_absent_service_semantics(self):
        library = self.root / 'dz.dylib'
        subprocess.run(('xcrun', 'clang', '-dynamiclib', '-Wall', '-Wextra', '-Werror',
                        SOURCE / 'tools/HostInstall/offline-kext-notifications.c', '-o', library), check=True)
        offline = ctypes.CDLL(str(library))
        offline.dz_notify_kextcache_update.argtypes = [ctypes.c_char_p]
        offline.dz_notify_kextcache_update.restype = None
        offline.dz_check_policy_kext_load.argtypes = [ctypes.c_char_p]
        offline.dz_check_policy_kext_load.restype = ctypes.c_uint64
        offline.dz_notify_kextcache_update(b'/offline/guest/Test.kext')
        self.assertEqual(offline.dz_check_policy_kext_load(b'/offline/guest/Test.kext'), 0)


if __name__ == '__main__':
    unittest.main()
