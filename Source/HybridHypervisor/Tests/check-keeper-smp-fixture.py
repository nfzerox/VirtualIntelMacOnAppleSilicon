#!/usr/bin/env python3
"""Qualify retained keeper-SMP functional logs; never execute a guest.

check-keeper-smp-fixture.py STDOUT STDERR EXIT_CODE --cpus {2,4} --guard {0,1}
check-keeper-smp-fixture.py --self-test

EXIT_CODE must be the runner's observed terminal exit status. Final host records
must follow joined run loops; guest PASS text alone supplies no host path proof.
"""
import argparse
import json
from pathlib import Path
import re

MAX_LOG_BYTES = 1048576
UINT64_MAX = (1 << 64) - 1
FAULT_ADDRESS = 0x7fff10000000
# name, warm result, final value (None means fault), PF error, mutation stores.
PHASES = (
    ('remap', 17, 33, 0, 1),
    ('neighbor_after_remap', 55, 55, 0, 0),
    ('synonym_after_remap', 17, 17, 0, 0),
    ('accessed_rearm', 33, 33, 0, 1),
    ('nx_revoke', 33, None, 0x15, 1),
    ('nx_restore', 55, 33, 0, 1),
    ('us_revoke', 33, None, 0x15, 1),
    ('us_restore', 55, 33, 0, 1),
    ('present_revoke', 33, None, 0x14, 1),
    ('present_restore', 55, 33, 0, 1),
    ('threshold_burst', 33, 33, 0, 65),
    ('smc_remapped', 33, 49, 0, 1),
    ('smc_synonym', 17, 50, 0, 1),
    ('remapped_final', 49, 49, 0, 0),
    ('neighbor_final', 55, 55, 0, 0),
)


def lines(text):
    if not isinstance(text, str) or not text or len(text.encode('utf-8')) > MAX_LOG_BYTES:
        raise ValueError('empty or over-budget log')
    if not text.endswith('\n') or any(ord(c) < 32 and c != '\n' or ord(c) > 126 for c in text):
        raise ValueError('log incomplete/non-ASCII/control bytes')
    return [line for line in text.splitlines() if line]


def uint(text):
    if not re.fullmatch(r'0|[1-9][0-9]{0,19}', text):
        raise ValueError('noncanonical unsigned integer')
    number = int(text)
    if number > UINT64_MAX:
        raise ValueError('counter overflow')
    return number


def fields(line, prefix, keys, words=()):
    parts = line.split(' ')
    if parts[0] != prefix or len(parts) != len(keys) + 1:
        raise ValueError('record kind/field count mismatch')
    found = {}
    for part in parts[1:]:
        if part.count('=') != 1:
            raise ValueError('malformed key/value')
        key, value = part.split('=')
        if key not in keys or key in found:
            raise ValueError('unknown/duplicate field')
        found[key] = value if key in words else uint(value)
    if set(found) != set(keys):
        raise ValueError('missing field')
    return found


