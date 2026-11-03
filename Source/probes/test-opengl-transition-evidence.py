"""Fast offline checks of cumulative-counter attribution; no VM control."""
import copy
import importlib.util
from pathlib import Path
import unittest

module = Path(__file__).resolve().parents[1]/'scripts/opengl_transition_evidence.py'
spec = importlib.util.spec_from_file_location('transition_evidence', module)
evidence = importlib.util.module_from_spec(spec)
spec.loader.exec_module(evidence)


def journal():
    before = dict.fromkeys(evidence.COUNTERS, 0)
    before.update(Generation=7, PresentedFingerprintFrames=100, DistinctFrames=80,
                  DuplicateFrames=20, Trace=[], Error='')
    after = dict(before, PresentedFingerprintFrames=103, DistinctFrames=82,
                 DuplicateFrames=21, Trace=[[1, 10, True], [2, 11, True], [3, 11, False]])
    return {'MeasurementBefore': {'Frame': {'DistinctPresentationTiming': before}},
            'MeasurementAfter': {'Frame': {'DistinctPresentationTiming': after},
                                 'MeasurementEnabledAfter': False},
            'Actions': [{'ExitCode': 0}], 'Failure': None, 'MeasurementMayRemainArmed': False}


class EvidenceTests(unittest.TestCase):
    def test_uses_deltas_not_totals(self):
        result = evidence.summarize(journal())
        self.assertTrue(result['MeasurementValid'])
        self.assertEqual(result['RunCounterDeltas']['DistinctFrames'], 2)
        self.assertEqual(result['RunCounterDeltas']['PresentedFingerprintFrames'], 3)

    def test_rejects_generation_change(self):
        data = journal();data['MeasurementAfter']['Frame']['DistinctPresentationTiming']['Generation'] = 8
        self.assertFalse(evidence.summarize(data)['MeasurementValid'])

    def test_handles_a_frame_observed_before_the_baseline_snapshot(self):
        data = journal()
        data['MeasurementBefore']['Frame']['DistinctPresentationTiming']['Trace'] = [[0, 9, True]]
        data['MeasurementAfter']['Frame']['DistinctPresentationTiming']['Trace'].insert(0, [0, 9, True])
        result = evidence.summarize(data)
        self.assertTrue(result['MeasurementValid'])
        self.assertEqual(result['RunCounterDeltas']['DistinctFrames'], 2)

    def test_rejects_reset_or_dropped_trace(self):
        for key, value in [('DistinctFrames', 0), ('TraceDropped', 1), ('Trace', [])]:
            data = journal();data['MeasurementAfter']['Frame']['DistinctPresentationTiming'][key] = value
            self.assertFalse(evidence.summarize(data)['MeasurementValid'])

    def test_rejects_failed_control_and_missing_metadata(self):
        data = journal();data['Actions'][0]['ExitCode'] = 1
        self.assertFalse(evidence.summarize(data)['MeasurementValid'])
        self.assertFalse(evidence.summarize({})['MeasurementValid'])

    def test_does_not_mutate_raw_evidence(self):
        data = journal();before = copy.deepcopy(data)
        evidence.summarize(data);self.assertEqual(data, before)


if __name__ == '__main__':
    unittest.main()
