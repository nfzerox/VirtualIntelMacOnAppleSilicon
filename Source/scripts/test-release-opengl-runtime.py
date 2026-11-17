#!/usr/bin/env python3
"""Read-only packaging checks using an app's actual OpenGL guest inputs.

Does not launch a VM, mutate the app, or turn a research build into a release.
Negative cases alter private copies of the app's packaging receipt, not the
app itself. They are test fixtures, not replacement production build evidence.
"""
import argparse
import copy
from pathlib import Path
import plistlib
import runpy
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path, required=True)
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[1]
    verifier = runpy.run_path(str(source/'scripts/verify-release-build.py'))
    verify = verifier['verify_opengl_runtime']
    runtime = args.app/'Contents/Resources/Runtime'
    guests = ('leopard', 'snowleopard', 'lion', 'mountainlion', 'mavericks',
              'yosemite', 'elcapitan', 'sierra', 'highsierra')
    # The normal packager now records each profile's mutable VARS and the one
    # Common EFI/CODE pair. Do not reconstruct an obsolete per-guest EFI copy
    # or invent missing Firmware fields merely to make the fixture pass.
    fixture = plistlib.loads((runtime/'Packaging.plist').read_bytes())
    if fixture.get('UnifiedEFI'):
        verifier['verify_unified_firmware'](runtime, source, fixture)
    verify(runtime, source, fixture)
    print('PASS actual nine-guest inputs, x64/i386 slices, minimum OS, signatures, linkage and quiet firmware')
    cases = []
    for guest in guests:
        missing = copy.deepcopy(fixture); del missing['OpenGLGuests'][guest]
        cases.append(('missing '+guest, missing, 'Release requires OpenGL guest support'))
    extra = copy.deepcopy(fixture); extra['OpenGLGuests']['unknown'] = {}
    cases.append(('unexpected guest', extra, 'Release requires OpenGL guest support'))
    host = copy.deepcopy(fixture); host['OpenGLGuests']['lion']['HostExecutorPackagedSHA256'] = '0'*64
    cases.append(('mismatched host executor', host, 'OpenGL host executor checksum mismatch: lion'))
    firmware = copy.deepcopy(fixture); firmware['OpenGLGuests']['lion']['EFIImageSHA256'] = '0'*64
    cases.append(('mismatched EFI', firmware, 'Guest firmware checksum or quiet CODE mismatch: lion'))
    for name, candidate, expected in cases:
        try:
            verify(runtime, source, candidate)
        except RuntimeError as error:
            if expected not in str(error):
                raise RuntimeError('Unrelated failure cannot qualify rejection of '+name) from error
            print('PASS rejects '+name)
        else:
            raise RuntimeError('Accepted '+name)
    if (runtime/'ResearchRuntime.plist').exists():
        rejected = subprocess.run([sys.executable, str(source/'scripts/verify-release-build.py'),
            '--app', str(args.app), '--source', str(source), '--configuration', 'Release'],
            capture_output=True, text=True)
        if not rejected.returncode or 'research-only app cannot be packaged as a release' not in rejected.stderr:
            raise RuntimeError('The complete verifier did not explicitly reject research packaging')
        print('PASS complete release verifier refuses research packaging')


if __name__ == '__main__':
    main()
