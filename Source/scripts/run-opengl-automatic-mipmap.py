#!/usr/bin/env python3
"""Isolate each public automatic-mipmap case; retain crashes and missing output.

Example: --output NEW_DIR --dense -- ssh HOST /path/to/automatic-mipmap
The command prefix may include the physical Intel GPU holder and arch selector.
No absent summary, timeout, crash or unsupported case counts as a pass.
"""
import argparse
import itertools
import json
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--dense', action='store_true')
    parser.add_argument('--clipped-max', action='store_true')
    parser.add_argument('--npot', action='store_true')
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if not command or args.output.exists():
        parser.error('Supply a probe command and a new output directory')
    args.output.mkdir(parents=True)
    cases = [('parameters', ['--parameters'], 13)]
    for target, fmt, base, named in itertools.product(
            (0xde0, 0xde1, 0x806f), (0x8051, 0x8058, 0x8c43), (0, 2), (0, 1)):
        label = f'{target:x}-{fmt:x}-base{base}-named{named}'
        options = ['--case', hex(target), hex(fmt), str(base), str(named)]
        if args.dense:
            options.insert(0, '--dense')
        if args.clipped_max:
            options.insert(0, '--clipped-max')
        if args.npot:
            options.insert(0, '--npot')
        cases.append((label, options, 17 if base else 16))
    results = []
    for label, options, expected in cases:
        try:
            child = subprocess.run(command + options, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, timeout=45)
            output = child.stdout.decode('utf-8', errors='replace')
            code = child.returncode
        except subprocess.TimeoutExpired as error:
            output = (error.stdout or b'').decode('utf-8', errors='replace')
            code = 'timeout'
        (args.output / (label + '.log')).write_text(output)
        match = re.search(r'^automatic-mipmap checks=(\d+) failures=(\d+) '
                          r'skipped_format_families=(\d+)$', output, re.MULTILINE)
        summary = tuple(map(int, match.groups())) if match else None
        status = ('pass' if code == 0 and summary == (expected, 0, 0) else
                  'unsupported' if code == 77 and 'UNSUPPORTED' in output else
                  'fail' if summary and summary[1] else 'incomplete')
        results.append(dict(case=label, status=status, exit=code, summary=summary))
        (args.output / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
        print(f'{label}: {status} exit={code} summary={summary}', flush=True)
    counts = {key: sum(r['status'] == key for r in results)
              for key in ('pass', 'fail', 'unsupported', 'incomplete')}
    print(json.dumps(counts), flush=True)
    return 1 if counts['fail'] or counts['incomplete'] else 0


if __name__ == '__main__':
    raise SystemExit(main())