def functional(stdout, cpus):
    peers = set(range(1, cpus))
    established = False
    epoch = 1
    results = set()
    summaries = set()
    done = False
    for line in lines(stdout):
        if done:
            raise ValueError('data after terminal guest record')
        kind = line.split(' ', 1)[0]
        if kind == 'KEEPER_SMP_ESTABLISHED':
            row = fields(line, kind, ('cpu', 'a_then_b', 'shared_pte', 'distinct_upper'))
            if established or epoch != 1 or results or row != dict(cpu=0, a_then_b=1, shared_pte=1, distinct_upper=1):
                raise ValueError('keeper establishment missing/duplicated/wrong')
            established = True
        elif kind == 'KEEPER_SMP_RESULT':
            row = fields(line, kind, ('epoch', 'cpu', 'warm', 'outcome', 'value', 'rip', 'cr2', 'err'), ('outcome',))
            if not established or epoch > len(PHASES) or row['epoch'] != epoch or row['cpu'] not in peers or row['cpu'] in results:
                raise ValueError('peer result epoch/CPU/uniqueness mismatch')
            name, warm, value, error, stores = PHASES[epoch - 1]
            if row['warm'] != warm:
                raise ValueError('wrong warmed generation: ' + name)
            if value is None:
                if row['outcome'] != 'fault' or row['rip'] != FAULT_ADDRESS or row['cr2'] != FAULT_ADDRESS or row['err'] != error:
                    raise ValueError('wrong fault generation/identity: ' + name)
                # value is non-authoritative when the read faulted.
            elif (row['outcome'] != 'value' or row['value'] != value or row['rip'] or row['cr2'] or row['err']):
                raise ValueError('stale/wrong peer value: ' + name)
            results.add(row['cpu'])
        elif kind == 'KEEPER_SMP_PHASE':
            row = fields(line, kind, ('epoch', 'case', 'peers', 'writes', 'completed'), ('case',))
            if not established or epoch > len(PHASES) or results != peers:
                raise ValueError('phase lacks every peer result')
            name, warm, value, error, stores = PHASES[epoch - 1]
            if row != dict(epoch=epoch, case=name, peers=cpus - 1, writes=stores, completed=cpus - 1):
                raise ValueError('phase identity/store/completion count mismatch')
            results.clear()
            epoch += 1
        elif kind == 'KEEPER_SMP_CPU':
            row = fields(line, kind, ('cpu', 'role', 'ready_epoch', 'ack_epoch', 'done_epoch', 'parked', 'failures'), ('role',))
            cpu = row['cpu']
            if epoch != len(PHASES) + 1 or results or cpu >= cpus or cpu in summaries:
                raise ValueError('CPU summary missing phases/duplicate identity')
            expected = dict(cpu=cpu, role='writer' if cpu == 0 else 'reader', ready_epoch=15 if cpu else 0,
                            ack_epoch=15 if cpu else 0, done_epoch=15, parked=1, failures=0)
            if row != expected:
                raise ValueError('CPU generation/quiescence/failure mismatch')
            summaries.add(cpu)
        elif kind == 'KEEPER_SMP_DONE':
            row = fields(line, kind, ('schema', 'cpus', 'writer', 'epochs', 'failures'))
            if epoch != len(PHASES) + 1 or results or summaries != set(range(cpus)) or row != dict(schema=1, cpus=cpus, writer=0, epochs=15, failures=0):
                raise ValueError('guest terminal summary incomplete')
            done = True
        else:
            raise ValueError('unknown/failure guest record')
    if not done:
        raise ValueError('missing terminal guest record')
    return {'epochs': len(PHASES), 'peer_results': len(PHASES) * (cpus - 1),
            'all_cpu_generations_and_quiescence': True}


def host_final(stderr, cpus, guard):
    joined = False
    records = {}
    for line in lines(stderr):
        if re.search(r'engine stop|watchdog|fatal|panic|deadline|truncat|timed.out|\btimeout\b|\berror\b|\bFAIL\b', line, re.I):
            raise ValueError('host error/deadline/truncation diagnostic')
        if line.startswith('SMP_JOIN'):
            match = re.fullmatch(r'SMP_JOIN expected ([0-9]+) stopped ([0-9]+)', line)
            if joined or records or not match or tuple(map(uint, match.groups())) != (cpus, cpus):
                raise ValueError('missing/duplicate/incomplete host join')
            joined = True
        elif line.startswith('SMP_FINAL'):
            match = re.fullmatch(r'SMP_FINAL cpu ([0-9]+) stopped ([0-9]+) outcome ([a-z]+) exit (-?[0-9]+) guard ([0-9]+) suppressed ([0-9]+) thresholdFallbacks ([0-9]+) sharedDirsGlobal ([0-9]+)', line)
            if not joined or not match:
                raise ValueError('host final record before join/malformed')
            cpu, stopped, outcome, code, enabled, suppressed, fallback, shared = match.groups()
            cpu, stopped, enabled, suppressed, fallback, shared = map(uint, (cpu, stopped, enabled, suppressed, fallback, shared))
            if cpu >= cpus or cpu in records or stopped != 1 or outcome != 'exited' or code != '0' or enabled != guard:
                raise ValueError('host CPU identity/exit/mode mismatch')
            records[cpu] = dict(guard=enabled, suppressed=suppressed, threshold_fallbacks=fallback, shared_directories_global=shared)
        elif line.startswith('SMP_'):
            raise ValueError('unknown host SMP record')
        # Other Engine.stats() lines are retained diagnostics, not path proof.
    if not joined or set(records) != set(range(cpus)):
        raise ValueError('missing final host CPU record')
    shared = {record['shared_directories_global'] for record in records.values()}
    if len(shared) != 1 or next(iter(shared)) <= 0:
        raise ValueError('shared directory global metadata missing/inconsistent')
    if guard:
        if records[0]['suppressed'] <= 0 or records[0]['threshold_fallbacks'] <= 0:
            raise ValueError('writer guard suppression/fallback paths not demonstrated')
    elif any(record['suppressed'] or record['threshold_fallbacks'] for record in records.values()):
        raise ValueError('OFF control contains guard activity')
    return records


