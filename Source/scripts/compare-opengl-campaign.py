#!/usr/bin/env python3
"""Account for an entire Khronos campaign without hiding crashes or missing cases.

Pass roots containing run-opengl-cts-batches.sh output directories, and the
original complete Khronos stdout-caselist. Only combined NNN.qpa files are
read: individual case logs and enumeration logs must not double-count tests.
An external crash annotation explains an absent case but never makes it pass.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import re
import runpy


def load(root,read_qpa):
    outcomes={}
    files=[]
    for path in sorted(root.rglob('*.qpa')):
        if not re.fullmatch(r'\d{3}',path.stem):
            continue
        cases,ended=read_qpa(path)
        files.append({'File':str(path.relative_to(root)),'SessionEnded':ended,'Cases':len(cases)})
        duplicate=outcomes.keys()&cases.keys()
        if duplicate:
            raise ValueError(f'Duplicate campaign cases in {path}: {sorted(duplicate)}')
        outcomes.update(cases)
    if not files:
        raise ValueError(f'No combined Khronos logs found under {root}')
    return outcomes,files


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('reference',type=Path)
    parser.add_argument('guest',type=Path)
    parser.add_argument('--case-list',type=Path,required=True)
    parser.add_argument('--external-repros',type=Path)
    parser.add_argument('--json-output',type=Path)
    args=parser.parse_args()
    expected={line[6:].strip() for line in args.case_list.read_text().splitlines() if line.startswith('TEST: ')}
    if not expected:
        parser.error('The complete case list has no TEST: entries.')
    read_qpa=runpy.run_path(str(Path(__file__).with_name('compare-opengl-cts.py')))['read_qpa']
    native,native_files=load(args.reference,read_qpa)
    guest,guest_files=load(args.guest,read_qpa)
    extra=(native.keys()|guest.keys())-expected
    if extra:
        raise ValueError(f'Observed cases outside the expected campaign: {sorted(extra)}')
    annotations={}
    if args.external_repros:
        for line in args.external_repros.read_text().splitlines():
            if not line.strip() or line.startswith('#'):
                continue
            name,kind,reason=line.split('\t',2)
            if name not in expected or name in annotations:
                raise ValueError(f'Invalid or duplicate external reproduction: {name}')
            annotations[name]={'Kind':kind,'Reason':reason}
    rows=[{'Case':name,'Native':native.get(name,'Missing'),'Guest':guest.get(name,'Missing'),
           **({'ExternalReproduction':annotations[name]} if name in annotations else {})}
          for name in sorted(expected)]
    pairs=Counter((row['Native'],row['Guest']) for row in rows)
    report={'ExpectedCases':len(expected),'NativeObserved':len(native),'GuestObserved':len(guest),
            'NativeFiles':native_files,'GuestFiles':guest_files,'Cases':rows}
    if args.json_output:
        args.json_output.write_text(json.dumps(report,indent=2)+'\n')
    print(f'Expected: {len(expected)}; native observed: {len(native)}; guest observed: {len(guest)}')
    for (left,right),count in sorted(pairs.items()):
        print(f'{left:20s} -> {right:20s} {count:5d}')
    print('\nNative-pass differences:')
    for row in rows:
        if row['Native']=='Pass' and row['Guest']!='Pass':
            print(f"{row['Native']} -> {row['Guest']}\t{row['Case']}")
    for row in rows:
        if 'ExternalReproduction' in row:
            print(f"External reproduction (not a pass): {row['Case']}\t{row['ExternalReproduction']}")
    incomplete=any(row[key] in {'Missing','Incomplete'} for row in rows for key in ('Native','Guest'))
    print(f"\nShared passes: {pairs['Pass','Pass']}/{len(expected)}")
    print('Shared failures, unsupported cases and external reproductions are not conformance passes.')
    if incomplete:
        print('Campaign incomplete: absent or unterminated cases remain open.')
        return 2
    return int(any(row['Guest']!='Pass' and
                   not (row['Guest']=='NotSupported' and row['Native']=='NotSupported')
                   for row in rows))


if __name__=='__main__':
    raise SystemExit(main())
