#!/usr/bin/env python3
"""Analyze fixed-window clicks and an independently chosen active/inactive ROI.

The real app's Pattern=activate workload measures host queued input to actually
presented pixels. Choose an exposed close-button/title ROI in one stationary
window; it must have two distinct, stable states. This does not measure the
whole window, application readiness or physical HID-to-photon latency.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import statistics


def analyze(data):
    if data.get('Error') or data.get('Pattern') != 'activate':
        raise ValueError(data.get('Error') or 'Expected an activation workload')
    before, after = data['Before'], data['After']
    a, b = before['DistinctPresentationTiming'], after['DistinctPresentationTiming']
    if b['TraceColumns'] != ['PresentedAt', 'RGBFingerprint', 'Changed', 'MarkerX', 'MarkerY', 'RegionRGBFingerprint']:
        raise ValueError('Unknown presentation trace columns')
    for key in ('PixelWidth', 'PixelHeight', 'BackingScale'):
        if before[key] != after[key]:
            raise ValueError('Guest geometry changed')
    for key in ('Failures', 'Reordered', 'TraceDropped'):
        if b[key] != a[key]:
            raise ValueError('Presentation observer changed: '+key)
    if a['Generation'] != b['Generation'] or b['Trace'][:len(a['Trace'])] != a['Trace']:
        raise ValueError('Presentation trace is not one continuous observation')
    if any(reply[2] for reply in data['InputReplies']) or len(data['Inputs']) != len(data['InputReplies']):
        raise ValueError('Input failed or was not acknowledged')
    trace = b['Trace'][len(a['Trace']):]
    clicks = [row for row in data['Inputs'] if row[4] == 'activation-down']
    if len(clicks) < 4:
        raise ValueError('Not enough alternating clicks')
    # The last observed ROI state before the following click is the settled
    # target. No full-frame fingerprint is used to infer app activation.
    targets = []
    for index, click in enumerate(clicks):
        end = clicks[index+1][0] if index+1 < len(clicks) else data['MotionEnd']
        frames = [row for row in trace if click[0] <= row[0] < end-.04]
        if not frames:
            raise ValueError('No presented ROI after a click')
        targets.append(frames[-1][5])
    states = [Counter(targets[parity::2]).most_common(1)[0][0] for parity in (0, 1)]
    if states[0] == states[1] or any(target != states[i % 2] for i, target in enumerate(targets)):
        raise ValueError('ROI did not settle into two alternating states')
    results = []
    # The first click can already target the active app: exclude it explicitly.
    for index, click in enumerate(clicks[1:], 1):
        at, target = click[0], states[index % 2]
        previous = [row for row in trace if row[0] < at]
        if not previous or previous[-1][5] == target:
            raise ValueError('ROI already active before a measured click')
        end = clicks[index+1][0] if index+1 < len(clicks) else data['MotionEnd']
        frame = next(row for row in trace if at <= row[0] < end and row[5] == target)
        results.append({'Step': click[5], 'QueuedToPresentedROIChangeMS': (frame[0]-at)*1000})
    values = [row['QueuedToPresentedROIChangeMS'] for row in results]
    return {'Switches': results, 'MedianMS': statistics.median(values),
            'MaximumMS': max(values), 'MissedHostTimerTicks': data['MissedHostTimerTicks'],
            'MaximumHostTimerLatenessMS': data['MaximumHostTimerLatenessMilliseconds'],
            'Caution': 'Fixed-window active/inactive ROI only; first click excluded. Independently verify unchanged real-window geometry and the two intended apps.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('journals', nargs='+', type=Path)
    args = parser.parse_args()
    output = []
    for path in args.journals:
        try:
            output.append({'Journal': str(path), 'Valid': True, **analyze(json.loads(path.read_text()))})
        except (KeyError, ValueError, StopIteration, IndexError) as error:
            output.append({'Journal': str(path), 'Valid': False, 'Error': str(error)})
    print(json.dumps(output, indent=2))
    return int(any(not row['Valid'] for row in output))


if __name__ == '__main__':
    raise SystemExit(main())
