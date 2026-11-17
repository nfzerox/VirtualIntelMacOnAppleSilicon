#!/usr/bin/env python3
"""Static fixture/protocol check. Arguments: prepared flat fixture and verified universal decoder. Executes neither."""
import argparse
from pathlib import Path
import runpy
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('fixture', type=Path)
    parser.add_argument('decoder', type=Path)
    args = parser.parse_args()
    driver = runpy.run_path(str(Path(__file__).with_name('vcphevc-sar.py')))
    driver['verify_decoder'](args.decoder)
    metadata = driver['verify_fixture'](args.fixture)
    with tempfile.TemporaryDirectory(prefix='lightclick-vcp-static-test-') as temporary:
        bad = Path(temporary)/'modified-fixture'
        original = args.fixture.read_bytes()
        position = original.find(driver['BODY'])
        assert position >= 0
        altered = bytearray(original)
        altered[position] ^= 1
        bad.write_bytes(altered)
        try:
            driver['verify_fixture'](bad)
        except ValueError:
            pass
        else:
            raise AssertionError('modified body accepted')
    # Explicit single-step register/flag vectors, rather than computing expected SAR with x86 or executing the image.
    vectors = ((0, (0, 0xffffffffffffffff, 0x4d5e3, 0x5a5a1234abcdef01, 0, 0, 0x44, 0xa5a5fedc98765432)),
               (32768, (0xffc0, 0xffffffffffff7fff, 0xe55e3, 0x5a5a1234abcdef01, 0xffffffc0, 0x18000, 4, 0xa5a5fedc98765432)))
    for seed, fields in vectors:
        expected = 0xcbf29ce484222325
        for field in fields:
            expected = (expected ^ field) * 0x1000193 % (1 << 64)
        assert driver['expected_hash'](1, seed) == f'{expected:016x}'
    expected = driver['expected_hash'](10, 3735928559)
    line = f'PASS vcphevc-sar cases=65536 iterations=10 elapsed_us=20 hash={expected} expected={expected}\n'
    assert driver['parse_result'](line, 10, expected)['elapsed_us'] == 20
    for text in (line.replace('cases=65536', 'cases=0'), line.replace('iterations=10', 'iterations=11'),
                 line.replace('elapsed_us=20', 'elapsed_us=0'), line.replace(expected, '0'*16), line+line):
        try:
            driver['parse_result'](text, 10, expected)
        except ValueError:
            pass
        else:
            raise AssertionError('invalid transcript accepted')
    print(f'PASS static exact25bytes/two bodies/fixed addresses, mutation refusal, oracle vectors and transcript checks: {metadata["fixture_sha256"]}')


if __name__ == '__main__':
    main()
