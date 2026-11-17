#!/usr/bin/env python3
"""Check the --atomics variant's completed two-CPU Engine run.

Usage: check-pte-atomic-fixture.py STDOUT STDERR OBSERVED_EXIT_CODE
Run with ENGINE_SHARE=1 ENGINE_STATS=1; the normal same64 optimization stays off.
"""
import json
from pathlib import Path
import runpy
import sys


def main():
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    base = runpy.run_path(str(Path(__file__).with_name('check-pte-same64-fixture.py')))
    stdout, stderr = (base['read_log'](p) for p in sys.argv[1:3])
    marker = 'PTE_ATOMIC_FORMS widths=32,64 cas_success=1 cas_failure=1 swap=1 ad_walk_rounds=128 failures=0\n'
    if not stdout.endswith(marker) or stdout.count('PTE_ATOMIC_FORMS') != 1:
        raise SystemExit('Missing completed atomic fixture record')
    result = base['qualify'](stdout[:-len(marker)], stderr, int(sys.argv[3]), 2, 0)
    result['result'] = 'PTE_ATOMIC_FORMS_TARGETED_PASS'
    result['scope'] = ('32/64-bit CAS, exchange and LSE; peer mapping/permission/SMC checks; '
                       'real A/D walker attempts, not forced interleaving or speed proof')
    result['phase_writes'] = 'logical phase updates, not primitive atomic instruction counts'
    print(json.dumps(result, sort_keys=True))


if __name__ == '__main__':
    main()
