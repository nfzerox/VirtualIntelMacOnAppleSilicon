#!/usr/bin/env python3
"""Analyze the real app's finite pointer-workload journal, with no VM changes.

All input-queued/presented timestamps share the HOST clock. Close-button marker
calibration and trajectory matching are mandatory. Reports visible motion-period
cadence and bounded queued-input-to-presented-position age, NOT physical HID to
photon latency, arbitrary animations, or GL/effect conformance. For a repeating
circle, latency matching uses only the first arc before its position repeats.
Never discard long gaps to make the frame rate look healthier.
"""
import argparse
import bisect
import json
import math
from pathlib import Path
import statistics


def percentile(values, fraction):
    ordered = sorted(values)
    return ordered[min(len(ordered)-1, int((len(ordered)-1)*fraction))] if ordered else None


def summarize(journal, reference=None):
    issues = []
    try:
        if journal.get('Error'):
            raise ValueError(journal['Error'])
        before, after = journal['Before'], journal['After']
        a, b = before['DistinctPresentationTiming'], after['DistinctPresentationTiming']
        expected_columns = ['PresentedAt', 'RGBFingerprint', 'Changed', 'MarkerX', 'MarkerY', 'RegionRGBFingerprint']
        if a['TraceColumns'] != expected_columns or b['TraceColumns'] != expected_columns or \
                journal['InputColumns'] != ['QueuedAt', 'ScheduledAt', 'X', 'Y', 'Phase', 'Step'] or \
                journal['InputReplyColumns'] != ['QueuedAt', 'CompletedAt', 'Error']:
            raise ValueError('Unknown journal schema; do not reinterpret columns implicitly.')
        if a['Generation'] != b['Generation'] or a.get('Error') or b.get('Error'):
            raise ValueError('Observer generation/error changed.')
        if b['Trace'][:len(a['Trace'])] != a['Trace']:
            raise ValueError('Before trace is not a prefix.')
        trace = b['Trace'][len(a['Trace']):]
        if len(trace) != b['PresentedFingerprintFrames']-a['PresentedFingerprintFrames']:
            raise ValueError('Not every presentation has an observer record.')
        for key in ('Failures', 'Reordered', 'TraceDropped'):
            if b[key] != a[key]:
                raise ValueError('Observer failures/reordering/drop: '+key)
        for key in ('PixelWidth', 'PixelHeight', 'BackingScale'):
            if before[key] != after[key]:
                raise ValueError('Geometry/density changed.')
        if before.get('MaximumDrawableCount') != after.get('MaximumDrawableCount'):
            raise ValueError('Presentation buffering changed during this motion.')
        if before.get('AsynchronousDrawableAcquisition') != after.get('AsynchronousDrawableAcquisition'):
            raise ValueError('Drawable acquisition policy changed during this motion.')
        for frame in (before, after):
            if frame.get('Sleeping') or frame.get('PresentationHidden') or frame.get('WindowOcclusionVisible') != 1:
                raise ValueError('The real guest window was hidden/asleep.')
        if any(reply[2] for reply in journal['InputReplies']):
            raise ValueError('A production input request failed.')
        if len(journal['Inputs']) != len(journal['InputReplies']):
            raise ValueError('An input request is unacknowledged.')
        start, end = journal['MotionStart'], journal['MotionEnd']
        if end <= start or journal['Pattern'] != 'circle':
            raise ValueError('This position-age analysis requires a finite calibrated circle.')
        origin = next(row for row in journal['Inputs'] if row[4] == 'origin')
        calibration = next(row for row in journal['Inputs'] if row[4] == 'calibration')
        initial = [row for row in trace if row[0] < start and row[3] >= 0 and row[4] >= 0]
        if not initial:
            raise ValueError('No observed preroll close-button marker.')
        mx, my = initial[-1][3:5]
        sx, sy = after['PixelWidth']/32767, after['PixelHeight']/32767
        expected_dx = (calibration[2]-origin[2])*sx
        calibration_seen = abs(expected_dx) >= 8 and any(
            abs(row[3]-mx-expected_dx) <= 4 and abs(row[4]-my) <= 2 for row in initial)
        reused = False
        if not calibration_seen:
            # A startup stall can drop the short preroll move completely.
            # Do not call that a calibration pass. An EXPLICIT already valid
            # same-geometry reference may calibrate a later unchanged fixture;
            # the current run must STILL match the complete moving trajectory.
            if reference is None or not summarize(reference)['Valid']:
                raise ValueError('Calibration move was not presented; supply an independently valid same-geometry reference, not an assumed marker.')
            ref_origin = next(row for row in reference['Inputs'] if row[4] == 'origin')
            ref_initial = [row for row in reference['After']['DistinctPresentationTiming']['Trace']
                           if row[0] < reference['MotionStart'] and row[3] >= 0 and row[4] >= 0]
            if origin[2:4] != ref_origin[2:4] or any(after[key] != reference['After'][key]
                    for key in ('PixelWidth', 'PixelHeight', 'BackingScale')) or not ref_initial or \
                    abs(mx-ref_initial[-1][3]) > 2 or abs(my-ref_initial[-1][4]) > 2:
                raise ValueError('Reference calibration geometry/origin does not match this run.')
            reused = True
        motion = [row for row in trace if start <= row[0] < end]
        if not motion or any(row[3] < 0 or row[4] < 0 for row in motion):
            raise ValueError('Marker missing during actual motion.')
        changed = [row for row in motion if row[2]]
        gaps = [1000*(right[0]-left[0]) for left, right in zip(changed, changed[1:])]
        if any(gap < 0 for gap in gaps):
            raise ValueError('Non-monotonic presentation timestamps.')
        moved = sum((left[3], left[4]) != (right[3], right[4]) for left, right in zip(motion, motion[1:]))
        inputs = [row for row in journal['Inputs'] if row[4] == 'motion']
        queued = [row[0] for row in inputs]
        # Exclude the first/last quarter second only from position-age matching,
        # NOT cadence. The second circle is intentionally not used for latency:
        # its repeated positions cannot identify which input caused a frame.
        candidates = [row for row in motion if start+.25 <= row[0] < start+(end-start)/2-.15]
        lower, upper, nearest, residuals = [], [], [], []
        unmatched = 0
        for row in candidates:
            history = inputs[:bisect.bisect_right(queued, row[0])]
            distances = [(math.hypot(mx+(item[2]-origin[2])*sx-row[3],
                                    my+(item[3]-origin[3])*sy-row[4]), item[0]) for item in history]
            compatible = [item[1] for item in distances if item[0] <= 3]
            if not compatible:
                unmatched += 1
                continue
            best = min(distances, key=lambda item: (item[0], -item[1]))
            residuals.append(best[0])
            lower.append(1000*(row[0]-max(compatible)))
            upper.append(1000*(row[0]-min(compatible)))
            nearest.append(1000*(row[0]-best[1]))
        if not candidates or unmatched/len(candidates) > .05:
            issues.append('More than 5% of the first-arc markers do not match the real queued trajectory; possible contamination/clamping.')
        return dict(Valid=not issues, Issues=issues, Pattern=journal['Pattern'], MotionSeconds=end-start,
            ChangedPresentedImages=len(changed), ChangedImagesPerMotionSecond=len(changed)/(end-start),
            MarkerPositionChanges=moved, MissedHostTimerTicks=journal['MissedHostTimerTicks'],
            Calibration=dict(MovePresentedThisRun=calibration_seen, ReusedExplicitReference=reused,
                             OriginMarkerPixels=[mx, my], ExpectedCalibrationDeltaPixels=expected_dx),
            MaximumHostTimerLatenessMilliseconds=journal['MaximumHostTimerLatenessMilliseconds'],
            GapsMilliseconds=dict(Median=statistics.median(gaps) if gaps else None,
                P90=percentile(gaps, .90), P99=percentile(gaps, .99), Maximum=max(gaps) if gaps else None,
                Over25ms=sum(value > 25 for value in gaps), Count=len(gaps)),
            PositionAgeMilliseconds=dict(Matched=len(lower), Unmatched=unmatched,
                MedianLowerBound=statistics.median(lower) if lower else None,
                MedianUpperBound=statistics.median(upper) if upper else None,
                NearestPositionMedian=statistics.median(nearest) if nearest else None,
                P90UpperBound=percentile(upper, .90),
                P95SpatialResidualPixels=percentile(residuals, .95), TolerancePhysicalPixels=3),
            Caution='Real presented RGB/marker diagnostics, not physical HID-to-photon or broad 60Hz/conformance acceptance. Position age is spatially bounded, not an exact event identity.')
    except (KeyError, TypeError, ValueError, StopIteration, IndexError) as error:
        return dict(Valid=False, Issues=[str(error)])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('journals', nargs='+', type=Path)
    parser.add_argument('--reference-calibration', type=Path,
                        help='independently valid unchanged-window calibration; never an automatic fallback')
    args = parser.parse_args()
    reference = json.loads(args.reference_calibration.read_text()) if args.reference_calibration else None
    results = [dict(Journal=str(path), **summarize(json.loads(path.read_text()), reference)) for path in args.journals]
    print(json.dumps(results, indent=2))
    return int(any(not row['Valid'] for row in results))


if __name__ == '__main__':
    raise SystemExit(main())
