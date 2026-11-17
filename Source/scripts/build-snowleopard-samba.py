#!/usr/bin/env python3
"""Rebuild Snow Leopard's signed SMB server payload from bundled sources."""
import argparse
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys
import tarfile
import tempfile

SOURCE = Path(__file__).resolve().parents[1]
VENDOR = SOURCE / 'vendor/source'
GUEST = 'snowleopard'
ARCHIVE = 'samba-235.7-source.tar.gz'
COMMIT = 'b291a85933eba214fcc2eae8fbc9412038891f1d'
POLICY_VERSION = 2
SECURITY_FIXES = ('CVE-2012-0870', 'CVE-2012-1182')
EXECUTABLES = ('snowleopard-smbd', 'snowleopard-odsam.dylib')
FILES = (*EXECUTABLES, 'Samba-COPYING.txt', 'SnowLeopardSMB.plist')


def run(*command, **kwargs):
    subprocess.run(command, check=True, **kwargs)


def verify(output):
    manifest = plistlib.loads((output / 'SnowLeopardSMB.plist').read_bytes())
    if (manifest.get('Guest') != GUEST or manifest.get('PolicyVersion') != POLICY_VERSION or
            manifest.get('SecurityFixes') != list(SECURITY_FIXES)):
        raise ValueError('Missing Snow Leopard SMB policy')
    for name in FILES:
        if not (output / name).is_file():
            raise ValueError('Missing Snow Leopard SMB component: '+name)
    for name in EXECUTABLES:
        binary = output / name
        if subprocess.check_output(['lipo', '-archs', binary], text=True).split() != ['i386']:
            raise ValueError('Expected the native Snow Leopard i386 ABI: '+name)
        if any(marker in binary.read_bytes() for marker in (b'/Users/', b'/Volumes/', b'Built by:', b'Build host:')):
            raise ValueError('Builder information leaked into '+name)
        run('codesign', '--verify', '--strict', binary)


def build(sdk, compiler, output, jobs):
    if output.exists():
        raise ValueError('Use a new output directory')
    with tempfile.TemporaryDirectory(prefix='vim-snow-smb-build-') as temp:
        work = Path(temp)
        for archive in (ARCHIVE, 'CF-550.42-samba-headers.tar.gz'):
            with tarfile.open(VENDOR / archive) as source:
                source.extractall(work, filter='data')
        upstream = work / 'samba-samba-235.7'
        # Retain Apple's FD_SETSIZE hardening and the later Snow Leopard
        # security fixes instead of installing the old 235.6 server unchanged.
        for issue in SECURITY_FIXES:
            security = SOURCE / 'probes/patches' / ('snowleopard-samba-'+issue+'.patch')
            run('git', 'apply', '--check', security, cwd=upstream/'samba')
            run('git', 'apply', security, cwd=upstream/'samba')
        patch = SOURCE / 'probes/patches/snowleopard-samba-spnego-mic.patch'
        run('git', 'apply', '--check', patch, cwd=upstream)
        run('git', 'apply', patch, cwd=upstream)
        compiled = work / 'build'
        run(sys.executable, SOURCE / 'probes/build-leopard-samba.py',
            '--target', GUEST, '--source', upstream, '--sdk', sdk, '--cc', compiler,
            '--cf-source', work / 'CF-550.42', '--output', compiled,
            '--jobs', str(jobs), '--auth-module')
        output.mkdir(parents=True)
        for name, source in zip(EXECUTABLES, ('smbd', 'auth_odsam.dylib')):
            binary = output / name
            shutil.copyfile(compiled / 'bin' / source, binary)
            binary.chmod(0o755)
            run('strip', '-S', binary)
            run('codesign', '--force', '--sign', '-', '--digest-algorithm=sha1,sha256', binary)
        shutil.copyfile(upstream / 'samba/COPYING', output / 'Samba-COPYING.txt')
        (output / 'SnowLeopardSMB.plist').write_bytes(plistlib.dumps(
            {'FormatVersion': 1, 'Guest': GUEST, 'PolicyVersion': POLICY_VERSION,
             'UpstreamCommit': COMMIT, 'SecurityFixes': list(SECURITY_FIXES)}))
        verify(output)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--verify', type=Path)
    for name in ('sdk', 'cc', 'output'):
        parser.add_argument('--'+name, type=Path)
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    if args.verify:
        verify(args.verify)
    elif args.sdk and args.cc and args.output:
        build(args.sdk.resolve(), args.cc.resolve(), args.output.resolve(), args.jobs)
    else:
        parser.error('Use --verify or supply --sdk, --cc and --output')
