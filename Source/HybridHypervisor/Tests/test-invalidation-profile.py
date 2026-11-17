#!/usr/bin/env python3
"""Emit a host-only mechanism fixture from production invalidation functions. No Engine, HVF, guest or code execution.

    python3 Tests/test-invalidation-profile.py --emit-c /tmp/invalidation-profile-check.c
    nice -n 10 xcrun clang -O2 -Wall -Wextra -Werror /tmp/invalidation-profile-check.c -o /tmp/invalidation-profile-check
    /tmp/invalidation-profile-check

Clocks/cache maintenance are stubs: this tests sampling, exact hash changes and counters, NOT performance.
"""
import argparse
import json
from pathlib import Path
import re

engine = Path(__file__).resolve().parents[1]
source = (engine/'Sources/CEngine/translate.c').read_text()


def function(name):
    match = re.search(r'(?m)^(?:static )?[^;{\n]+\b'+re.escape(name)+r'\([^;{]*\)\s*\{', source)
    assert match, name
    depth, at = 1, match.end()
    while depth:
        depth += (source[at] == '{') - (source[at] == '}')
        at += 1
    return source[match.start():at]


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--emit-c', type=Path, required=True)
args = parser.parse_args()
assert 'cfg.profile_invalidations && (++stats.invalidation_profiled_kills & 255) == 0' in source
assert 'config.profile_invalidations = environment["LC_HVDBT_PROFILE_INVALIDATION"] == "1"' in (engine/'Sources/Engine/Engine.swift').read_text()
assert 'config.invalidation_membership = true' in (engine/'Sources/Engine/Engine.swift').read_text()
assert 'SVC_DEAD starts with view_lookup' in source
assert source.index('membership_clear_all(); // Both full and private-part flushes') < source.index('cc_cursor[1] = cc_base + CC_PART2_WORD;', source.index('void tr_flush_parked'))
header = json.dumps(str(engine/'Sources/CEngine/include/translator.h'))
prefix = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mach/mach_time.h>
#include HEADER
#undef HASH_ENTRIES
#define HASH_ENTRIES 64u
#define MAP_BITS 6
#define MAP_SIZE (1u << MAP_BITS)
#undef BLOCKS_MAX
#define BLOCKS_MAX 8u
#define BLOCK_BASE(part) ((part) ? BLOCKS_MAX : 0)
#define DEAD_SLOTS 16u
#define WORD_DEAD (0xD4000001u | SVC_DEAD << 5)
#define KEY_DOMAIN 0x40000000u
typedef uint64_t u64;
typedef uint32_t u32;
typedef struct { u64 rip; u32 view, block; } MapEntry;
typedef struct { u64 rip; u32 view, entry_word; bool dead, registered; uint16_t read_bytes; } HostBlock;
static TrConfig cfg;
static TrStats stats;
static mach_timebase_info_data_t invalidation_timebase = {125, 3};
static u64 table[MAX_VIEWS][HASH_ENTRIES * 2];
static u64 *hash_table(int view) { return table[view]; }
static MapEntry block_map[MAP_SIZE];
static HostBlock hblocks[BLOCKS_MAX * 2];
static u32 hblock_count[2], hash_slots_used[MAX_VIEWS];
static u64 (*shared_hash_views)[MAX_VIEWS / 64];
static u64 membership_storage[BLOCKS_MAX][MAX_VIEWS / 64];
static u32 cc_words[16], *cc_base = cc_words;
#define cc_storage cc_words[0]
static u64 tr_cc_va(const u32 *word) { return PRIV_VA + OFF_CC + (u64)(word - cc_base) * 4; }
static u32 dead_slots[DEAD_SLOTS];
static int view_high;
static u64 clock_calls, icache_calls;
static u64 fixture_clock(void) { clock_calls++; return clock_calls * 3; }
static void fixture_icache(void *entry, size_t bytes) {
    assert((u32 *)entry >= cc_words && (u32 *)entry < cc_words + 16 && bytes == 4 && *(u32 *)entry == WORD_DEAD);
    icache_calls++;
}
#define mach_absolute_time fixture_clock
#define sys_icache_invalidate fixture_icache
'''.replace('HEADER', header)
functions = '\n\n'.join(function(name) for name in (
    'map_hash', 'map_insert', 'map_remove', 'crc32c64', 'dead_slot',
    'invalidation_ns', 'block_at', 'membership_track', 'membership_clear_all', 'hash_empty', 'tr_hash_insert',
    'hash_remove', 'hash_remove_profiled', 'hash_remove_entry', 'invalidate_shared_hashes', 'kill_block', 'tr_view_forget'))
checks = r'''
static void collision_equivalence(void) {
    const u64 rip = 0x12345678;
    u32 start = crc32c64(rip) & (HASH_ENTRIES - 1);
    for (int mode = 0; mode < 3; mode++) {
        memset(table, 0, sizeof(table));
        memset(&stats, 0, sizeof(stats));
        if (mode == 2) {
            for (u32 slot = 0; slot < HASH_ENTRIES; slot++) table[0][slot * 2] = 1;
        } else {
            table[0][start * 2] = 1;
            table[0][((start + 1) & (HASH_ENTRIES - 1)) * 2] = rip + 1;
            if (mode == 0) table[0][((start + 2) & (HASH_ENTRIES - 1)) * 2] = rip;
        }
        memcpy(table[1], table[0], sizeof(table[0]));
        hash_remove(0, rip);
        hash_remove_profiled(1, rip);
        assert(!memcmp(table[0], table[1], sizeof(table[0])));
        assert(stats.invalidation_hash_view_visits == 1);
        assert(stats.invalidation_hash_probes == (mode == 2 ? HASH_ENTRIES : 3));
        assert(stats.invalidation_hash_removals == (mode == 0 ? 1 : 0));
    }
}
static void kills(bool enabled, int views, bool shared, bool registered) {
    memset(&stats, 0, sizeof(stats));
    cfg.profile_invalidations = enabled;
    clock_calls = icache_calls = 0;
    view_high = views - 1;
    for (int i = 0; i < 512; i++) {
        memset(table, 0, sizeof(table));
        memset(block_map, 0, sizeof(block_map));
        u64 rip = 0x10000 + (u64)i * 8;
        u32 key = shared ? KEY_DOMAIN : 0;
        hblocks[0] = (HostBlock){.rip = rip, .view = key, .registered = registered};
        map_insert((int)key, rip, 1);
        u32 slot = crc32c64(rip) & (HASH_ENTRIES - 1);
        table[0][slot * 2] = rip;
        if (shared && views > 1) table[views - 1][slot * 2] = rip;
        cc_storage = 0;
        kill_block(0);
        kill_block(0); // Already-dead calls must not consume sampling cadence or maintain the cache twice.
        assert(hblocks[0].dead && cc_storage == WORD_DEAD);
        if (registered) {
            assert(block_map[map_hash((int)key, rip)].block == ~0u);
            assert(table[0][slot * 2] == 1);
            if (shared && views > 1) assert(table[views - 1][slot * 2] == 1);
        } else {
            assert(block_map[map_hash((int)key, rip)].block == 1);
            assert(table[0][slot * 2] == rip);
        }
    }
    assert(icache_calls == 512 && stats.invalidated == 512);
    assert(stats.invalidation_profiled_kills == (enabled ? 512 : 0));
    assert(stats.invalidation_sampled_kills == (enabled ? 2 : 0));
    assert(stats.invalidation_sampled_shared == (enabled && shared ? 2 : 0));
    assert(stats.invalidation_sampled_registered == (enabled && registered ? 2 : 0));
    u64 visits = registered ? (shared ? views : 1) : 0;
    assert(stats.invalidation_hash_view_visits == (enabled ? 2 * visits : 0));
    assert(stats.invalidation_hash_probes == (enabled ? 2 * visits : 0));
    assert(stats.invalidation_hash_removals == (enabled && registered ? 2 * (shared && views > 1 ? 2 : 1) : 0));
    assert(stats.invalidation_map_unlink_ns == (enabled && registered ? 250 : 0));
    assert(stats.invalidation_hash_fanout_ns == (enabled && registered ? 250 : 0));
    assert(stats.invalidation_icache_ns == (enabled ? 250 : 0));
    assert(clock_calls == (enabled ? registered ? 12 : 4 : 0));
    printf("PASS enabled=%d views=%d shared=%d registered=%d sampled=%llu visits=%llu removals=%llu\n",
           enabled, views, shared, registered, (unsigned long long)stats.invalidation_sampled_kills,
           (unsigned long long)stats.invalidation_hash_view_visits, (unsigned long long)stats.invalidation_hash_removals);
}
static void membership_checks(void) {
    memset(&stats, 0, sizeof(stats));
    memset(table, 0, sizeof(table));
    memset(hash_slots_used, 0, sizeof(hash_slots_used));
    memset(block_map, 0, sizeof(block_map));
    cfg.profile_invalidations = false;
    cfg.invalidation_membership = true;
    shared_hash_views = membership_storage;
    membership_clear_all();
    view_high = 255;
    hblock_count[0] = 2;
    hblock_count[1] = 0;
    const u64 rip = 0x20000;
    hblocks[0] = (HostBlock){.rip=rip, .view=KEY_DOMAIN, .registered=true, .entry_word=0};
    hblocks[1] = (HostBlock){.rip=rip, .view=KEY_DOMAIN|1, .registered=true, .entry_word=1};
    const u64 arm_a = tr_cc_va(cc_base), arm_b = tr_cc_va(cc_base + 1);
    map_insert(KEY_DOMAIN, rip, 1);
    map_insert(KEY_DOMAIN|1, rip, 2);
    tr_hash_insert(0, rip, arm_a);
    tr_hash_insert(1, rip, arm_a);
    tr_hash_insert(1, rip, arm_b); // Same RIP, a different domain. Old membership is deliberately conservative.
    tr_hash_insert(2, rip, arm_a);
    tr_view_forget(2);
    tr_hash_insert(2, rip, arm_b); // Reusing a view must not let stale membership remove the new target.
    tr_hash_insert(3, rip, arm_a);
    hash_remove(3, rip); // Tombstone retains a stale value, which must never be treated as a live lookup.
    tr_hash_insert(4, rip, arm_a);
    hash_empty(4);
    u32 slot = crc32c64(rip) & (HASH_ENTRIES - 1);
    kill_block(0);
    assert(stats.invalidation_membership_view_visits == 5);
    assert(stats.invalidation_membership_stale_visits == 4);
    assert(stats.invalidation_membership_removals == 1);
    assert(table[0][slot*2] == 1);
    assert(table[1][slot*2] == rip && table[1][slot*2+1] == arm_b);
    assert(table[2][slot*2] == rip && table[2][slot*2+1] == arm_b);
    for (int group=0; group<4; group++) assert(shared_hash_views[0][group] == 0);
    // Model re-registration after the production reuse path's frame/byte/CAS checks: no old bitmap may be resurrected.
    hblocks[0].dead = false;
    cc_words[0] = 0xd503201f;
    map_insert(KEY_DOMAIN, rip, 1);
    tr_hash_insert(7, rip, arm_a);
    kill_block(0);
    assert(stats.invalidation_membership_view_visits == 6 && stats.invalidation_membership_removals == 2);
    assert(table[1][slot*2+1] == arm_b);
    // An interior/noncanonical ARM target makes tracking uncertain BEFORE publishing. Future kills use full scans.
    tr_hash_insert(5, rip, arm_b + 4);
    assert(!stats.invalidation_membership_reliable && stats.invalidation_membership_tracking_failures == 1);
    hblocks[0].dead = false;
    map_insert(KEY_DOMAIN, rip, 1);
    tr_hash_insert(6, rip, arm_a);
    kill_block(0);
    assert(stats.invalidation_membership_full_scans == 1 && table[6][slot*2] == 1);
    // A global hash flush (full OR private-part reset) resets all sidecar indices before any index is reused.
    for (int view=0; view<MAX_VIEWS; view++) hash_empty(view);
    membership_clear_all();
    assert(stats.invalidation_membership_reliable);
    for (u32 idx=0; idx<BLOCKS_MAX; idx++)
        for (int group=0; group<4; group++) assert(!shared_hash_views[idx][group]);
    // If allocation was unavailable, the mode stays fail-closed and uses the same full scan.
    shared_hash_views = NULL;
    stats.invalidation_membership_reliable = 0;
    hblocks[0].dead = false;
    map_insert(KEY_DOMAIN, rip, 1);
    tr_hash_insert(8, rip, arm_a);
    kill_block(0);
    assert(stats.invalidation_membership_full_scans == 2 && table[8][slot*2] == 1);
    // Exercise every bitmap word, including both sides of each64-view edge.
    shared_hash_views = membership_storage;
    for (int view=0; view<MAX_VIEWS; view++) hash_empty(view);
    membership_clear_all();
    hblocks[0].dead = false;
    cc_words[0] = 0xd503201f;
    map_insert(KEY_DOMAIN, rip, 1);
    const int edges[] = {0, 63, 64, 127, 128, 191, 192, MAX_VIEWS-1};
    const size_t edge_count = sizeof(edges)/sizeof(edges[0]);
    u64 visits = stats.invalidation_membership_view_visits;
    u64 removals = stats.invalidation_membership_removals;
    for (size_t i=0; i<edge_count; i++) {
        tr_hash_insert(edges[i], rip, arm_a);
        assert(shared_hash_views[0][edges[i]/64] & (1ULL << (edges[i]%64)));
    }
    kill_block(0);
    assert(stats.invalidation_membership_view_visits == visits+edge_count);
    assert(stats.invalidation_membership_removals == removals+edge_count);
    for (size_t i=0; i<edge_count; i++) assert(table[edges[i]][slot*2] == 1);
    for (int group=0; group<MAX_VIEWS/64; group++) assert(!shared_hash_views[0][group]);
    puts("PASS conservative membership: exact target, replacement, tombstone, forget/view reuse, revive, uncertain/failure fallback, flush reset");
    puts("PASS membership word boundaries and highest view");
}
int main(void) {
    collision_equivalence();
    for (int enabled = 0; enabled < 2; enabled++) {
        kills(enabled, 1, true, true);
        kills(enabled, 256, true, true);
        kills(enabled, 256, false, true);
        kills(enabled, 256, true, false);
    }
    membership_checks();
    return 0;
}
'''
args.emit_c.write_text(prefix+'\n'+functions+'\n'+checks)
print(f'PASS source/config invariants; emitted actual-function host mechanism fixture: {args.emit_c}')
