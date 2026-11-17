#!/usr/bin/env python3
"""Qualify retained same64-PTE SMP functional logs; never execute a guest.

check-pte-same64-fixture.py STDOUT STDERR EXIT_CODE --cpus 2 --same64 {0,1}
check-pte-same64-fixture.py --self-test

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
AD_ROUNDS = 128
DATA_VALUE = 0x12345678
PHASES = (
    ('same_value_preserve', 17, 17, 0, 1),
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
    ('identical_zero_flood', 33, 33, 0, 65),
    ('smc_remapped', 33, 49, 0, 1),
    ('smc_synonym', 17, 50, 0, 1),
    ('remapped_final', 49, 49, 0, 0),
    ('neighbor_final', 55, 55, 0, 0),
    ('inactive_zero_root', 49, 49, 0, 3),
    ('ad_walk_store_race', 55, DATA_VALUE, 0, AD_ROUNDS * 65),
    ('pt_code_alias_smc', 71, 73, 0, 1),
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
    ad_rounds = 0
    summaries = set()
    done = False
    for line in lines(stdout):
        if done:
            raise ValueError('data after terminal guest record')
        kind = line.split(' ', 1)[0]
        if kind == 'PTE_SAME64_ESTABLISHED':
            row = fields(line, kind, ('cpu', 'a_then_b', 'a_reentered', 'b_kernel_only', 'shared_pte', 'distinct_upper'))
            if established or epoch != 1 or results or row != dict(cpu=0, a_then_b=1, a_reentered=1, b_kernel_only=1, shared_pte=1, distinct_upper=1):
                raise ValueError('keeper establishment missing/duplicated/wrong')
            established = True
        elif kind == 'PTE_SAME64_RESULT':
            row = fields(line, kind, ('epoch', 'cpu', 'warm', 'outcome', 'value', 'rip', 'cr2', 'err'), ('outcome',))
            if not established or epoch > len(PHASES) or epoch == 18 or row['epoch'] != epoch or row['cpu'] not in peers or row['cpu'] in results:
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
        elif kind == 'PTE_SAME64_AD':
            row = fields(line, kind, ('round', 'cpu', 'first_use', 'read_write_attempts', 'pte_clears',
                                     'final_shootdown', 'final_accessed', 'final_dirty', 'pfn_permissions', 'value', 'failures'))
            expected = dict(round=ad_rounds+1, cpu=1, first_use=1, read_write_attempts=65, pte_clears=65,
                            final_shootdown=1, final_accessed=1, final_dirty=1, pfn_permissions=1, value=DATA_VALUE, failures=0)
            if not established or epoch != 18 or ad_rounds >= AD_ROUNDS or row != expected:
                raise ValueError('A/D round generation/quiescent proof mismatch')
            ad_rounds += 1
            if ad_rounds == AD_ROUNDS:
                results.add(1)
        elif kind == 'PTE_SAME64_PHASE':
            row = fields(line, kind, ('epoch', 'case', 'peers', 'writes', 'completed'), ('case',))
            if not established or epoch > len(PHASES) or results != peers:
                raise ValueError('phase lacks every peer result')
            name, warm, value, error, stores = PHASES[epoch - 1]
            if row != dict(epoch=epoch, case=name, peers=cpus - 1, writes=stores, completed=cpus - 1):
                raise ValueError('phase identity/store/completion count mismatch')
            results.clear()
            epoch += 1
        elif kind == 'PTE_SAME64_CPU':
            row = fields(line, kind, ('cpu', 'role', 'ready_epoch', 'ack_epoch', 'done_epoch', 'parked', 'failures'), ('role',))
            cpu = row['cpu']
            if epoch != len(PHASES) + 1 or results or cpu >= cpus or cpu in summaries:
                raise ValueError('CPU summary missing phases/duplicate identity')
            expected = dict(cpu=cpu, role='writer' if cpu == 0 else 'reader', ready_epoch=19 if cpu else 0,
                            ack_epoch=19 if cpu else 0, done_epoch=19, parked=1, failures=0)
            if row != expected:
                raise ValueError('CPU generation/quiescence/failure mismatch')
            summaries.add(cpu)
        elif kind == 'PTE_SAME64_DONE':
            row = fields(line, kind, ('schema', 'cpus', 'writer', 'epochs', 'failures'))
            if epoch != len(PHASES) + 1 or results or summaries != set(range(cpus)) or row != dict(schema=1, cpus=cpus, writer=0, epochs=19, failures=0):
                raise ValueError('guest terminal summary incomplete')
            done = True
        else:
            raise ValueError('unknown/failure guest record')
    if not done or ad_rounds != AD_ROUNDS:
        raise ValueError('missing terminal guest record')
    return {'epochs': len(PHASES), 'peer_results': (len(PHASES)-1) * (cpus - 1), 'first_use_ad_rounds': ad_rounds,
            'quiescent_ad_pfn_permissions_data_checks': ad_rounds, 'all_cpu_generations_and_quiescence': True}


def host_keeper_final(stderr, cpus, guard):
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


def host_same64_final(stderr, cpus, mode):
    seen_cpu = set()
    records = {}
    for line in lines(stderr):
        if line.startswith('SMP_FINAL '):
            match = re.match(r'SMP_FINAL cpu ([0-9]+) ', line)
            if match:
                seen_cpu.add(uint(match[1]))
        elif line.startswith('PTE_SAME64_FINAL'):
            match = re.fullmatch(r'PTE_SAME64_FINAL cpu ([0-9]+) enabled ([0-9]+) exchanges ([0-9]+) equal ([0-9]+) changed ([0-9]+) leafSkips ([0-9]+) xmarkSkips ([0-9]+)', line)
            if not match:
                raise ValueError('malformed same64 final record')
            cpu, enabled, exchanges, same, changed, leaf, xmark = map(uint, match.groups())
            if cpu not in seen_cpu or cpu >= cpus or cpu in records or enabled != mode:
                raise ValueError('same64 CPU/order/mode identity mismatch')
            if exchanges != same + changed or xmark > leaf:
                raise ValueError('same64 counter classifications inconsistent')
            if not mode and any((exchanges, same, changed, leaf, xmark)):
                raise ValueError('OFF control contains same64 activity')
            records[cpu] = dict(enabled=enabled, exchanges=exchanges, exact_equal=same, changed=changed,
                                nonempty_leaf_skips=leaf, xmark_skips=xmark)
        elif line.startswith('PTE_SAME64_'):
            raise ValueError('unknown same64 host record')
    if set(records) != set(range(cpus)):
        raise ValueError('missing same64 final CPU record')
    if mode and any(records[0][key] <= 0 for key in ('exchanges', 'exact_equal', 'changed', 'nonempty_leaf_skips', 'xmark_skips')):
        raise ValueError('writer same64 exchange/equal/changed/leaf/xmark paths not demonstrated')
    # leaf skips count mirrors, so leaf_skips <= exact_equal is NOT an invariant.
    return records


def qualify(stdout, stderr, exit_code, cpus, same64):
    if type(cpus) is not int or cpus != 2 or type(same64) is not int or same64 not in (0, 1):
        raise ValueError('exact2CPUs and same64 mode0/1 required')
    if type(exit_code) is not int or exit_code != 0:
        raise ValueError('runner terminal exit0 required')
    guest = functional(stdout, cpus)
    # Isolate the new prototype: the separate inactive-keeper guard stays OFF.
    keeper = host_keeper_final(stderr, cpus, 0)
    same = host_same64_final(stderr, cpus, same64)
    return {'result': 'PTE_SAME64_TARGETED_FUNCTIONAL_AND_PATH_GATES_PASS', 'cpus': cpus, 'same64': same64,
            'writer': 0, 'guest': guest, 'keeper_final_by_cpu': keeper, 'pte_same64_final_by_cpu': same,
            'shared_directories_are_global_metadata_not_summed': True,
            'scope': 'targeted standalone2CPU controls and A/D attempts; no forced-interleaving, speed or production-default qualification'}

def sample(cpus=2, same64=1):
    out = ['PTE_SAME64_ESTABLISHED cpu=0 a_then_b=1 a_reentered=1 b_kernel_only=1 shared_pte=1 distinct_upper=1']
    for epoch, (name, warm, value, error, stores) in enumerate(PHASES, 1):
        if epoch == 18:
            for round_number in range(1, AD_ROUNDS+1):
                out.append('PTE_SAME64_AD round=%d cpu=1 first_use=1 read_write_attempts=65 pte_clears=65 final_shootdown=1 final_accessed=1 final_dirty=1 pfn_permissions=1 value=%d failures=0' % (round_number, DATA_VALUE))
        else:
            fault = value is None
            out.append('PTE_SAME64_RESULT epoch=%d cpu=1 warm=%d outcome=%s value=%d rip=%d cr2=%d err=%d' %
                       (epoch, warm, 'fault' if fault else 'value', 0 if fault else value,
                        FAULT_ADDRESS if fault else 0, FAULT_ADDRESS if fault else 0, error))
        out.append('PTE_SAME64_PHASE epoch=%d case=%s peers=1 writes=%d completed=1' % (epoch, name, stores))
    out += ['PTE_SAME64_CPU cpu=0 role=writer ready_epoch=0 ack_epoch=0 done_epoch=19 parked=1 failures=0',
            'PTE_SAME64_CPU cpu=1 role=reader ready_epoch=19 ack_epoch=19 done_epoch=19 parked=1 failures=0',
            'PTE_SAME64_DONE schema=1 cpus=2 writer=0 epochs=19 failures=0']
    err = ['SMP_JOIN expected 2 stopped 2']
    for cpu in range(cpus):
        err.append('SMP_FINAL cpu %d stopped 1 outcome exited exit 0 guard 0 suppressed 0 thresholdFallbacks 0 sharedDirsGlobal 1' % cpu)
        # Deliberately leaf120 > equal80: one exchange may visit many mirrors.
        counts = (100, 80, 20, 120, 40) if same64 and cpu == 0 else (0, 0, 0, 0, 0)
        err.append('PTE_SAME64_FINAL cpu %d enabled %d exchanges %d equal %d changed %d leafSkips %d xmarkSkips %d' % ((cpu, same64) + counts))
    return '\n'.join(out)+'\n', '\n'.join(err)+'\n'


def self_test():
    checks = 0
    for mode in (0, 1):
        a, b = sample(same64=mode)
        assert qualify(a, b, 0, 2, mode)['guest']['first_use_ad_rounds'] == AD_ROUNDS
        checks += 1
    out, err = sample()
    def reject(a=out, b=err, code=0, cpus=2, mode=1):
        nonlocal checks
        try:
            qualify(a, b, code, cpus, mode)
        except ValueError:
            checks += 1
        else:
            raise AssertionError('unqualified fixture accepted')
    reject(code=1); reject(code=True); reject(cpus=4); reject(cpus=True); reject(mode=True)
    reject(a='PASS\n'); reject(a=''); reject(b=''); reject(a=out[:-1]); reject(b=err[:-1])
    reject(a=out+out); reject(b=err+err); reject(a=out+'PTE_SAME64_FAIL reason=bad\n')
    for old, new in [('a_reentered=1','a_reentered=0'), ('b_kernel_only=1','b_kernel_only=0'),
                     ('case=same_value_preserve','case=remap'), ('warm=17 outcome=value value=17','warm=17 outcome=value value=33'),
                     ('writes=65','writes=64'), ('writes=8320','writes=8319'),
                     ('err=21','err=20'), ('cr2=%d'%FAULT_ADDRESS,'cr2=1'),
                     ('ready_epoch=19','ready_epoch=18'), ('ack_epoch=19','ack_epoch=18'),
                     ('done_epoch=19','done_epoch=18'), ('parked=1','parked=0'), ('failures=0','failures=1'),
                     ('round=1 cpu=1','round=2 cpu=1'), ('round=1 cpu=1','round=1 cpu=0'),
                     ('first_use=1','first_use=0'), ('read_write_attempts=65','read_write_attempts=64'),
                     ('pte_clears=65','pte_clears=64'), ('final_shootdown=1','final_shootdown=0'), ('final_accessed=1','final_accessed=0'),
                     ('final_dirty=1','final_dirty=0'), ('pfn_permissions=1','pfn_permissions=0'),
                     ('value=305419896','value=1')]:
        reject(a=out.replace(old,new,1))
    reject(a=out.replace('round=1 cpu=1','round=1 cpu=1 cpu=1',1))
    reject(a=out.replace('warm=17','warm=18446744073709551616',1));reject(a=out+'x'*MAX_LOG_BYTES+'\n')
    for prefix in ('PTE_SAME64_ESTABLISHED ', 'PTE_SAME64_RESULT ', 'PTE_SAME64_PHASE ', 'PTE_SAME64_AD ', 'PTE_SAME64_CPU ', 'PTE_SAME64_DONE '):
        rows=out.splitlines();i=next(i for i,line in enumerate(rows)if line.startswith(prefix))
        reject(a='\n'.join(rows[:i]+rows[i+1:])+'\n')
        reject(a='\n'.join(rows[:i]+[rows[i]]+rows[i:])+'\n')
    # A/D records cannot occur before their owned phase or after terminal output.
    rows=out.splitlines();i=next(i for i,line in enumerate(rows)if line.startswith('PTE_SAME64_AD '))
    reject(a='\n'.join([rows[i]]+rows[:i]+rows[i+1:])+'\n')
    for old,new in [('SMP_JOIN expected 2 stopped 2','SMP_JOIN expected 2 stopped 1'),
                    ('cpu 1 stopped','cpu 0 stopped'), ('outcome exited','outcome boundary'),
                    ('exit 0','exit -1'), ('guard 0','guard 1'),
                    ('suppressed 0','suppressed 1'), ('sharedDirsGlobal 1','sharedDirsGlobal 0'),
                    ('cpu 1 enabled','cpu 0 enabled'), ('enabled 1','enabled 0'),
                    ('exchanges 100','exchanges 99'), ('equal 80','equal 79'),
                    ('changed 20','changed 0'), ('leafSkips 120','leafSkips 0'),
                    ('xmarkSkips 40','xmarkSkips 121')]:
        reject(b=err.replace(old,new,1))
    rows=err.splitlines();reject(b='\n'.join(rows[:-1])+'\n')
    same_index=next(i for i,line in enumerate(rows)if line.startswith('PTE_SAME64_FINAL '))
    reject(b='\n'.join(rows[:same_index]+[rows[same_index]]+rows[same_index:])+'\n')
    reject(b='\n'.join([rows[same_index]]+rows[:same_index]+rows[same_index+1:])+'\n')
    reject(b=err+'engine stop: error\n');reject(b=err+'watchdog deadline\n');reject(b=err+'[truncated]\n')
    reject(b=err+'PTE_SAME64_UNKNOWN 1\n')
    a,b=sample(same64=0);reject(a=a,b=b.replace('equal 0 changed 0','equal 1 changed 0',1),mode=0)
    reject(a=a,b=b.replace('leafSkips 0 xmarkSkips 0','leafSkips 1 xmarkSkips 0',1),mode=0)
    print(json.dumps({'result':'PASS','ram_checks':checks,'guest_or_native_execution':False,
                      'scope':'checker acceptance/rejection only; no runtime race/path proof'},sort_keys=True))

def read_log(path):
    with Path(path).open('rb') as stream:
        data = stream.read(MAX_LOG_BYTES + 1)
    if len(data) > MAX_LOG_BYTES:
        raise ValueError('fixture log exceeds1MiB budget')
    return data.decode('ascii', 'strict')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('logs', nargs='*')
    parser.add_argument('--cpus', type=int, choices=(2,))
    parser.add_argument('--same64', type=int, choices=(0, 1))
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        if args.logs or args.cpus is not None or args.same64 is not None:
            parser.error('--self-test takes no log/mode arguments')
        self_test()
        return
    if len(args.logs) != 3 or args.cpus is None or args.same64 is None:
        parser.error('expected STDOUT STDERR EXIT_CODE --cpus2 --same640/1')
    try:
        result = qualify(read_log(args.logs[0]), read_log(args.logs[1]), int(args.logs[2]), args.cpus, args.same64)
    except (ValueError, OSError) as error:
        parser.exit(1, 'INCONCLUSIVE ' + str(error) + '\n')
    print(json.dumps(result, sort_keys=True))


if __name__ == '__main__':
    main()
