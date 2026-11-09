#!/usr/bin/env python3
"""Summarize scoped transition diagnostics, never animation FPS or conformance.

Presentation counters survive measurement disarming. Only before/after deltas
within the same generation describe this recording; totals from successive
recordings must not be compared as independent frame counts.
"""
import argparse
import json
from pathlib import Path

COUNTERS = ('PresentedFingerprintFrames', 'DistinctFrames', 'DuplicateFrames',
            'Failures', 'Reordered', 'TraceDropped', 'Unpresented')
GEOMETRY = ('PixelWidth', 'PixelHeight', 'ViewWidth', 'ViewHeight', 'BackingScale')


def visible_frame(frame):
    return (frame.get('WindowOcclusionVisible') == 1
            and not any(frame.get(key) for key in ('Sleeping', 'GuestDisplaySleeping', 'PresentationHidden'))
            and all(frame.get(key, 0) > 0 for key in GEOMETRY))


def summarize(journal):
    issues = []
    counts = {}
    try:
        first_frame = journal['MeasurementBefore']['Frame']
        last_frame = journal['MeasurementAfter']['Frame']
        if not (visible_frame(first_frame) and visible_frame(last_frame)):
            issues.append('The guest window was hidden, asleep or had no display at a measurement boundary.')
        if any(first_frame.get(key) != last_frame.get(key) for key in GEOMETRY):
            issues.append('Display geometry changed during the workload.')
        before = first_frame['DistinctPresentationTiming']
        after = last_frame['DistinctPresentationTiming']
        generation = before['Generation']
        if type(generation) is not int or generation < 0 or generation != after['Generation']:
            issues.append('Measurement generation changed during the workload.')
        for name in COUNTERS:
            first, last = before[name], after[name]
            if type(first) is not int or type(last) is not int or first < 0 or last < first:
                raise ValueError('Non-monotonic counter: ' + name)
            counts[name] = last-first
        earlier_trace = before['Trace']
        if after['Trace'][:len(earlier_trace)] != earlier_trace:
            issues.append('The before trace is not a prefix of the final trace.')
        trace = after['Trace'][len(earlier_trace):]
        if len(trace) != counts['PresentedFingerprintFrames']:
            issues.append('The trace does not contain every presentation in this measurement.')
        if not trace:
            issues.append('No image was actually presented during the workload.')
        changed = sum(row[2] is True for row in trace)
        if changed != counts['DistinctFrames']:
            issues.append('Changed trace entries do not match the distinct-frame delta.')
        if counts['DistinctFrames']+counts['DuplicateFrames'] != counts['PresentedFingerprintFrames']:
            issues.append('Distinct and duplicate deltas do not cover the presentation delta.')
        if before.get('Error') or after.get('Error'):
            issues.append('The fingerprint observer reported an error.')
        if any(counts[name] for name in ('Failures', 'Reordered', 'TraceDropped')):
            issues.append('Observer failures, reordering or dropped trace records occurred.')
        if journal['MeasurementAfter']['MeasurementEnabledAfter']:
            issues.append('Measurement was not disarmed.')
        if journal.get('MeasurementMayRemainArmed'):
            issues.append('The journal could not confirm measurement teardown.')
        actions = journal['Actions']
        if not actions or any(row.get('ExitCode') != 0 for row in actions):
            issues.append('A control action failed or its status is missing.')
        if journal.get('Failure'):
            issues.append('The workload reported a failure: ' + str(journal['Failure']))
    except (KeyError, TypeError, ValueError, IndexError) as failure:
        issues.append('Incomplete or inconsistent measurement: ' + str(failure))
        generation = None
    return {'MeasurementValid': not issues, 'Issues': issues,
            'Generation': generation, 'RunCounterDeltas': counts,
            'Caution': 'Scoped diagnostic counts, including setup/idle; not animation FPS, latency or visual/conformance acceptance.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('journals', nargs='+', type=Path)
    args = parser.parse_args()
    print(json.dumps([{'Journal': str(path), **summarize(json.loads(path.read_text()))}
                      for path in args.journals], indent=2))


if __name__ == '__main__':
    main()
