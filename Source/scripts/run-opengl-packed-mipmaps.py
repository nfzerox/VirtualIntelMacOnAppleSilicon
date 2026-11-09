#!/usr/bin/env python3
"""Run packed depth/stencil mipmap controls through a supplied command prefix.

Use the public probe on guests/references; --adapted belongs only to the
optional owned-process native diagnostic. Unsupported cases are retained,
not converted to full feature passes. A probe timeout is an incomplete test,
not an instruction to restart the VM or repeat a pending guest operation.
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
    for profile, flags in (
            ('explicit', []), ('core', ['--core']),
            ('automatic', ['--automatic']),
            ('list', ['--automatic', '--list']),
            ('compile-execute', ['--automatic', '--compile-execute'])):
        for source, input_flags in (('cpu', []), ('pbo', ['--pbo'])):
            label = profile + '-' + source
            try:
                child = subprocess.run(command + flags + input_flags,
                                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                       timeout=90)
                output, code = child.stdout.decode('utf-8', errors='replace'), child.returncode
            except subprocess.TimeoutExpired as error:
                output = (error.stdout or b'').decode('utf-8', errors='replace')
                code = 'timeout'
            (args.output / (label + '.log')).write_text(output)
            matches = re.findall(r'^packed-mipmap checks=(\d+) failures=(\d+) unsupported=(\d+)$',
                                 output, re.MULTILINE)
            summary = tuple(map(int, matches[0])) if len(matches) == 1 else None
            cases = re.findall(r'^case float=[01] cube=[01] base=[02] automatic=[01] mode=[0-9a-f]+ pbo=[01]$',
                               output, re.MULTILINE)
            if summary and summary[1]:
                status = 'fail'
            elif summary and code in (0, 77) and len(cases) == len(set(cases)) == 8:
                status = ('unsupported' if summary[2] == 8 else
                          'pass-with-unsupported' if summary[2] else
                          'pass' if code == 0 and summary[0] == 376 else 'incomplete')
            else:
                status = 'unsupported' if code == 77 and not summary else 'incomplete'
            results.append(dict(test=label, status=status, exit=code, summary=summary,
                                cases=len(cases), expected_cases=8))
            (args.output / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
            print(f'{label}: {status} exit={code} summary={summary}', flush=True)
    return int(any(result['status'] in ('fail', 'incomplete') for result in results))


if __name__ == '__main__':
    raise SystemExit(main())
