#!/usr/bin/env python3
"""Classify every requested Piglit process using its actual JSON and exit status.

Run on the builder after collecting a run-opengl-piglit results directory.
Missing/duplicate results, crashes and absent device identities stay incomplete.
This is diagnostic tooling, not part of a guest or an app hot path.
"""
import argparse
from collections import Counter
import json
from pathlib import Path


def summarize(root):
    cases=(root/'cases.txt').read_text().splitlines()
    if not cases or len(set(cases))!=len(cases):
        raise ValueError('Missing or duplicate requested cases')
    statuses={}
    for line in (root/'status.tsv').read_text().splitlines():
        name,status=line.split('\t')
        if name in statuses or name not in cases:
            raise ValueError('Unexpected or duplicate process status')
        statuses[name]=int(status)
    results=[]
    for name in cases:
        log=root/(name+'.log')
        text=log.read_text(errors='replace') if log.is_file() else ''
        records=[]
        for line in text.splitlines():
            if line.startswith('PIGLIT: '):
                try:
                    record=json.loads(line[len('PIGLIT: '):])
                except ValueError:
                    continue
                if isinstance(record,dict) and 'result' in record:
                    records.append(record)
        devices=[line for line in text.splitlines() if line.startswith('PIGLIT GL vendor=')]
        observed=records[-1]['result'] if records else None
        exit_code=statuses.get(name)
        result='incomplete'
        if devices and observed=='pass' and exit_code==0:
            result='pass'
        elif devices and observed=='skip' and exit_code in (0,77):
            result='skip'
        elif devices and observed=='fail' and exit_code==1:
            result='fail'
        results.append({'case':name,'result':result,'reported':observed,
            'exit':exit_code,'device':devices[-1] if devices else None})
    counts=Counter(row['result'] for row in results)
    return {'directory':str(root),'counts':dict(counts),'cases':results}


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directories',nargs='+',type=Path)
    args=parser.parse_args()
    reports=[summarize(root) for root in args.directories]
    print(json.dumps(reports,indent=2))
    raise SystemExit(int(any(report['counts'].get('fail',0) or
        report['counts'].get('incomplete',0) for report in reports)))
