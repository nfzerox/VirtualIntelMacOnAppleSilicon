#!/usr/bin/env python3
"""Audit a genuine-SDK Tiger candidate against explicit native symbol inputs.

Read-only. This checks relocatable format and symbol availability, not loading,
C++ vtable compatibility, graphics correctness or runtime acceptance. Never use
this report alone to turn on acceleration in the production app.
"""
import argparse
import hashlib
import json
from pathlib import Path
import runpy
import subprocess


def symbols(path, mode):
    if not path.is_file() or not 0 < path.stat().st_size <= 32*1024*1024:
        raise ValueError('Missing or oversized native input: '+str(path))
    text = subprocess.check_output(['nm', '-arch', 'i386', mode, str(path)], text=True)
    return {line.split()[-1] for line in text.splitlines() if line.split()}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--kernel', type=Path, required=True)
    p.add_argument('--graphics', type=Path, required=True)
    p.add_argument('--pci', type=Path, required=True)
    p.add_argument('--candidate', type=Path, required=True)
    a = p.parse_args()
    verifier = runpy.run_path(str(Path(__file__).resolve().parents[1]/'scripts/build-opengl-kernel.py'))['verify_period_object']
    native = {'Kernel': a.kernel, 'IOGraphicsFamily': a.graphics, 'IOPCIFamily': a.pci}
    exports = set()
    for path in native.values():
        exports |= symbols(path, '-gU')
    names = ('VMsvga2', 'VMsvga2Accel')
    paths = {name: a.candidate/(name+'.kext')/'Contents/MacOS'/name for name in names}
    rows = {}
    for name, path in paths.items():
        verifier(path, '10.4')
        # Only the accelerator declares a dependency on our framebuffer.
        available = exports | (symbols(paths['VMsvga2'], '-gU') if name == 'VMsvga2Accel' else set())
        rows[name] = {'SHA256': hashlib.sha256(path.read_bytes()).hexdigest(),
                      'MissingNativeSymbols': sorted(symbols(path, '-u')-available)}
    report = {'FormatVersion': 1, 'Architecture': 'i386', 'SDKVersion': '10.4',
              'NativeInputs': {name: hashlib.sha256(path.read_bytes()).hexdigest() for name, path in native.items()},
              'Candidates': rows, 'RuntimeQualified': False}
    print(json.dumps(report, indent=2, sort_keys=True))
    return int(any(row['MissingNativeSymbols'] for row in rows.values()))


if __name__ == '__main__':
    raise SystemExit(main())
