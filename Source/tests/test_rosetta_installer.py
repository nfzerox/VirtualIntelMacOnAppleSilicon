"""The install trigger builds without running Intel code or changing Rosetta.

The explicit VIM_TEST_ROSETTA_LAUNCH=1 check additionally exercises NSWorkspace
only after /usr/bin/true proves translation already works on this test host.
"""
from pathlib import Path
import os
import platform
import plistlib
import runpy
import shutil
import subprocess
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[1]


@unittest.skipUnless(platform.system() == 'Darwin', 'requires the macOS SDK')
class RosettaInstallerTests(unittest.TestCase):
    @unittest.skipUnless(platform.machine() == 'arm64', 'Rosetta detection is ARM-host-only')
    def test_real_preflight_missing_and_broken(self):
        with tempfile.TemporaryDirectory(prefix='vim-rosetta-preflight-') as temporary:
            app = Path(temporary)/'Preflight.app'
            binary = app/'Contents/MacOS/Preflight'
            helper = app/'Contents/Resources/Runtime/bin/apple-gfx-helper'
            binary.parent.mkdir(parents=True)
            helper.parent.mkdir(parents=True)
            with (app/'Contents/Info.plist').open('wb') as stream:
                plistlib.dump({'CFBundleIdentifier': 'com.mac.virtualintelmac.preflight-test',
                              'CFBundleExecutable': 'Preflight', 'CFBundlePackageType': 'APPL'}, stream)
            subprocess.run(['xcrun', 'clang', '-arch', 'arm64', '-fobjc-arc', '-O2',
                            '-mmacosx-version-min=11.0', '-I', str(SOURCE/'VirtualIntelMac'),
                            str(SOURCE/'probes/rosetta-preflight.m'),
                            str(SOURCE/'VirtualIntelMac/VIMHostSupport.m'),
                            str(SOURCE/'VirtualIntelMac/VIMAlerts.m'),
                            '-framework', 'AppKit', '-o', str(binary)], check=True)
            subprocess.run(['codesign', '--force', '--sign', '-', binary], check=True)
            # Same finite helper returns64. ARM-only is a real EBADARCH with
            # an Intel binpref, even on a host where Rosetta is installed.
            shutil.copy2(binary, helper)
            subprocess.run([binary, 'missing'], check=True, timeout=15)
            helper.unlink()
            subprocess.run([binary, 'broken'], check=True, timeout=15)

    def test_package_and_optional_launch(self):
        with tempfile.TemporaryDirectory(prefix='vim-rosetta-installer-') as temporary:
            app = Path(temporary)/'Virtual Intel Mac.app'
            (app/'Contents').mkdir(parents=True)
            with (app/'Contents/Info.plist').open('wb') as stream:
                plistlib.dump({'CFBundleIdentifier': 'com.mac.virtualintelmac.test'}, stream)
            bundle = runpy.run_path(str(SOURCE/'scripts/build-rosetta-installer.py'))['build'](SOURCE, app, '-')
            binary = bundle/'Contents/MacOS/VIMRosettaInstaller'
            info = plistlib.loads((bundle/'Contents/Info.plist').read_bytes())
            self.assertEqual(info['LSMinimumSystemVersion'], '11.0')
            self.assertTrue(info['LSUIElement'])
            self.assertEqual(subprocess.check_output(['lipo', '-archs', binary], text=True).strip(), 'x86_64')
            self.assertIn('minos 11.0', subprocess.check_output(['xcrun', 'vtool', '-show-build', binary], text=True))
            subprocess.run(['codesign', '--verify', '--strict', bundle], check=True)
            if os.environ.get('VIM_TEST_ROSETTA_LAUNCH') == '1':
                # No launch attempt when Rosetta is absent; this fixture must
                # not surprise the user with an installation/license prompt.
                ready = subprocess.run(['arch', '-x86_64', '/usr/bin/true'], capture_output=True)
                if ready.returncode:
                    self.skipTest('Intel execution is unavailable; do not install Rosetta for a test')
                probe = Path(temporary)/'launch-check'
                subprocess.run(['xcrun', 'clang', '-O2', '-fobjc-arc', '-mmacosx-version-min=11.0',
                                '-Wall', '-Wextra', '-Werror', '-Werror=unguarded-availability-new',
                                str(SOURCE/'probes/rosetta-installer-launch.m'), '-framework', 'AppKit',
                                '-o', str(probe)], check=True)
                subprocess.run([probe, bundle], check=True, timeout=20)


if __name__ == '__main__':
    unittest.main()
