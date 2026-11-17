#!/usr/bin/env python3
"""Test production cross-key tail generation as data, on either host architecture.

Uses the real encoders and link functions. Cache maintenance is observed, not
executed. This is not a replacement for actual Engine/guest execution tests.
"""
from pathlib import Path
import argparse
import re
import subprocess


def function(source, name):
    found = re.search(r'(?m)^(?:static )?[^;{\n]+\b'+name+r'\([^;{]*\)\s*\{', source)
    if not found:
        raise ValueError('missing production function: '+name)
    depth, at = 1, found.end()
    while depth:
        depth += (source[at] == '{') - (source[at] == '}')
        at += 1
    return source[found.start():at]


PREFIX = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tr_internal.h"
#undef CC_PART2_WORD
#define CC_PART2_WORD 2048u
#undef BLOCKS_MAX
#define BLOCKS_MAX 8u
#define BLOCK_BASE(part) ((part) ? BLOCKS_MAX : 0)
typedef struct { u64 rip; u32 view, entry_word; } HostBlock;
static HostBlock hblocks[BLOCKS_MAX * 2];
static u32 hblock_count[2];
static u32 storage[4096], *cc_base = storage, *cc_cursor[2];
u32 *tr_cc;
int cur_part;
static TrStats stats;
u64 tr_stub_lookup = PRIV_VA + OFF_CC + 4000 * 4;
u64 tr_cc_va(const u32 *p) { return PRIV_VA + OFF_CC + (u64)(p - cc_base) * 4; }
static u32 *cc_host(u64 p) { return cc_base + (p - PRIV_VA - OFF_CC) / 4; }
static void *invalidated[8];
static size_t lengths[8];
static unsigned invalidations;
static void record_cache(void *address, size_t bytes) {
    assert(invalidations < 8);
    invalidated[invalidations] = address; lengths[invalidations++] = bytes;
}
#define sys_icache_invalidate record_cache
'''

CHECKS = r'''
static void reset(void) {
    memset(storage, 0, sizeof(storage)); memset(&stats, 0, sizeof(stats));
    memset(hblocks, 0, sizeof(hblocks));
    cc_cursor[0] = storage + 512; cc_cursor[1] = storage + 2560;
    hblock_count[0] = hblock_count[1] = 1;
    hblocks[0] = (HostBlock){.rip=0x7fff10000000ULL, .view=0x40000001, .entry_word=32};
    hblocks[8] = (HostBlock){.rip=0x7fff50000000ULL, .view=7, .entry_word=2080};
    invalidations = 0;
}
static u32 svc_word(int kind) { return 0xD4000001u | (u32)kind << 5; }
static void tail(int kind, int part) {
    reset();
    u32 *site = storage + (part ? 2084 : 36), *start = cc_cursor[part];
    site[0] = kind == SVC_CALL ? 0x94000003u : svc_word(kind);
    site[1] = 0x12345678; site[2] = 0x76543210; site[3] = 0x42;
    const u64 target = 0x7FFF12345678ULL;
    tr_lookup_tail(tr_cc_va(site), target);
    assert((site[0] & 0xFC000000u) == (kind == SVC_CALL ? 0x94000000u : 0x14000000u));
    assert(site[1] == 0x12345678 && site[2] == 0x76543210 && site[3] == 0x42);
    assert(invalidations == 2 && invalidated[0] == start && lengths[0] == 28);
    assert(invalidated[1] == site && lengths[1] == 4);
    assert(cc_cursor[part] == start + 7);
    assert(stats.call_tails == (kind == SVC_CALL) && stats.jump_tails == (kind != SVC_CALL));
    // Actual emitted words: materialize x86 RIP, preserve LR across the normal
    // per-view lookup, then BR its result. No flag-setting encodings.
    assert(start[0] == (0xD2800000u | 0x5678u << 5 | S0));
    assert(start[1] == (0xF2800000u | 1u << 21 | 0x1234u << 5 | S0));
    assert(start[2] == (0xF2800000u | 2u << 21 | 0x7fffu << 5 | S0));
    assert(start[3] == (0xAA0003E0u | LR << 16 | S4));
    assert((start[4] & 0xFC000000u) == 0x94000000u);
    assert(start[5] == (0xAA0003E0u | S4 << 16 | LR));
    assert(start[6] == (0xD61F0000u | S2 << 5));
    // A second jump request must not rewrite the new B tail or allocate
    // another. Calls retain their original BL-site behavior.
    if (kind != SVC_CALL) {
        tr_lookup_tail(tr_cc_va(site), target);
        assert(cc_cursor[part] == start + 7 && invalidations == 2);
    }
}
int main(void) {
    for (int part = 0; part < 2; part++) {
        tail(SVC_CALL, part); tail(SVC_DIRECT, part); tail(SVC_JCC, part);
    }
    const u32 rejected[] = {0xD503201Fu, 0x14000001u, 0, svc_word(SVC_DEAD), svc_word(SVC_SINGLE_DONE)};
    for (unsigned i = 0; i < sizeof(rejected)/sizeof(rejected[0]); i++) {
        reset(); storage[36] = rejected[i];
        tr_lookup_tail(tr_cc_va(storage + 36), 0x7fff50000000ULL);
        assert(storage[36] == rejected[i] && cc_cursor[0] == storage + 512 && !invalidations);
    }
    for (int kind = SVC_DIRECT; kind <= SVC_CALL; kind++) {
        reset(); storage[36] = kind == SVC_CALL ? 0x94000001u : svc_word(kind);
        tr_link(tr_cc_va(storage + 36), kind, tr_cc_va(storage + 2080), 7);
        assert(invalidations == 2 && (stats.call_tails + stats.jump_tails) == 1);
    }
    reset(); storage[36] = svc_word(SVC_DIRECT);
    tr_link(tr_cc_va(storage + 36), SVC_DIRECT, tr_cc_va(storage + 2080), hblocks[0].view);
    assert(invalidations == 1 && !stats.call_tails && !stats.jump_tails);
    puts("PASS actual cross-key call/direct/conditional tail encodings, LR/flags preservation, both cache parts, exact-site guards and same-key links");
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path, help='existing isolated scratch directory')
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[1]/'Sources/CEngine'
    translation = (source/'translate.c').read_text()
    body = '\n\n'.join((PREFIX, function((source/'emit_int.c').read_text(), 'movimm'),
                         *(function(translation, name) for name in ('block_at', 'tr_lookup_tail', 'tr_link')),
                         CHECKS))
    code = args.output/'lookup-tail-check.c'
    code.write_text(body)
    program = args.output/'lookup-tail-check'
    subprocess.run(['xcrun', 'clang', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-I'+str(source), '-I'+str(source/'include'), str(code), '-o', str(program)], check=True)
    subprocess.run([str(program)], check=True)


if __name__ == '__main__':
    main()
