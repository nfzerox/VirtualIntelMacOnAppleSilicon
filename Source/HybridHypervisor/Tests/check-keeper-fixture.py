#!/usr/bin/env python3
"""Check retained --kernel fixture logs and observed exit status; never execute a VM.

check-keeper-fixture.py STDOUT STDERR EXIT_CODE [--require-pool-reclaim]
check-keeper-fixture.py --self-test
"""
import argparse
from pathlib import Path
import re


def qualify(stdout, stderr, exit_code, require_pool=False):
    if exit_code != 0 or 'keeper fixture done, 0 functional failures; host counter gates still required' not in stdout:
        raise ValueError('fixture did not complete with zero functional failures/exit0')
    for phase in ('PASS keeper remap/NX/U-S/present/A-rearm/neighbor checks',
                  'PASS keeper threshold-burst and physical-SMC functional checks',
                  'PASS keeper table unlink/reuse and multi-root pool-pressure functional checks'):
        if phase not in stdout:
            raise ValueError('missing functional phase: ' + phase)
    matches = re.findall(r'keeper inactive guard \(this vCPU\): enabled (\d+) suppressed (\d+) thresholdFallbacks (\d+) sharedDirs (\d+)', stderr)
    if len(matches) != 1:
        raise ValueError('expected one final standalone-kernel guard report')
    enabled, suppressed, fallback, shared = map(int, matches[0])
    if enabled != 1 or min(suppressed, fallback, shared) <= 0:
        raise ValueError('guard/shared-domain path not demonstrated by nonzero counters')
    if require_pool:
        reclaim = re.findall(r'pool-reclaims (\d+)', stderr)
        if len(reclaim) != 1 or int(reclaim[0]) <= 0:
            raise ValueError('low-pool run did not demonstrate reclamation')
    return {'coverage': 'standalone one-vCPU inactive user-domain keeper; not SMP qualification',
            'suppressed': suppressed, 'threshold_fallbacks': fallback, 'shared_directories': shared,
            'pool_reclamation_required': require_pool}


def self_test():
    out = ('PASS keeper remap/NX/U-S/present/A-rearm/neighbor checks\n'
           'PASS keeper threshold-burst and physical-SMC functional checks\n'
           'PASS keeper table unlink/reuse and multi-root pool-pressure functional checks\n'
           'keeper fixture done, 0 functional failures; host counter gates still required\n')
    err = 'pool-reclaims 2\nkeeper inactive guard (this vCPU): enabled 1 suppressed 64 thresholdFallbacks 1 sharedDirs 1\n'
    assert qualify(out, err, 0, True)['suppressed'] == 64
    for bad_out, bad_err, code in ((out, err, 1), (out.replace('0 functional', '1 functional'), err, 0),
                                   (out, '', 0), (out, err.replace('enabled 1', 'enabled 0'), 0),
                                   (out, err.replace('suppressed 64', 'suppressed 0'), 0),
                                   (out, err.replace('thresholdFallbacks 1', 'thresholdFallbacks 0'), 0),
                                   (out, err.replace('sharedDirs 1', 'sharedDirs 0'), 0),
                                   (out, err.replace('pool-reclaims 2', 'pool-reclaims 0'), 0)):
        try: qualify(bad_out, bad_err, code, True)
        except ValueError: pass
        else: raise AssertionError('accepted missing runtime coverage')
    print('PASS retained-log qualifier: functional/exit/state/suppression/fallback/shared-domain/pool gates')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('logs', nargs='*')
    parser.add_argument('--require-pool-reclaim', action='store_true')
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        self_test()
    else:
        if len(args.logs) != 3:
            parser.error('expected STDOUT STDERR EXIT_CODE')
        paths = [Path(name) for name in args.logs[:2]]
        if any(p.stat().st_size > 1048576 for p in paths):
            parser.error('fixture log exceeds 1MiB budget')
        try:
            result = qualify(*(p.read_text() for p in paths), int(args.logs[2]), args.require_pool_reclaim)
        except ValueError as error:
            parser.exit(1, 'INCONCLUSIVE ' + str(error) + '\n')
        print('PASS targeted fixture gates:', result)