def qualify(stdout, stderr, exit_code, cpus, guard):
    if type(cpus) is not int or cpus not in (2, 4) or type(guard) is not int or guard not in (0, 1):
        raise ValueError('exact CPU count2/4 and guard mode0/1 required')
    if type(exit_code) is not int or exit_code != 0:
        raise ValueError('runner terminal exit0 required')
    guest = functional(stdout, cpus)
    host = host_final(stderr, cpus, guard)
    return {'result': 'KEEPER_SMP_TARGETED_FUNCTIONAL_AND_PATH_GATES_PASS', 'cpus': cpus, 'guard': guard,
            'writer': 0, 'guest': guest, 'host_final_by_cpu': host,
            'shared_directories_are_global_metadata_not_summed': True,
            'scope': 'standalone targeted SMP fixture; no performance or production-default qualification'}


def sample(cpus, guard):
    out = ['KEEPER_SMP_ESTABLISHED cpu=0 a_then_b=1 shared_pte=1 distinct_upper=1']
    for epoch, (name, warm, value, error, stores) in enumerate(PHASES, 1):
        for cpu in range(1, cpus):
            fault = value is None
            out.append('KEEPER_SMP_RESULT epoch=%d cpu=%d warm=%d outcome=%s value=%d rip=%d cr2=%d err=%d' %
                       (epoch, cpu, warm, 'fault' if fault else 'value', 0 if fault else value,
                        FAULT_ADDRESS if fault else 0, FAULT_ADDRESS if fault else 0, error))
        out.append('KEEPER_SMP_PHASE epoch=%d case=%s peers=%d writes=%d completed=%d' % (epoch, name, cpus-1, stores, cpus-1))
    for cpu in range(cpus):
        out.append('KEEPER_SMP_CPU cpu=%d role=%s ready_epoch=%d ack_epoch=%d done_epoch=15 parked=1 failures=0' %
                   (cpu, 'writer' if cpu == 0 else 'reader', 15 if cpu else 0, 15 if cpu else 0))
    out.append('KEEPER_SMP_DONE schema=1 cpus=%d writer=0 epochs=15 failures=0' % cpus)
    err = ['SMP_JOIN expected %d stopped %d' % (cpus, cpus)]
    for cpu in range(cpus):
        err.append('SMP_FINAL cpu %d stopped 1 outcome exited exit 0 guard %d suppressed %d thresholdFallbacks %d sharedDirsGlobal 1' %
                   (cpu, guard, 64 if guard and cpu == 0 else 0, 1 if guard and cpu == 0 else 0))
    return '\n'.join(out)+'\n', '\n'.join(err)+'\n'


