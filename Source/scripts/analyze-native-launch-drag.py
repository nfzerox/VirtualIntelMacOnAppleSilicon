#!/usr/bin/env python3
"""Read finite real-Finder drag journals; never control a VM.

Usage: analyze-native-launch-drag.py /absolute/STEM [...]
Reads STEM-{Begin,Workload,End}.json from the visible app's diagnostic adapter.
The input helper is probes/opengl-native-finder-drag.c, not a synthetic window.
Report the central motion interval, INCLUDING all long gaps and edge gaps.
Native guest CGEvent injection does not measure host HID-to-photon latency.
Input call spans include descheduling, not just IPC or CPU work. Other guest
content also contributes to whole-desktop RGB changes; do not call this GL
conformance or infer a causal speedup across different boots/cache states.
"""
import json
from pathlib import Path
import re
import statistics
import sys


def gaps(times, start, end):
    values = [start] + sorted(t for t in times if start < t < end) + [end]
    durations = sorted((b-a)*1000 for a, b in zip(values, values[1:]))
    return dict(Intervals=len(durations), Median=statistics.median(durations),
        P95=durations[int(.95*(len(durations)-1))], Maximum=max(durations),
        Over50MS=sum(x > 50 for x in durations))


def summarize(stem):
    records = {part: json.loads(Path(str(stem)+'-'+part+'.json').read_text())
               for part in ('Begin', 'Workload', 'End')}
    for record in records.values():
        if record.get('Error') or record.get('ExitCode', 0):
            raise ValueError('App/guest request failed: '+str(record.get('Error') or record.get('Stderr')))
    if len({r.get('Session') for r in records.values()}) != 1:
        raise ValueError('Emulator session changed during the trial.')
    before, after = (records[part]['Frame'] for part in ('Begin', 'End'))
    geometry_keys = ('PixelWidth', 'PixelHeight', 'BackingScale', 'MaximumDrawableCount')
    if any(before[k] != after[k] for k in geometry_keys):
        raise ValueError('Display geometry or buffering changed.')
    for frame in (before, after):
        if frame.get('Sleeping') or frame.get('PresentationHidden') or frame.get('WindowOcclusionVisible') != 1:
            raise ValueError('The measured guest window was hidden/asleep.')
    a, b = (f['DistinctPresentationTiming'] for f in (before, after))
    if a['Generation'] != b['Generation'] or b.get('Error'):
        raise ValueError('Presentation observer changed or failed.')
    for key in ('Failures', 'Reordered', 'TraceDropped'):
        if b[key] != a[key]:
            raise ValueError('Observer failures/reordering/dropped records: '+key)
    if b['Trace'][:len(a['Trace'])] != a['Trace']:
        raise ValueError('Before trace is not a prefix of the same observation.')
    if b['TraceColumns'] != ['PresentedAt', 'RGBFingerprint', 'Changed', 'MarkerX', 'MarkerY', 'RegionRGBFingerprint']:
        raise ValueError('Unknown presentation schema.')
    text = records['Workload']['Stdout']
    ready = re.search(r'READY Finder window=(\d+) bounds=([^\n]+?) seconds=([0-9.]+) pattern=(\w+)', text)
    done = re.search(r'DONE events=(\d+) final_geometry_restored=1\b', text)
    timing = re.search(r'INPUT_TIMING max_start_gap_ms=([0-9.]+) gap_at_seconds=([0-9.]+) starts_over20ms=(\d+) max_post_span_ms=([0-9.]+) post_at_seconds=([0-9.]+)', text)
    if not ready or not done or not timing:
        raise ValueError('Missing native movement/geometry or producer-timing evidence.')
    seconds = float(ready[3])
    pipe = b['PipelineTrace']
    if not 3 <= seconds <= 12 or not pipe or pipe[-1][0]-pipe[0][0] < seconds-.5:
        raise ValueError('Insufficient continuous-motion time coverage; do not hide a late start.')
    # Exclude input acquisition/release only. No gap-size cutoff is applied.
    start, end = pipe[0][0]+1, pipe[0][0]+seconds-1
    changed = [row[0] for row in b['Trace'] if row[2] and start < row[0] < end]
    if not changed:
        raise ValueError('No observed RGB changes during native movement.')
    return dict(Journal=str(stem), Valid=True, Scope='central real-Finder motion; not host input latency',
        Geometry={k: after[k] for k in geometry_keys}, FinderWindow=int(ready[1]),
        NativeInputEvents=int(done[1]), CentralSeconds=end-start,
        ChangedPresentedImages=len(changed), SourceOfferGapsMS=gaps([r[0] for r in pipe], start, end),
        ChangedPresentationGapsMS=gaps(changed, start, end),
        Producer=dict(MaximumStartGapMS=float(timing[1]), GapAtSeconds=float(timing[2]),
            StartsOver20MS=int(timing[3]), MaximumPostCallSpanMS=float(timing[4]),
            PostAtSeconds=float(timing[5])))


if __name__ == '__main__':
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    reports = []
    for name in sys.argv[1:]:
        try:
            reports.append(summarize(Path(name)))
        except (ValueError, KeyError, OSError, IndexError, TypeError) as error:
            reports.append(dict(Journal=name, Valid=False, Error=str(error)))
    print(json.dumps(reports, indent=2))
    sys.exit(any(not report['Valid'] for report in reports))
