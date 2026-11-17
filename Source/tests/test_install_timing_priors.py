import importlib.util
import math
from pathlib import Path
import plistlib
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('timings', SOURCE / 'scripts/collect-install-timing-priors.py')
TIMINGS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TIMINGS)


class TimingPriorTests(unittest.TestCase):
    def test_one_bundled_baseline_covers_every_shipping_family(self):
        model = plistlib.loads((SOURCE / 'VirtualIntelMac/HostInstallTimings.plist').read_bytes())
        self.assertEqual(set(model), {'Guests', 'Scales'})
        self.assertEqual(set(model['Scales']), {'Intel', 'ExFAT'})
        self.assertTrue(all(math.isfinite(x) and x > 0 for x in model['Scales'].values()))
        self.assertEqual(set(model['Guests']), TIMINGS.SHIPPING)
        for stages in model['Guests'].values():
            self.assertTrue(stages)
            for stage in stages:
                self.assertEqual(set(stage), {'Stage', 'Seconds'})
                self.assertIsInstance(stage['Stage'], str)
                self.assertNotIn('/', stage['Stage'])
                self.assertTrue(math.isfinite(stage['Seconds']) and stage['Seconds'] >= 0)

    def test_export_keeps_only_successful_shipping_observations_and_no_identity(self):
        with tempfile.TemporaryDirectory() as folder:
            paths = []
            for index, (guest, success, seconds) in enumerate((('mavericks', True, 50),
                    ('mavericks', False, 3), ('sonoma', True, 700), ('mavericks', True, 44))):
                path = Path(folder) / f'{index}.plist'
                path.write_bytes(plistlib.dumps({'Guest': guest, 'Succeeded': success,
                    'ElapsedSeconds': seconds, 'User': 'must-not-export', 'Path': '/private/example',
                    'Stages': [{'Stage': 'payload', 'Seconds': seconds}]}))
                paths.append(str(path))
            model = TIMINGS.collect([['arm64-apfs', *paths]])
            self.assertEqual(model, {'Guests': {'mavericks': [{'Stage': 'payload', 'Seconds': 44.0}]},
                                     'Scales': {'Intel': 2.0, 'ExFAT': 3.0}})

    def test_scale_factors_are_two_and_three_without_separate_guest_tables(self):
        with tempfile.TemporaryDirectory() as folder:
            groups = []
            for profile, values in (('arm64-apfs', (100, 200)), ('x86_64-apfs', (200, 600)),
                                    ('arm64-exfat', (400, 400))):
                group = [profile]
                for guest, seconds in zip(('mavericks', 'lion'), values):
                    path = Path(folder) / f'{profile}-{guest}.plist'
                    path.write_bytes(plistlib.dumps({'Guest': guest, 'Succeeded': True,
                        'ElapsedSeconds': seconds, 'Stages': [{'Stage': 'payload', 'Seconds': seconds}]}))
                    group.append(str(path))
                groups.append(group)
            model = TIMINGS.collect(groups)
            self.assertEqual(model['Scales'], {'Intel': 2.0, 'ExFAT': 3.0})
            self.assertEqual(model['Guests']['mavericks'][0]['Seconds'], 100)
