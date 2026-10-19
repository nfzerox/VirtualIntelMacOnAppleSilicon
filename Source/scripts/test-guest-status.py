#!/usr/bin/env python3
"""Exercise the real compiled, read-only guest probe against temporary volumes."""
import json
from pathlib import Path
import plistlib
import subprocess
import tempfile
import sys

binary=Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='vim-status-test-') as directory:
    root=Path(directory)
    system=root/'System/Library/CoreServices'
    system.mkdir(parents=True)
    setup=root/'private/var/db/.AppleSetupDone'
    setup.parent.mkdir(parents=True)
    def check(version, expected, candidate):
        (system/'SystemVersion.plist').write_bytes(plistlib.dumps(
            {'ProductVersion':version,'ProductBuildVersion':'FixtureBuild'}))
        result=json.loads(subprocess.check_output([binary,'--root',root]))
        assert result['Guest']==expected, result
        assert result['InstalledSystemCandidate'] is candidate, result
        assert result['Ready'] is False and result['LiveRoot'] is False, result
        assert result['KernelMatchesSystem'] is False, result
        return result
    check('10.14.6','mojave',False)
    (system/'Finder.app').mkdir()
    setup.touch()
    for version, guest in [('10.14.1','mojave'),('10.14.99','mojave'),
                           ('10.15.0','catalina'),('10.15.99','catalina'),('11.7.10','bigsur')]:
        check(version,guest,True)
    check('10.13.6','unsupported',False)
    check('27.0','unsupported',False)
    messages=root/'.OSInstallerMessages'
    messages.write_bytes(plistlib.dumps([{'Domain':'com.apple.installer.progresserror',
                                        'Fields':{'com.apple.message.precentage':'-27'}}]))
    for version, guest in [('10.14.6','mojave'),('10.15.7','catalina'),('11.7.10','bigsur')]:
        result=check(version,guest,True)
        assert result['OSInstallerMessagesPresent'] is True and result['InstallerMarkers']==[], result
    marker=root/'macOS Install Data'
    marker.mkdir()
    assert check('11.7.10','bigsur',False)['InstallerMarkers']==['macOS Install Data']
    marker.rmdir()
    recovery=root/'System/Installation/CDIS/macOS Utilities.app'
    recovery.mkdir(parents=True)
    assert check('11.7.10','bigsur',False)['RecoveryToolsPresent'] is True
    (system/'SystemVersion.plist').write_bytes(b'not a plist')
    assert subprocess.run([binary,'--root',root],capture_output=True).returncode==65
assert subprocess.run([binary,'--root','relative'],capture_output=True).returncode==64
print('PASS: version families, leftover installer messages, incomplete install, Recovery, malformed input; never authorizes readiness')