def self_test():
    checks = 0
    for cpus in (2, 4):
        for guard in (0, 1):
            out, err = sample(cpus, guard)
            assert qualify(out, err, 0, cpus, guard)['cpus'] == cpus
            checks += 1
    out, err = sample(2, 1)
    def reject(a=out, b=err, code=0, cpus=2, guard=1):
        nonlocal checks
        try:
            qualify(a, b, code, cpus, guard)
        except ValueError:
            checks += 1
        else:
            raise AssertionError('unqualified fixture accepted')
    reject(code=1); reject(code=True); reject(cpus=3); reject(cpus=True); reject(guard=True)
    reject(a='PASS\n'); reject(a=''); reject(b=''); reject(a=out[:-1]); reject(b=err[:-1])
    reject(a=out+out); reject(b=err+err); reject(a=out+'KEEPER_SMP_FAIL epoch=1\n')
    reject(a=out.replace('warm=17 outcome=value value=33', 'warm=17 outcome=value value=17', 1))
    reject(a=out.replace('warm=17 outcome=value value=33', 'warm=33 outcome=value value=33', 1))
    reject(a=out.replace('epoch=1 cpu=1', 'epoch=2 cpu=1', 1))
    reject(a=out.replace('epoch=1 cpu=1', 'epoch=1 cpu=0', 1))
    reject(a=out.replace('case=remap', 'case=neighbor_after_remap', 1))
    reject(a=out.replace('writes=65', 'writes=64', 1))
    reject(a=out.replace('peers=1 writes=1 completed=1', 'peers=1 writes=1 completed=0', 1))
    reject(a=out.replace('err=21', 'err=20', 1)); reject(a=out.replace('cr2=%d' % FAULT_ADDRESS, 'cr2=1', 1))
    reject(a=out.replace('ready_epoch=15', 'ready_epoch=14', 1)); reject(a=out.replace('ack_epoch=15', 'ack_epoch=14', 1))
    reject(a=out.replace('done_epoch=15', 'done_epoch=14', 1)); reject(a=out.replace('parked=1', 'parked=0', 1))
    reject(a=out.replace('failures=0', 'failures=1', 1)); reject(a=out.replace('schema=1 cpus=2', 'schema=1 cpus=4'))
    reject(a=out.replace('epoch=1 cpu=1 ', 'epoch=1 cpu=1 cpu=1 ', 1)); reject(a=out.replace('warm=17', 'warm=-17', 1))
    reject(a=out.replace('warm=17', 'warm=18446744073709551616', 1)); reject(a=out+'x'*MAX_LOG_BYTES+'\n')
    reject(a=out.replace('KEEPER_SMP_DONE', 'deadline KEEPER_SMP_DONE')); reject(a=out+'[truncated]\n')
    reject(b=err.replace('SMP_JOIN expected 2 stopped 2', 'SMP_JOIN expected 2 stopped 1'))
    reject(b=err.replace('SMP_JOIN expected 2 stopped 2\n', ''))
    reject(b=err.replace('cpu 1 stopped', 'cpu 0 stopped')); reject(b=err.replace('cpu 1 stopped', 'cpu 2 stopped'))
    reject(b=err.replace('outcome exited', 'outcome boundary', 1)); reject(b=err.replace('exit 0', 'exit -1', 1))
    reject(b=err.replace('guard 1', 'guard 0', 1)); reject(b=err.replace('suppressed 64', 'suppressed 0', 1))
    reject(b=err.replace('thresholdFallbacks 1', 'thresholdFallbacks 0', 1))
    reject(b=err.replace('sharedDirsGlobal 1', 'sharedDirsGlobal 0'))
    reject(b=err.replace('sharedDirsGlobal 1', 'sharedDirsGlobal 2', 1))
    reject(b='\n'.join(err.splitlines()[1:]+err.splitlines()[:1])+'\n')
    reject(b=err+'engine stop on vCPU 1: error\n'); reject(b=err+'watchdog deadline\n')
    reject(b=err+'[truncated]\n'); reject(b=err+'fatal\n'); reject(b=err+'SMP_UNKNOWN 0\n')
    off_out, off_err = sample(2, 0)
    reject(a=off_out, b=off_err.replace('suppressed 0', 'suppressed 1', 1), guard=0)
    reject(a=off_out, b=off_err.replace('thresholdFallbacks 0', 'thresholdFallbacks 1', 1), guard=0)
    # Missing/duplicate peer/phase/CPU summaries are each checked, including4CPU.
    for cpus in (2, 4):
        a, b = sample(cpus, 1)
        for prefix in ('KEEPER_SMP_ESTABLISHED ', 'KEEPER_SMP_RESULT ', 'KEEPER_SMP_PHASE ', 'KEEPER_SMP_CPU ', 'KEEPER_SMP_DONE '):
            rows = a.splitlines(); index = next(i for i, line in enumerate(rows) if line.startswith(prefix))
            reject(a='\n'.join(rows[:index]+rows[index+1:])+'\n', b=b, cpus=cpus)
            reject(a='\n'.join(rows[:index]+[rows[index]]+rows[index:])+'\n', b=b, cpus=cpus)
        rows = b.splitlines()
        reject(a=a, b='\n'.join(rows[:-1])+'\n', cpus=cpus)
    print(json.dumps({'result': 'PASS', 'ram_checks': checks, 'guest_or_native_execution': False,
                      'scope': 'checker acceptance/rejection tests; not runtime fixture proof'}, sort_keys=True))


def read_log(path):
    with Path(path).open('rb') as stream:
        data = stream.read(MAX_LOG_BYTES + 1)
    if len(data) > MAX_LOG_BYTES:
        raise ValueError('fixture log exceeds1MiB budget')
    return data.decode('ascii', 'strict')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('logs', nargs='*')
    parser.add_argument('--cpus', type=int, choices=(2, 4))
    parser.add_argument('--guard', type=int, choices=(0, 1))
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        if args.logs or args.cpus is not None or args.guard is not None:
            parser.error('--self-test takes no log/mode arguments')
        self_test()
        return
    if len(args.logs) != 3 or args.cpus is None or args.guard is None:
        parser.error('expected STDOUT STDERR EXIT_CODE --cpus2/4 --guard0/1')
    try:
        result = qualify(read_log(args.logs[0]), read_log(args.logs[1]), int(args.logs[2]), args.cpus, args.guard)
    except (ValueError, OSError) as error:
        parser.exit(1, 'INCONCLUSIVE ' + str(error) + '\n')
    print(json.dumps(result, sort_keys=True))


if __name__ == '__main__':
    main()
