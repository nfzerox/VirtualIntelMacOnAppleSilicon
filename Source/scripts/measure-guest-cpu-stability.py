#!/usr/bin/env python3
"""Repeat a native OpenSSL SHA-1 workload through the real app owner.

Diagnostic only: no guest files, settings, CPU flags or power state are changed.
The period benchmark takes about 15 seconds per run. With --fixed-mib the
guest instead hashes that many MiB of zeros, with no file I/O and no guest
timer deciding how much work to do. HostSeconds includes adapter overhead.
Keep other workloads constant; these results alone do not attribute a
regression to the CPU engine.
An observation failure stops the series instead of replaying a pending request.
"""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import time


def fixed_work_digest(mib):
    digest = hashlib.sha1()
    zeroes = bytes(1024 * 1024)
    for _ in range(mib):
        digest.update(zeroes)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('diagnostics', type=Path)
    parser.add_argument('output', type=Path, help='new, private diagnostic directory')
    parser.add_argument('--runs', type=int, default=8)
    parser.add_argument('--fixed-mib', type=int,
                        help='hash this many MiB of zeros instead of running OpenSSL speed (1–128)')
    parser.add_argument('--tally', type=Path, help='optional already-enabled engine tally')
    parser.add_argument('--snapshot-pid', type=int,
                        help='optional Hybrid QEMU PID: read counters with LLDB outside each timed run')
    args = parser.parse_args()
    if not 1 <= args.runs <= 32:
        parser.error('--runs must be 1–32')
    if args.fixed_mib is not None and not 1 <= args.fixed_mib <= 128:
        parser.error('--fixed-mib must be 1–128')
    if args.fixed_mib is not None:
        expected_digest = fixed_work_digest(args.fixed_mib)
        workload = ['/bin/sh', '-c',
                    'time -p /bin/dd if=/dev/zero bs=1048576 count="$1" | /usr/bin/openssl sha1',
                    'fixed-sha1', str(args.fixed_mib)]
    else:
        workload = ['/usr/bin/openssl', 'speed', '-elapsed', 'sha1']
    args.output.mkdir(mode=0o700)
    adapter = Path(__file__).with_name('app-guest-command.py')

    def snapshot(label):
        if args.snapshot_pid is None:
            return
        probe = Path(__file__).resolve().parents[1] / 'probes/hybrid_runtime_snapshot.py'
        destination = (args.output / (label + '.counters.json')).resolve()
        command = ['xcrun', 'lldb', '--batch', '-p', str(args.snapshot_pid),
                   '-o', f'command script import "{probe}"',
                   '-o', f'vim-hybrid-snapshot "{destination}"',
                   '-o', 'process detach']
        with (args.output / (label + '.lldb.log')).open('w') as log:
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
        if result.returncode or not destination.is_file():
            raise SystemExit('Counter snapshot failed; inspect the LLDB log before continuing.')

    for number in range(1, args.runs + 1):
        snapshot(f'run-{number:02d}-before')
        started = datetime.datetime.now(datetime.timezone.utc).isoformat()
        begin = time.monotonic()
        result = subprocess.run([sys.executable, str(adapter), str(args.diagnostics), *workload],
                                text=True, capture_output=True)
        record = {'Run': number, 'StartedUTC': started,
                  'HostSeconds': time.monotonic() - begin,
                  'AdapterExitCode': result.returncode}
        try:
            response = json.loads(result.stdout)
        except ValueError:
            response = {'AdapterOutput': result.stdout, 'AdapterError': result.stderr}
        record['Response'] = response
        stdout = response.get('Stdout', '')
        if args.fixed_mib is not None:
            # The digest also catches short input or dd failure despite sh
            # returning the last pipeline command's exit code.
            match = re.fullmatch(r'(?:[^\n]*=\s*)?([0-9a-fA-F]{40})\s*', stdout)
            workload_verified = bool(match and match[1].lower() == expected_digest)
            record.update(FixedMiB=args.fixed_mib, DigestVerified=workload_verified)
        else:
            row = re.search(r'^sha1\s+(.+)$', stdout, re.MULTILINE)
            workload_verified = row is not None
            if row:
                record['SHA1BytesPerSecond'] = [float(value.rstrip('k')) * 1000
                                                 for value in row.group(1).split()]
        destination = args.output / f'run-{number:02d}.json'
        destination.write_text(json.dumps(record, indent=2) + '\n')
        if args.tally and args.tally.is_file():
            (args.output / f'run-{number:02d}.tally').write_bytes(args.tally.read_bytes())
        print(json.dumps({key: record[key] for key in
                          ('Run', 'HostSeconds', 'SHA1BytesPerSecond', 'FixedMiB',
                           'DigestVerified') if key in record}), flush=True)
        if result.returncode or not workload_verified:
            print(f'Stopped; inspect {destination} and the existing app request before retrying.', file=sys.stderr)
            return 1
        snapshot(f'run-{number:02d}-after')
    return 0


if __name__ == '__main__':
    sys.exit(main())
