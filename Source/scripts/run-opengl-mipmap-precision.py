#!/usr/bin/env python3
"""Run the public precision fixture with retained, assertion-bearing results.

Supply a new output directory and the complete command prefix after --.
Use a PUBLIC_ONLY build on guests/references, so no embedded host adapter can
conceal the executor's behavior. A pass with unsupported cases is not full
format support; the summary retains both counts and the exact case inventory.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if not command or args.output.exists():
        parser.error('Supply a probe command and a new output directory')
    args.output.mkdir(parents=True)
    results = []
    for profile, settings, targets in (
            ('compat-lists', ['--lists'], 4),
            ('compat-auto-cube', ['--automatic', '--cube-only'], 1),
            ('core', ['--core'], 7),
            ('core-immutable', ['--core', '--immutable'], 7)):
        for family, flags, formats in (
                ('float', [], 16), ('normalized', ['--normalized'], 8),
                ('byte', ['--byte'], 8), ('depth', ['--depth'], 4)):
            label = profile + '-' + family
            try:
                child = subprocess.run(command + settings + flags,
                                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                       timeout=90)
                output, code = child.stdout.decode('utf-8', errors='replace'), child.returncode
            except subprocess.TimeoutExpired as error:
                output = (error.stdout or b'').decode('utf-8', errors='replace')
                code = 'timeout'
            (args.output / (label + '.log')).write_text(output)
            matches = re.findall(r'^float-mipmap checks=(\d+) failures=(\d+) unsupported=(\d+)$',
                                 output, re.MULTILINE)
            summary = tuple(map(int, matches[0])) if len(matches) == 1 else None
            cases = re.findall(r'^case target=[0-9a-f]+ format=[0-9a-f]+ base=[02] immutable=[01]$',
                               output, re.MULTILINE)
            expected = formats * targets * 2
            if summary and summary[1]:
                status = 'fail'
            elif code == 0 and summary and len(cases) == expected and len(set(cases)) == expected:
                status = ('unsupported' if summary[2] == expected else
                          'pass-with-unsupported' if summary[2] else 'pass')
            else:
                status = 'unsupported' if code == 77 else 'incomplete'
            results.append(dict(test=label, status=status, exit=code, summary=summary,
                                cases=len(cases), expected_cases=expected))
            (args.output / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
            print(f'{label}: {status} exit={code} summary={summary} cases={len(cases)}/{expected}',
                  flush=True)
    return int(any(result['status'] in ('fail', 'incomplete') for result in results))


if __name__ == '__main__':
    raise SystemExit(main())
