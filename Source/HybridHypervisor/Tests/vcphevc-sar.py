#!/usr/bin/env python3
"""Narrow same-byte dependency benchmark. prepare compiles/inspects ONLY; run explicitly executes existing runners.

prepare VERIFIED_VCPHEVC NEW_OUTPUT_DIRECTORY
run FIXTURE ENGINE_RUN FLATRUN NEW_RESULTS_DIRECTORY [--iterations 100000] [--runs 3]

The fixture uses existing flat test addresses, not guest ASLR. Fast flags is a benchmark diagnostic, never a guest setting.
This is a synthetic register/context fixture around measured bytes, not a replay of captured decoder state.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import struct
import subprocess

HERE = Path(__file__).resolve().parent
BODY = bytes.fromhex('66c1fa09448d04814c8b5f20488b4728488945a80fbec285f6')
VERIFIED_DECODER = 'd01da39e2fc4db0b02031100b74ee2ddd38f08d81f508e6dfd36e578149baf34'
TEXT, DATA = 0x100000000000, 0x100010000000
MASK = (1 << 64) - 1


def sha(data):
    return hashlib.sha256(data).hexdigest()


def macho(raw):
    if raw[:4] != b'\xcf\xfa\xed\xfe':
        raise ValueError('expected little-endian 64-bit Mach-O')
    segments, symbols, table = [], {}, None
    cursor = 32
    for _ in range(struct.unpack_from('<I', raw, 16)[0]):
        command, size = struct.unpack_from('<II', raw, cursor)
        if size < 8 or cursor + size > len(raw):
            raise ValueError('invalid load command')
        if command == 0x19:
            name = raw[cursor+8:cursor+24].split(b'\0')[0].decode()
            address, length, offset, count = struct.unpack_from('<QQQQ', raw, cursor+24)
            segments.append((name, address, length, offset, count))
        elif command == 2:
            table = struct.unpack_from('<IIII', raw, cursor+8)
        cursor += size
    if table:
        symoff, count, stroff, strsize = table
        strings = raw[stroff:stroff+strsize]
        for i in range(count):
            index, kind, section, desc, value = struct.unpack_from('<IBBHQ', raw, symoff+i*16)
            name = strings[index:].split(b'\0')[0].decode()
            if name:
                symbols[name] = value
    return segments, symbols


def at_address(raw, segments, address, length):
    for name, base, size, offset, count in segments:
        if base <= address and address + length <= base + count:
            return raw[offset+address-base:offset+address-base+length]
    raise ValueError('fixture bytes are outside file-backed segments')


def verify_decoder(path):
    raw = path.read_bytes()
    if len(raw) != 4528656 or sha(raw) != VERIFIED_DECODER or raw[:4] != b'\xca\xfe\xba\xbe':
        raise ValueError('decoder is not the verified universal Mojave artifact')
    slices = [struct.unpack_from('>IIIII', raw, 8+i*20) for i in range(struct.unpack_from('>I', raw, 4)[0])]
    matches = [entry for entry in slices if entry[0] == 0x1000007 and entry[1] == 3]
    if len(matches) != 1:
        raise ValueError('expected exactly one baseline x86_64 slice, never x86_64h')
    _, _, offset, length, _ = matches[0]
    image = raw[offset:offset+length]
    segments, _ = macho(image)
    if at_address(image, segments, 0xd9033, 25) != BODY:
        raise ValueError('verified decoder fragment does not match the exact fixture body')
    return {'decoder_sha256': sha(raw), 'x86_64_slice_offset': offset, 'x86_64_slice_bytes': length,
            'x86_64_slice_sha256': sha(image), 'fragment_address': '0xd9033', 'fragment_hex': BODY.hex()}


def verify_fixture(path):
    raw = path.read_bytes()
    segments, symbols = macho(raw)
    mapped = {name: address for name, address, size, offset, count in segments if name != '__LINKEDIT'}
    if mapped != {'__TEXT': TEXT, '__DATA': DATA}:
        raise ValueError(f'unexpected fixture mapping; refusing fixed-address runner: {mapped}')
    for prefix in ('_vcp_case', '_vcp_batch'):
        start, end = symbols[prefix+'_body'], symbols[prefix+'_end']
        if end - start != 25 or at_address(raw, segments, start, 25) != BODY:
            raise ValueError('compiled fixture body differs from verified source bytes')
    return {'fixture_sha256': sha(raw), 'text_address': hex(TEXT), 'data_address': hex(DATA),
            'case_body': hex(symbols['_vcp_case_body']), 'batch_body': hex(symbols['_vcp_batch_body']),
            'fragment_hex': BODY.hex()}


def prepare(decoder, destination):
    provenance = verify_decoder(decoder)
    destination.mkdir(parents=True, exist_ok=False)
    x86 = HERE/'x86'
    flags = ['xcrun', 'clang', '-target', 'x86_64-apple-macos10.9', '-march=x86-64', '-O2', '-ffreestanding',
             '-fno-builtin', '-fno-stack-protector', '-fno-unwind-tables', '-fno-asynchronous-unwind-tables',
             '-fno-exceptions', '-mno-red-zone', '-fno-vectorize', '-fno-slp-vectorize', '-I', str(x86),
             '-Wno-incompatible-library-redeclaration', '-Wno-format']
    commands = []
    objects = []
    for name in ('t_vcphevc_sar.c', 't_vcphevc_sar.s', 'flatlibc.c'):
        obj = destination/(name + '.o')
        command = [*flags, '-c', str(x86/name), '-o', str(obj)]
        commands.append(command)
        subprocess.run(command, check=True, capture_output=True, text=True, timeout=30)
        objects.append(str(obj))
    image = destination/'t_vcphevc_sar'
    command = ['xcrun', 'ld', '-arch', 'x86_64', '-preload', '-e', '_flat_start', '-no_uuid', '-pagezero_size', '0',
               '-segaddr', '__TEXT', hex(TEXT), '-segaddr', '__DATA', hex(DATA), '-o', str(image), *objects]
    commands.append(command)
    subprocess.run(command, check=True, capture_output=True, text=True, timeout=30)
    provenance.update(verify_fixture(image), commands=commands, executed=False,
                      note='Compiled and statically verified only. Runtime correctness and performance remain untested.')
    (destination/'prepare.json').write_text(json.dumps(provenance, indent=2)+'\n')
    print(f'PREPARED exact 25-byte fixture without execution: {image}')


def expected_hash(iterations, seed):
    value = 0xcbf29ce484222325
    for i in range(iterations):
        source = (seed + i) & MASK
        signed = (source & 65535) - (65536 if source & 32768 else 0)
        shifted = signed // 512
        result = ((source & ~65535) | (shifted & 65535)) & MASK
        low = shifted & 255
        eax = ((low - 256) if low >= 128 else low) & 0xffffffff
        esi = source * 3 & 0xffffffff
        flags = (64 if esi == 0 else 0) | (128 if esi & 0x80000000 else 0) | (4 if bin(esi & 255).count('1') % 2 == 0 else 0)
        fields = (result, ~source & MASK, ((~source & MASK) + (source * 5 + 0x13579) * 4) & 0xffffffff,
                  0x5a5a1234abcdef01, eax, esi, flags, 0xa5a5fedc98765432)
        for field in fields:
            value = ((value ^ field) * 0x1000193) & MASK
    return f'{value:016x}'


def parse_result(stdout, iterations, expected):
    rows = re.findall(r'^PASS vcphevc-sar cases=65536 iterations=([0-9]+) elapsed_us=([0-9]+) hash=([0-9a-f]{16}) expected=([0-9a-f]{16})$', stdout, re.M)
    if len(rows) != 1 or int(rows[0][0]) != iterations or rows[0][2:] != (expected, expected) or int(rows[0][1]) <= 0:
        raise ValueError('fixture output failed independent iteration/hash/duration validation')
    return {'iterations': iterations, 'elapsed_us': int(rows[0][1]), 'hash': expected,
            'ns_per_fragment_with_wrapper': int(rows[0][1]) * 1000 / iterations}


def run(image, engine, flatrun, destination, iterations, runs):
    metadata = verify_fixture(image)
    destination.mkdir(parents=True, exist_ok=False)
    expected = expected_hash(iterations, 3735928559)
    metadata.update(result='RUNNING', expected_hash=expected, trials=[],
                    engine_sha256=sha(engine.read_bytes()), flatrun_sha256=sha(flatrun.read_bytes()),
                    note='Synthetic fragment plus wrapper; fast flags is diagnostic only. No whole-decoder or application speedup claim.')
    def save():
        (destination/'result.json').write_text(json.dumps(metadata, indent=2)+'\n')
    def interrupted(number, frame):
        raise SystemExit(128 + number)
    for number in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
        signal.signal(number, interrupted)
    modes = ('precise', 'fast-flags', 'rosetta')
    try:
        save()
        for trial in range(runs):
            order = modes[trial % 3:] + modes[:trial % 3]
            for mode in order:
                env = {key: value for key, value in os.environ.items() if not key.startswith('ENGINE_')}
                if mode == 'rosetta':
                    command = ['arch', '-x86_64', str(flatrun), str(image), str(iterations), '3735928559']
                else:
                    env['ENGINE_ALARM'] = '20'
                    if mode == 'fast-flags':
                        env['ENGINE_FAST_FLAGS'] = '1'
                    command = [str(engine), str(image), str(iterations), '3735928559']
                process = subprocess.Popen(command, env=env, stdin=subprocess.DEVNULL,
                                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, start_new_session=True)
                try:
                    stdout, stderr = process.communicate(timeout=35)
                finally:
                    if process.poll() is None:
                        process.terminate()
                        try:
                            process.wait(timeout=2)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.wait(timeout=2)
                prefix = destination/f'{trial + 1}-{mode}'
                prefix.with_suffix('.stdout.txt').write_text(stdout)
                prefix.with_suffix('.stderr.txt').write_text(stderr)
                if process.returncode:
                    raise RuntimeError(f'{mode} failed with exit {process.returncode}')
                metadata['trials'].append(dict(parse_result(stdout, iterations, expected), mode=mode, trial=trial + 1,
                                               command=command, fast_flags=mode == 'fast-flags'))
                save()
                print(f'{mode}: {metadata["trials"][-1]["ns_per_fragment_with_wrapper"]:.3f} ns/fragment including wrapper', flush=True)
        metadata['result'] = 'PASS'
        save()
    except (Exception, SystemExit) as error:
        metadata.update(result='FAIL', error=repr(error))
        save()
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='action', required=True)
    prep = sub.add_parser('prepare')
    prep.add_argument('decoder', type=Path)
    prep.add_argument('output', type=Path)
    bench = sub.add_parser('run')
    for name in ('image', 'engine', 'flatrun', 'output'):
        bench.add_argument(name, type=Path)
    bench.add_argument('--iterations', type=int, default=100000)
    bench.add_argument('--runs', type=int, default=3)
    args = parser.parse_args()
    if args.action == 'prepare':
        prepare(args.decoder.resolve(), args.output.resolve())
    else:
        if not 1 <= args.iterations <= 1000000 or not 1 <= args.runs <= 3:
            parser.error('iterations must be 1..1000000 and runs 1..3')
        run(args.image.resolve(), args.engine.resolve(), args.flatrun.resolve(), args.output.resolve(), args.iterations, args.runs)


if __name__ == '__main__':
    try:
        main()
    except subprocess.CalledProcessError as error:
        raise SystemExit(f'FAIL fixture preparation: {error.stderr}') from error
