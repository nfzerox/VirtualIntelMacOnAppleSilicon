"""Real pax/bzip2 extraction and shared-offset progress; no OS installation."""
import bz2
import gzip
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import time
import unittest

SOURCE = Path(__file__).resolve().parents[1]


@unittest.skipUnless(sys.platform == 'darwin', 'uses the macOS pax implementation')
class CPIOPayloadTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory(prefix='vim-cpio-progress-')
        cls.addClassCleanup(cls.work.cleanup)
        cls.folder = Path(cls.work.name)
        cls.probe = cls.folder/'probe'
        subprocess.run(['xcrun', 'clang', '-arch', 'arm64', '-arch', 'x86_64',
                        '-fobjc-arc', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-mmacosx-version-min=11.0', '-Werror=unguarded-availability-new',
                        SOURCE/'probes/host-install-cpio-progress.m',
                        SOURCE/'tools/HostInstall/VIMCPIOPayload.m',
                        '-framework', 'Foundation', '-o', cls.probe], check=True)
        files = cls.folder/'files'
        files.mkdir()
        (files/'folder').mkdir(mode=0o751)
        cls.content = os.urandom(2*1024*1024)
        original = files/'folder/Contents'
        original.write_bytes(cls.content)
        original.chmod(0o640)
        os.link(original, files/'hardlink')
        os.symlink('folder/Contents', files/'symlink')
        cls.raw = subprocess.check_output(['/bin/pax', '-w', '-x', 'cpio', '.'], cwd=files)

    def command(self, *arguments):
        prefix = ['/usr/bin/arch', '-x86_64'] if os.environ.get('VIM_CPIO_PROBE_X86_64') == '1' else []
        return [*prefix, self.probe, *arguments]

    def extract(self, data, *, cancel=None, payload_link=False, missing_root=False):
        work = tempfile.TemporaryDirectory(dir=self.folder)
        self.addCleanup(work.cleanup)
        folder = Path(work.name)
        payload, target = folder/'Payload', folder/'Target'
        payload.write_bytes(data)
        if not missing_root:
            target.mkdir()
        if payload_link:
            link = folder/'Payload Link'
            link.symlink_to(payload.name)
            payload = link
        arguments = self.command(payload, target)
        if cancel is not None:
            arguments.append(str(cancel))
        completed = subprocess.run(arguments, capture_output=True, text=True, timeout=20, check=True)
        return json.loads(completed.stdout), target

    def test_plain_gzip_and_bzip2_keep_contents_modes_and_links(self):
        for data in (self.raw, gzip.compress(self.raw, mtime=0), bz2.compress(self.raw)):
            with self.subTest(signature=data[:6]):
                result, target = self.extract(data)
                self.assertTrue(result['Success'], result)
                observations = result['Progress']
                self.assertEqual(observations[0], 0)
                self.assertGreater(max(observations), 0)
                self.assertEqual(observations, sorted(observations))
                self.assertLessEqual(max(observations), len(data))
                self.assertEqual((target/'folder/Contents').read_bytes(), self.content)
                self.assertEqual((target/'hardlink').stat().st_ino, (target/'folder/Contents').stat().st_ino)
                self.assertEqual(os.readlink(target/'symlink'), 'folder/Contents')
                self.assertEqual(stat.S_IMODE((target/'folder/Contents').stat().st_mode), 0o640)
                self.assertEqual(stat.S_IMODE((target/'folder').stat().st_mode), 0o751)

    def test_corrupt_bzip2_is_an_actual_extraction_failure(self):
        data = bytearray(bz2.compress(self.raw))
        data[len(data)//2] ^= 0x80
        result, _ = self.extract(data)
        self.assertFalse(result['Success'])
        self.assertNotEqual(result['ErrorCode'], 0)

    def test_unknown_format_is_not_reported_as_success(self):
        result, _ = self.extract(b'not an installer payload')
        self.assertFalse(result['Success'])

    def test_readable_payload_link_keeps_the_previous_path_behavior(self):
        result, target = self.extract(bz2.compress(self.raw), payload_link=True)
        self.assertTrue(result['Success'], result)
        self.assertEqual((target/'hardlink').read_bytes(), self.content)

    def test_failed_consumer_launch_reaps_the_started_decoder(self):
        result, _ = self.extract(bz2.compress(self.raw), missing_root=True)
        self.assertFalse(result['Success'])
        self.assertNotEqual(result['ErrorCode'], 0)

    def test_cancel_before_launch_leaves_target_untouched(self):
        result, target = self.extract(bz2.compress(self.raw), cancel=0)
        self.assertFalse(result['Success'])
        self.assertEqual(result['ErrorDomain'], 'NSCocoaErrorDomain')
        self.assertEqual(result['ErrorCode'], 3072)  # NSUserCancelledError
        self.assertEqual(list(target.iterdir()), [])

    def test_cancel_after_input_consumption_reaps_the_pipeline(self):
        result, _ = self.extract(bz2.compress(self.raw), cancel=1)
        self.assertFalse(result['Success'])
        self.assertEqual(result['ErrorCode'], 3072)
        self.assertGreater(max(result['Progress']), 0)

    @unittest.skipUnless(os.environ.get('VIM_CPIO_PROGRESS_SCRATCH'), 'opt-in larger real extraction on scratch storage')
    def test_intermediate_progress_during_real_bzip2_extraction(self):
        with tempfile.TemporaryDirectory(dir=os.environ['VIM_CPIO_PROGRESS_SCRATCH']) as temporary:
            folder = Path(temporary)
            source, target = folder/'Files', folder/'Target'
            source.mkdir();target.mkdir()
            content = os.urandom(64*1024*1024)
            (source/'Blob').write_bytes(content)
            archive = subprocess.check_output(['/bin/pax', '-w', '-x', 'cpio', '.'], cwd=source)
            payload = folder/'Payload'
            payload.write_bytes(bz2.compress(archive))
            started = time.monotonic()
            completed = subprocess.run(self.command(payload, target), capture_output=True,
                                       text=True, check=True, timeout=60)
            elapsed = time.monotonic()-started
            result = json.loads(completed.stdout)
            self.assertTrue(result['Success'], result)
            self.assertEqual((target/'Blob').read_bytes(), content)
            intermediate = [n for n in result['Progress'] if 0 < n < payload.stat().st_size]
            print(json.dumps({'ComponentOnly': True, 'EncodedBytes': payload.stat().st_size,
                              'ExtractSeconds': elapsed, 'Progress': result['Progress']}), flush=True)
            if not intermediate:
                self.skipTest('Extraction finished before the500-ms observation cadence; not a speed failure')
            self.assertEqual(intermediate, sorted(intermediate))


if __name__ == '__main__':
    unittest.main()
