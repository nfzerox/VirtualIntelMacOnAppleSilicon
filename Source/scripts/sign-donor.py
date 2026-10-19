#!/usr/bin/env python3
"""Ad-hoc sign a modified donor using its unchanged source's entitlements.

Build-host only; extracted unsigned cache dylibs have no executable entitlement
policy. This does not copy an Apple identity, grant host entitlements to QEMU,
or bypass signing admission. The native guest preparer uses Security.framework
for the same capture-before-edit rule, without requiring Python in the guest.
"""
import argparse
from pathlib import Path
import plistlib
import subprocess
import tempfile


def read_policy(path):
    result = subprocess.run(['codesign', '-d', '--entitlements', ':-', str(path)], capture_output=True)
    if not result.stdout:
        # Cache extraction deliberately yields unsigned dylibs. Other command
        # failures must not silently turn an unknown policy into an empty one.
        if result.returncode and b'not signed at all' not in result.stderr:
            raise ValueError(result.stderr.decode('utf8', 'replace'))
        return {}
    policy = plistlib.loads(result.stdout)
    if not isinstance(policy, dict):
        raise ValueError('Donor entitlements are not a dictionary')
    return policy


def sign(original, target, deep=False):
    policy = read_policy(original)
    with tempfile.TemporaryDirectory(prefix='vim-donor-signing-') as directory:
        args = ['codesign', '-f', '-s', '-', '--preserve-metadata=entitlements']
        if deep:
            args.append('--deep')
        if policy:
            file = Path(directory)/'Entitlements.plist'
            file.write_bytes(plistlib.dumps(policy))
            args += ['--entitlements', str(file)]
        subprocess.run([*args, str(target)], check=True)
    if read_policy(target) != policy:
        raise ValueError('Signed donor did not retain its source entitlements')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('original', type=Path)
    parser.add_argument('target', type=Path)
    parser.add_argument('--deep', action='store_true')
    options = parser.parse_args()
    sign(options.original, options.target, options.deep)
