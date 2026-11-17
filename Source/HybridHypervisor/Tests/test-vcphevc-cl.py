#!/usr/bin/env python3
"""Static-only CL fixture checks: FIXTURE VERIFIED_DECODER. Neither binary is executed."""
import argparse
from pathlib import Path
import runpy
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('fixture',type=Path)
    parser.add_argument('decoder',type=Path)
    args = parser.parse_args()
    driver = runpy.run_path(str(Path(__file__).with_name('vcphevc-cl.py')))
    driver['verify_decoder'](args.decoder)
    metadata = driver['verify_fixture'](args.fixture)
    original = args.fixture.read_bytes()
    with tempfile.TemporaryDirectory(prefix='lightclick-cl-static-test-') as temporary:
        target = Path(temporary)/'modified'
        position = original.find(driver['BODY'])
        assert position >= 0 and len(driver['BODY'])==29
        changed = bytearray(original)
        changed[position+7] ^= 1  # Mutate the second shift; neither shift may be omitted or substituted.
        target.write_bytes(changed)
        try:
            driver['verify_fixture'](target)
        except ValueError:
            pass
        else:
            raise AssertionError('changed second shift accepted')
    # Hand-specified normal-result vectors include CL=0 and CL=1, both stores and complete preserved upper RDX.
    assert driver['fields'](0) == (0x13579,0xffffffffffffdb98,0xffffffffffffffff,0xffffffffffffffff,
                                  0x9234567800000013,0xffffffffffffffff,0x84,19)
    assert driver['fields'](1) == (0x26afc,0xffffffffffffb736,0,0xfffffffffffffff7,
                                  0x923456780000005c,3,4,92)
    expected = driver['expected_hash'](17,3735928559)
    line = f'PASS vcphevc-cl cases=4096 iterations=17 elapsed_us=30 hash={expected} expected={expected}\n'
    assert driver['parse_result'](line,17,expected)['elapsed_us']==30
    for altered in (line+line,line.replace('4096','0'),line.replace('elapsed_us=30','elapsed_us=0'),
                    line.replace('iterations=17','iterations=18'),line.replace(expected,'0'*16)):
        try:
            driver['parse_result'](altered,17,expected)
        except ValueError:
            pass
        else:
            raise AssertionError('invalid CL transcript accepted')
    print(f'PASS CL exact29bytes/two bodies/knownaddresses, mutation refusal, oracle vectors/transcripts: {metadata["fixture_sha256"]}')


if __name__=='__main__':
    main()
