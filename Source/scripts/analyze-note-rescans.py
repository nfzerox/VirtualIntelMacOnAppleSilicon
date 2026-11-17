#!/usr/bin/env python3
"""Correlate a stopped AFTER snapshot with a finite real-Finder journal.

Usage: analyze-note-rescans.py /absolute/STEM /absolute/AFTER.json
No process control. Diagnostic timings include lock waits/host work/descheduling,
not just CPU execution. Union overlapping CPU spans; never add nested waits.
"""
import importlib.util
import json
from pathlib import Path
import sys


def union_ms(spans, start, end):
    merged = []
    for a, b in sorted((max(a, start), min(b, end)) for a, b in spans):
        if b <= a:
            continue
        if merged and a <= merged[-1][1]:
            merged[-1][1] = max(merged[-1][1], b)
        else:
            merged.append([a, b])
    return sum(b-a for a, b in merged)*1000


def analyze(stem, snapshot):
    spec = importlib.util.spec_from_file_location('native_motion',
        Path(__file__).with_name('analyze-native-launch-drag.py'))
    motion = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(motion)
    qualification = motion.summarize(stem)  # same geometry/session/input checks
    end = json.loads(Path(str(stem)+'-End.json').read_text())
    frame = end['Frame']['DistinctPresentationTiming']
    start = frame['PipelineTrace'][0][0]+1
    finish = start+qualification['CentralSeconds']
    scale = snapshot['MachTimebase']['numer']/snapshot['MachTimebase']['denom']/1e9
    observed = snapshot['ObservedMachTicks']*scale
    if observed < finish:
        raise ValueError('Snapshot does not follow the whole workload.')
    records = []
    for cpu, state in enumerate(snapshot['CPUs']):
        if state['profile_mmu_observations'] != 1:
            raise ValueError('Rescan diagnostic was not enabled on every CPU.')
        ring = state['NoteRescans']
        if ring['ActiveStartTicks']:
            raise ValueError('A rescan is still active; completed spans are insufficient.')
        samples = ring['Samples']
        if ring['Overwritten'] and (not samples or samples[0]['start_ticks']*scale > start):
            raise ValueError('Overwritten records might include the workload.')
        for row in samples:
            a, duration = row['start_ticks']*scale, row['duration_ticks']*scale
            if a+duration > observed:
                raise ValueError('Rescan clock is not aligned with the host snapshot.')
            if a < finish and a+duration > start:
                records.append(dict(CPU=cpu, Sequence=row['sequence'], Start=a,
                    End=a+duration, DurationMS=duration*1000,
                    CodeFrames=row['code_frames'], TableFrames=row['table_frames']))
    changed = [start]+[r[0] for r in frame['Trace'] if r[2] and start < r[0] < finish]+[finish]
    gaps = sorted(zip(changed, changed[1:]), key=lambda p:p[1]-p[0], reverse=True)[:5]
    spans = [(r['Start'], r['End']) for r in records]
    return dict(Journal=str(stem), Scope=qualification['Scope'],
        CentralStart=start, CentralEnd=finish, Recoveries=records,
        RecoveryUnionMS=union_ms(spans, start, finish),
        LongestChangedPresentationGaps=[dict(Start=a, DurationMS=(b-a)*1000,
            RecoveryOverlapMS=union_ms(spans, a, b)) for a, b in gaps],
        Caution='Diagnostic attribution, not an uninstrumented speed comparison or proof about other boots.')


if __name__ == '__main__':
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    try:
        print(json.dumps(analyze(Path(sys.argv[1]), json.loads(Path(sys.argv[2]).read_text())), indent=2))
    except (ValueError, KeyError, OSError, TypeError, IndexError) as error:
        raise SystemExit(str(error))
