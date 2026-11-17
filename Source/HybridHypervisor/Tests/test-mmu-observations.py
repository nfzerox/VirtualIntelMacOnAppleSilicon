#!/usr/bin/env python3
"""Emit a host-only test of production MMU observation helpers (no Engine or guest execution).

    python3 Tests/test-mmu-observations.py --emit-c /tmp/mmu-observations.c
    nice -n 10 clang -O2 -Wall -Wextra -Werror /tmp/mmu-observations.c -o /tmp/mmu-observations
    /tmp/mmu-observations

The architectural counter is stubbed. This validates accounting, not elapsed-time accuracy or MMU optimization safety.
"""
import argparse
import json
from pathlib import Path
import re

engine = Path(__file__).resolve().parents[1]
source = (engine / 'Monitor/monitor.c').read_text()


def function(name):
    match = re.search(r'(?m)^static inline [^;{\n]+\b' + name + r'\([^;{]*\)\s*\{', source)
    assert match, name
    at, depth = match.end(), 1
    while depth:
        depth += (source[at] == '{') - (source[at] == '}')
        at += 1
    return source[match.start():at]


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--emit-c', type=Path, required=True)
args = parser.parse_args()
# The observation hooks must surround both existing walks, and must never replace store/invalidation work.
assert source.count('mmu_walk_begin(false)') == source.count('mmu_walk_end(false, walk_sample)') == 1
assert source.count('mmu_walk_begin(true)') == source.count('mmu_walk_end(true, walk_sample)') == 1
assert 'if (observe_plain)\n                mmu_observe_store(n == 8 ? 1 : 0, observed_old, val);\n            pt_slot_changed' in source
assert 'mmu_observe_store(wide ? 3 : 2, old, next);\n            pt_slot_changed' in source
assert 'next != old' in source  # Existing CAS/swap behavior remains separate.
assert 'environment["LC_HVDBT_MMU_OBSERVATIONS"] == "1" ? 1 : 0' in (engine / 'Sources/Engine/Engine.swift').read_text()
header = json.dumps(str(engine / 'Sources/CEngine/include/engine_abi.h'))
prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include HEADER
typedef uint64_t u64;
typedef uint32_t u32;
typedef struct { u64 began; bool sampled; } MmuWalkSample;
static EngineCtx state, *c = &state;
static u64 counter, counter_reads, frequency_reads;
static u64 read_cntvct_el0(void) { counter_reads++; return counter; }
static u64 read_cntfrq_el0(void) { frequency_reads++; return 24000000; }
#define RSR(name) read_##name()
'''.replace('HEADER', header)
checks = r'''
int main(void) {
    // Disabled mode has no counter reads and changes no accounting, including store observations.
    EngineCtx before = state;
    for (int i = 0; i < 1024; i++) {
        MmuWalkSample s = mmu_walk_begin(i & 1);
        assert(!s.sampled);
        mmu_walk_end(i & 1, s);
        mmu_observe_store(i & 3, 0, 0);
    }
    assert(memcmp(&before, &state, sizeof(state)) == 0);
    assert(counter_reads == 0 && frequency_reads == 0);
    state.profile_mmu_observations = 1;
    for (int i = 0; i < 512; i++) {
        counter = 1000 + i;
        MmuWalkSample s = mmu_walk_begin(false);
        assert(s.sampled == ((i & 255) == 255));
        counter += 7;
        mmu_walk_end(false, s);
    }
    assert(state.stat_mmu_first_walk_calls == 512 && state.stat_mmu_first_walk_samples == 2);
    assert(state.stat_mmu_first_walk_ticks == 14 && state.stat_mmu_locked_rewalk_calls == 0);
    assert(state.stat_mmu_counter_hz == 24000000 && frequency_reads == 1 && counter_reads == 4);
    // Zero is a valid counter value; unsigned wraparound also produces the correct delta.
    state.stat_mmu_locked_rewalk_calls = 255;
    counter = 0;
    MmuWalkSample s = mmu_walk_begin(true);
    counter = 11;
    mmu_walk_end(true, s);
    state.stat_mmu_locked_rewalk_calls = 511;
    counter = UINT64_MAX - 2;
    s = mmu_walk_begin(true);
    counter = 4;
    mmu_walk_end(true, s);
    assert(state.stat_mmu_locked_rewalk_samples == 2 && state.stat_mmu_locked_rewalk_ticks == 18);
    // W-sized operations compare actual stored low32, including LSE add wraparound.
    mmu_observe_store(0, 0x123400000007ULL, 7);
    mmu_observe_store(0, 7, 8);
    mmu_observe_store(1, 7, 0x100000007ULL);
    mmu_observe_store(1, UINT64_MAX, UINT64_MAX);
    mmu_observe_store(2, 7, 0x100000007ULL);
    mmu_observe_store(2, UINT32_MAX, 0x100000000ULL);
    mmu_observe_store(3, 0, 0);
    mmu_observe_store(3, 7, 0x100000007ULL);
    for (int i = 0; i < 4; i++) {
        assert(state.stat_mmu_store_observed[i] == 2);
        assert(state.stat_mmu_store_unchanged[i] == 1);
    }
    puts("PASS: MMU disabled accounting, independent walk sampling, zero/wrapped counter, and four operand widths");
    return 0;
}
'''
args.emit_c.write_text(prefix + '\n'.join(function(n) for n in ('mmu_walk_begin', 'mmu_walk_end', 'mmu_observe_store')) + checks)
print('Wrote', args.emit_c)
