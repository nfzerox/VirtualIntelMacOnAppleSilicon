import contextlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch


SCRIPT = Path(__file__).resolve().parents[1] / 'scripts/measure-guest-cpu-stability.py'
spec = importlib.util.spec_from_file_location('guest_cpu_stability', SCRIPT)
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class GuestCPUStabilityTests(unittest.TestCase):
    def run_series(self, output, response, *arguments):
        with patch.object(runner.sys, 'argv', [str(SCRIPT), '/diagnostics', str(output), '--runs', '2', *arguments]), \
             patch.object(runner.subprocess, 'run', return_value=response) as call, \
             contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            status = runner.main()
        return status, call

    def test_records_native_rows_without_changing_guest_configuration(self):
        response = subprocess.CompletedProcess([], 0, json.dumps({
            'ExitCode': 0, 'Stdout': 'sha1 10.00k 20.00k 30.00k 40.00k 50.00k\n'}), '')
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / 'series'
            status, call = self.run_series(output, response)
            self.assertEqual(status, 0)
            self.assertEqual(call.call_count, 2)
            record = json.loads((output / 'run-02.json').read_text())
            self.assertEqual(record['SHA1BytesPerSecond'], [10000, 20000, 30000, 40000, 50000])
            self.assertEqual(call.call_args.args[0][-4:], ['/usr/bin/openssl', 'speed', '-elapsed', 'sha1'])

    def test_unobserved_request_stops_without_replaying_it(self):
        response = subprocess.CompletedProcess([], 1, '', 'No result yet. The command may still run.')
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / 'series'
            status, call = self.run_series(output, response)
            self.assertEqual(status, 1)
            self.assertEqual(call.call_count, 1)
            self.assertTrue((output / 'run-01.json').is_file())
            self.assertFalse((output / 'run-02.json').exists())

    def test_fixed_work_distinguishes_a_known_digest_from_short_input(self):
        correct = '3b71f43ff30f4b15b5cd85dd9e95ebc7e84eb5a3'
        for stdout, expected in [(correct + '\n', True),
                                 ('SHA1(stdin)= ' + correct + '\n', True),
                                 ('SHA1(stdin)= ' + '0' * 40 + '\n', False)]:
            with self.subTest(stdout=stdout), tempfile.TemporaryDirectory() as temporary:
                output = Path(temporary) / 'series'
                response = subprocess.CompletedProcess([], 0, json.dumps({
                    'ExitCode': 0, 'Stdout': stdout}), '')
                status, call = self.run_series(output, response, '--fixed-mib', '1')
                self.assertEqual(status, int(not expected))
                self.assertEqual(call.call_count, 2 if expected else 1)
                record = json.loads((output / 'run-01.json').read_text())
                self.assertEqual(record['FixedMiB'], 1)
                self.assertEqual(record['DigestVerified'], expected)
                self.assertGreater(record['HostSeconds'], 0)


if __name__ == '__main__':
    unittest.main()
