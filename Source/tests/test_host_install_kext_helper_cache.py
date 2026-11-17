from pathlib import Path
import gzip
import plistlib
import subprocess
import sys
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[1]


@unittest.skipUnless(sys.platform == 'darwin', 'Native production metadata builder')
class KextHelperCacheTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.work.cleanup)
        cls.probe = Path(cls.work.name) / 'probe'
        subprocess.run(('xcrun', 'clang', '-fobjc-arc', '-Wall', '-Wextra', '-Werror',
                        '-framework', 'Foundation', '-lz',
                        SOURCE / 'probes/host-install-kext-helper-cache.m',
                        SOURCE / 'tools/HostInstall/VIMKextHelperCache.m', '-o', cls.probe), check=True)

    def build(self, root, architecture='x86_64', local=True):
        args = (self.probe, root, architecture) + (() if local else ('--system-only',))
        data = subprocess.check_output(args)
        decoded = gzip.decompress(data)
        self.assertTrue(decoded.startswith(b'bplist00'))
        return plistlib.loads(decoded)

    def bundle(self, root, path, **extra):
        info = {'CFBundleIdentifier': 'test.' + Path(path).stem, 'CFBundleVersion': '1.2'}
        info.update(extra)
        destination = root / path / 'Contents/Info.plist'
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(plistlib.dumps(info))
        return destination

    def test_empty_cache_is_present_even_when_no_helpers_exist(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.bundle(root, 'System/Library/Extensions/NoHelper.kext')
            self.assertEqual(self.build(root), [])

    def test_original_data_guest_paths_plugins_and_architecture_override(self):
        with tempfile.TemporaryDirectory(prefix='kext metadata ') as temporary:
            root = Path(temporary)
            outer = 'System/Library/Extensions/Parent.kext'
            child = outer + '/Contents/PlugIns/Child.kext'
            local = 'Library/Extensions/Local.kext'
            self.bundle(root, outer, OSBundleHelper={'Executable': 'original'})
            self.bundle(root, child, OSBundleHelper='generic', OSBundleHelper_x86_64='intel')
            self.bundle(root, local, OSBundleHelper=['one', 'two'])
            before = {p.relative_to(root): p.read_bytes() for p in root.rglob('*') if p.is_file()}
            rows = {row['OSBundlePath']: row for row in self.build(root)}
            self.assertEqual(set(rows), {'/' + p for p in (outer, child, local)})
            self.assertEqual(rows['/' + outer]['Data'], {'Executable': 'original'})
            self.assertEqual(rows['/' + child]['Data'], 'intel')
            self.assertEqual(rows['/' + local]['Data'], ['one', 'two'])
            self.assertEqual(rows['/' + child]['CFBundleIdentifier'], 'test.Child')
            self.assertEqual(rows['/' + child]['CFBundleVersion'], '1.2')
            old = {row['OSBundlePath']: row for row in self.build(root, 'i386')}
            self.assertEqual(old['/' + child]['Data'], 'generic')
            after = {p.relative_to(root): p.read_bytes() for p in root.rglob('*') if p.is_file()}
            self.assertEqual(after, before)

    def test_pre_mavericks_uses_only_system_repository(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.bundle(root, 'Library/Extensions/Local.kext', OSBundleHelper='local')
            self.assertEqual(self.build(root, local=False), [])
            system = 'System/Library/Extensions/System.kext'
            self.bundle(root, system, OSBundleHelper='system')
            self.assertEqual([row['OSBundlePath'] for row in self.build(root, local=False)], ['/' + system])


if __name__ == '__main__':
    unittest.main()
