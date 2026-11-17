#!/usr/bin/env python3
"""Emit a small actual-C flush/reuse fixture; generated ARM words remain DATA.

python3 Tests/test-retain-shared-dead.py --emit-c /private/tmp/retain-dead.c
nice -n10 clang -O2 -std=c11 -Wall -Wextra -Werror /private/tmp/retain-dead.c -o /private/tmp/retain-dead
nice -n10 /private/tmp/retain-dead
nice -n10 /private/tmp/retain-dead --baseline-negative-control  # must exit1

Only libc and native uint32 atomics execute. No guest/Engine/generated code runs.
Fetch permission walks and instruction-cache maintenance are observable stubs.
The original flush function is retained as a deliberate positive-test negative control.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ENGINE = Path(__file__).resolve().parents[1]
BASELINE = 'bfc0c5e4597895143a162a6476bcf4be8861a5e0'


def function(source, name):
    found = re.search(r'(?m)^(?:static )?(?:inline )?[^;{\n]+\b' + name + r'\([^;{]*\)\s*\{', source)
    if not found:
        raise ValueError('missing actual function: ' + name)
    at, depth = found.end(), 1
    while depth:
        depth += (source[at] == '{') - (source[at] == '}')
        at += 1
    return source[found.start():at]


def emit(path):
    source = (ENGINE/'Sources/CEngine/translate.c').read_text()
    header = ENGINE/'Sources/CEngine/include/translator.h'
    before = subprocess.check_output(['git', 'show', BASELINE+':Source/Engine/Sources/CEngine/translate.c'], cwd=ENGINE, text=True)
    unchanged = ('part_full', 'dead_slot', 'reuse_rejected', 'reuse_dead_block', 'link_block',
                 'map_hash', 'map_find', 'map_insert', 'page_slot', 'ram_at', 'tr_cc_va', 'membership_clear_all')
    for name in unchanged:
        if function(source, name) != function(before, name):
            raise ValueError('fixture review required: ' + name + ' changed')
    old_flush = function(before, 'tr_flush_parked')
    expected = old_flush.replace('memset(dead_slots, 0, DEAD_SLOTS * sizeof(u32));', 'flush_dead_slots(everything);')
    if function(source, 'tr_flush_parked') != expected:
        raise ValueError('fixture expects only the reviewed flush call-site change')
    if 'bool retain_shared_dead;' not in header.read_text():
        raise ValueError('actual config field missing')
    typedefs = []
    for name in ('HostBlock', 'MapEntry'):
        rows = re.findall(r'typedef struct\s*\{[^}]*\}\s*'+name+r';', source)
        if len(rows) != 1:
            raise ValueError('actual typedef identity: ' + name)
        typedefs.append(rows[0])
    body_names = ('tr_cc_va', 'map_hash', 'map_find', 'map_insert', 'ram_at', 'page_slot', 'part_full',
                  'dead_slot', 'flush_dead_slots', 'reuse_rejected', 'reuse_dead_block', 'link_block',
                  'membership_clear_all', 'tr_flush_parked')
    bodies = '\n\n'.join(function(source, name) for name in body_names)
    baseline = old_flush.replace('void tr_flush_parked(', 'void baseline_flush_parked(', 1)
    data = PREFIX.replace('HEADER', json.dumps(str(header))).replace('TYPEDEFS', '\n'.join(typedefs)) + bodies + '\n' + baseline + CHECKS
    path.write_text(data)
    receipt = {'schema': 1, 'source': str(ENGINE/'Sources/CEngine/translate.c'),
               'source_sha256': hashlib.sha256(source.encode()).hexdigest(), 'baseline': BASELINE,
               'actual_functions': list(body_names), 'actual_typedefs': ['HostBlock', 'MapEntry'],
               'generated_c_sha256': hashlib.sha256(data.encode()).hexdigest(),
               'generated_c_bytes': len(data.encode()), 'generated_guest_words_executed': False,
               'limitations': ['Tiny storage constants replace capacities, not flush/reuse/link/map logic.',
                              'Fetch permission/frame and cache-maintenance edges are stubs; generated words are uint32 data.',
                              'Single-thread native CAS checks cannot prove SMP interleavings or live guest behavior.',
                              'tr_init, actual allocation and real hash refill/membership publication are not executed.']}
    path.with_suffix('.json').write_text(json.dumps(receipt, indent=2)+'\n')
    print(json.dumps(receipt, sort_keys=True))


PREFIX = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include HEADER
typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;
#undef BLOCKS_MAX
#define BLOCKS_MAX 8u
#undef CC_PART2_WORD
#define CC_PART2_WORD 256u
#undef OFF_PCMAP2
#define OFF_PCMAP2 (OFF_PCMAP + 256u)
#undef HASH_BYTES
#define HASH_BYTES 32u
#define MAP_BITS 4
#define MAP_SIZE (1u << MAP_BITS)
#define PAGE_HEADS 16u
#define DEAD_SLOTS 16u
#define READ_COPY_BYTES 512u
#define MAX_INSNS 4
#define PCMAP_ENTRIES 64u
#define KEY_DOMAIN 0x40000000u
#define WORD_NOP 0xD503201Fu
#define WORD_DEAD (0xD4000001u | SVC_DEAD << 5)
TYPEDEFS
static unsigned checks;
static void check(int ok, const char *why, int line) {
    checks++;
    if (!ok) { fprintf(stderr,"FAIL actual-C retain fixture line=%d: %s\n",line,why); exit(1); }
}
#define CHECK(x) check((x),#x,__LINE__)
static EngineCtx ctx;
static EngineCtx *ctx_ptr=&ctx;
static TrConfig cfg;
static TrStats stats;
static HostBlock block_storage[BLOCKS_MAX*2],*hblocks=block_storage;
static MapEntry maps[MAP_SIZE],*block_map=maps;
static u32 words[CC_PART2_WORD*2],*cc_base=words,*cc_cursor[2];
static u64 cc_limit;
static u32 hblock_count[2],pc_map_used[2],read_copied[2];
static u32 dead_storage[DEAD_SLOTS],*dead_slots=dead_storage;
static u32 frame_storage[4],*frame_head=frame_storage,page_storage[PAGE_HEADS],*page_head=page_storage;
static u32 view_head[MAX_VIEWS],hash_slots_used[MAX_VIEWS];
static u64 hashes[3][HASH_BYTES/sizeof(u64)];
static u64 membership[BLOCKS_MAX][MAX_VIEWS/64],(*shared_hash_views)[MAX_VIEWS/64]=membership;
static u8 ram[4*4096],copies[READ_COPY_BYTES],*read_copies=copies;
static int view_high=2;
static bool tr_compat,fetch_ok,verify_clear_order,expect_full;
static u64 fetch_gpa;
static unsigned fetch_count,bytes_count,cas_count,icache_count;
static u32 expected_shared_count,expected_private_count;
static char trace[32];static size_t trace_n;
static void event(char c) { CHECK(trace_n<sizeof(trace)-1);trace[trace_n++]=c;trace[trace_n]=0; }
static bool fetch_walk(u64 rip,u64 *gpa) { (void)rip;fetch_count++;event('F');*gpa=fetch_gpa;return fetch_ok; }
static int observed_memcmp(const void *a,const void *b,size_t n) { bytes_count++;event('B');return memcmp(a,b,n); }
static bool observed_cas(u32 *p,u32 *old,u32 value,bool weak,int success,int failure) {
    cas_count++;event('C');return __atomic_compare_exchange_n(p,old,value,weak,success,failure);
}
static void sys_icache_invalidate(void *p,size_t bytes) {
    CHECK((u32 *)p>=words&&(u32 *)p<words+CC_PART2_WORD*2&&bytes==4);icache_count++;event('I');
}
static u64 *hash_table(int view) {
    CHECK(view>=0&&view<=view_high);
    if (verify_clear_order) {
        CHECK(hblock_count[0]==expected_shared_count&&hblock_count[1]==expected_private_count);
        for(u32 i=0;i<DEAD_SLOTS;i++) {
            u32 hint=dead_slots[i];
            if(expect_full||!cfg.retain_shared_dead) CHECK(hint==0);
            else CHECK(hint<=hblock_count[0]&&hint<=BLOCKS_MAX);
        }
    }
    return hashes[view];
}
#define memcmp observed_memcmp
#define __atomic_compare_exchange_n observed_cas
'''

CHECKS = r'''
#undef memcmp
#undef __atomic_compare_exchange_n
static u32 key=KEY_DOMAIN|1u;
static u32 primary_slot,collision_slot,live_slot,private_slot,invalid_slot;
static u64 winner_rip;
static void clear_trace(void) { memset(trace,0,sizeof trace);trace_n=0;fetch_count=bytes_count=cas_count=icache_count=0; }
static void block(u32 index,u64 rip,bool dead) {
    HostBlock *b=&hblocks[index];memset(b,0,sizeof *b);
    b->rip=rip;b->view=key;b->entry_word=8+index*8;b->frame[0]=0x1000;b->frame[1]=~0ULL;
    b->dead=dead;b->registered=true;b->on_frame=b->on_page=b->on_view=true;
    b->cpl=3;b->read_bytes=4;b->read_copy=1+index*8;b->first_word=WORD_NOP;
    b->next_on_frame[0]=b->next_on_page[0]=b->next_in_view=BLOCKS_MAX+1; // stale PRIVATE links
    words[b->entry_word]=dead?WORD_DEAD:WORD_NOP;
}
static void seed(bool enabled) {
    memset(&ctx,0,sizeof ctx);memset(&cfg,0,sizeof cfg);memset(&stats,0,sizeof stats);
    memset(hblocks,0,sizeof block_storage);memset(maps,0,sizeof maps);memset(words,0,sizeof words);
    memset(dead_slots,0,sizeof dead_storage);memset(frame_head,0,sizeof frame_storage);
    memset(page_head,0,sizeof page_storage);memset(view_head,0,sizeof view_head);
    memset(hashes,0x5a,sizeof hashes);memset(hash_slots_used,0x11,sizeof hash_slots_used);memset(membership,0x7f,sizeof membership);
    memset(ram,0xa5,sizeof ram);memset(copies,0xa5,sizeof copies);
    cfg.ram=ram;cfg.ram_size=sizeof ram;cfg.profile_invalidations=true;cfg.retain_shared_dead=enabled;
    stats.retain_shared_dead_enabled=enabled;ctx.vcpu_count=2;ctx.block_count=80;ctx.block_count_private=20;
    cc_limit=0x40000+sizeof words;cc_cursor[0]=words+80;cc_cursor[1]=words+CC_PART2_WORD+20;
    hblock_count[0]=4;hblock_count[1]=1;pc_map_used[0]=4;pc_map_used[1]=65;read_copied[0]=73;read_copied[1]=300;
    block(0,0x4000,true);primary_slot=dead_slot(key,hblocks[0].rip);
    winner_rip=0x6000;while(dead_slot(key,winner_rip)==primary_slot)winner_rip+=4096;
    collision_slot=dead_slot(key,winner_rip);u64 lost_rip=winner_rip+4096;
    for(u32 i=0;i<128&&dead_slot(key,lost_rip)!=collision_slot;i++)lost_rip+=4096;
    CHECK(dead_slot(key,lost_rip)==collision_slot&&winner_rip!=lost_rip);
    // The lower allocation index died LAST. Rebuilding hints by allocation
    // order would incorrectly replace it with the already-lost later index.
    block(2,winner_rip,true);block(3,lost_rip,true);
    u64 live=0x5000;
    while(dead_slot(key,live)==primary_slot||dead_slot(key,live)==collision_slot)live+=4096;
    block(1,live,false);live_slot=dead_slot(key,live);
    block(BLOCKS_MAX,0x9000,true);hblocks[BLOCKS_MAX].view=1;
    private_slot=0;while(private_slot==primary_slot||private_slot==collision_slot||private_slot==live_slot)private_slot++;
    invalid_slot=private_slot+1;while(invalid_slot==primary_slot||invalid_slot==collision_slot||invalid_slot==live_slot)invalid_slot++;
    CHECK(invalid_slot<DEAD_SLOTS);
    dead_slots[primary_slot]=1;dead_slots[live_slot]=2;dead_slots[collision_slot]=3;
    dead_slots[private_slot]=BLOCKS_MAX+1;dead_slots[invalid_slot]=5;
    map_insert((int)key,hblocks[0].rip,1);map_insert((int)key,hblocks[1].rip,2);map_insert(1,hblocks[BLOCKS_MAX].rip,BLOCKS_MAX+1);
    frame_head[1]=BLOCKS_MAX+1;page_head[page_slot(hblocks[0].rip)]=BLOCKS_MAX+1;view_head[1]=BLOCKS_MAX+1;
    fetch_ok=true;fetch_gpa=0x1000;tr_compat=false;verify_clear_order=false;clear_trace();
}
static void flush(bool all,bool baseline) {
    expected_shared_count=hblock_count[0];expected_private_count=hblock_count[1];expect_full=all||part_full(0);verify_clear_order=true;
    if(baseline)baseline_flush_parked(all);else tr_flush_parked(all);
    verify_clear_order=false;
}
static unsigned chain_count(u32 first,u32 wanted,bool frame) {
    unsigned count=0;u32 seen=0;
    for(u32 it=first;it;) {
        CHECK(it<=BLOCKS_MAX*2&&!(seen&(1u<<(it-1))));seen|=1u<<(it-1);
        count+=it==wanted;it=frame?hblocks[it-1].next_on_frame[0]:hblocks[it-1].next_on_page[0];
    }
    return count;
}
static unsigned map_count(u32 wanted) { unsigned n=0;for(u32 i=0;i<MAP_SIZE;i++)n+=maps[i].block==wanted;return n; }
static void clean_structures(void) {
    CHECK(ctx.tr_flushed==1&&hblock_count[1]==0&&ctx.block_count_private==0);
    CHECK(cc_cursor[1]==words+CC_PART2_WORD&&pc_map_used[1]==64&&read_copied[1]==READ_COPY_BYTES/2);
    for(u32 i=0;i<sizeof hashes/sizeof(u64);i++)CHECK(((u64 *)hashes)[i]==0);
    for(u32 i=0;i<MAX_VIEWS;i++)CHECK(hash_slots_used[i]==0&&view_head[i]==0);
    for(u32 i=0;i<sizeof membership/sizeof(u64);i++)CHECK(((u64 *)membership)[i]==0);
    CHECK(stats.invalidation_membership_reliable==1);
}
static void positive(bool baseline) {
    seed(true);flush(false,baseline);
    if(dead_slots[primary_slot]!=1) { fprintf(stderr,"FAIL retained hint survived partial flush\n");exit(1); }
    CHECK(dead_slots[primary_slot]==1&&dead_slots[collision_slot]==3&&dead_slots[live_slot]==2);
    CHECK(dead_slots[private_slot]==0&&dead_slots[invalid_slot]==0);
    CHECK(stats.shared_dead_retained_hints==3&&stats.shared_dead_cleared_hints==2);
    CHECK(hblock_count[0]==4&&cc_cursor[0]==words+80&&pc_map_used[0]==4&&read_copied[0]==73&&ctx.block_count==80);
    CHECK(stats.cache_private_resets==1&&stats.cache_resets==0);clean_structures();
    CHECK(map_find((int)key,hblocks[1].rip)==2&&map_count(2)==1&&map_find((int)key,hblocks[0].rip)==0);
    CHECK(map_find(1,hblocks[BLOCKS_MAX].rip)==0);
    CHECK(!hblocks[0].on_frame&&!hblocks[0].on_page&&!hblocks[0].on_view);
    CHECK(chain_count(frame_head[1],2,true)==1&&frame_head[1]==2);
    TrResult result={0};clear_trace();CHECK(reuse_dead_block(key,3,hblocks[0].rip,&result));
    CHECK(!hblocks[0].dead&&words[hblocks[0].entry_word]==WORD_NOP&&result.arm==tr_cc_va(words+hblocks[0].entry_word)&&result.frames[0]==0x1000);
    CHECK(strcmp(trace,"FBCI")==0&&fetch_count==1&&bytes_count==1&&cas_count==1&&icache_count==1);
    CHECK(stats.reused==1&&hblocks[0].on_frame&&hblocks[0].on_page&&!hblocks[0].on_view);
    CHECK(chain_count(frame_head[1],1,true)==1&&chain_count(frame_head[1],2,true)==1&&map_count(1)==1);
    CHECK(hblocks[0].next_on_frame[0]==2&&hblocks[0].next_on_page[0]!=BLOCKS_MAX+1);
    clear_trace();CHECK(!reuse_dead_block(key,3,hblocks[0].rip,&result));CHECK(trace_n==0&&stats.reused==1&&map_count(1)==1);
    CHECK(chain_count(frame_head[1],1,true)==1&&chain_count(page_head[page_slot(hblocks[0].rip)],1,false)==1);
    flush(false,false);clean_structures();
    CHECK(stats.shared_dead_retained_hints==6&&stats.shared_dead_cleared_hints==2);
    CHECK(map_find((int)key,hblocks[0].rip)==1&&map_count(1)==1&&map_count(2)==1);
    CHECK(chain_count(frame_head[1],1,true)==1&&chain_count(frame_head[1],2,true)==1);
    clear_trace();u64 old_reject=stats.reuse_reject[TR_REUSE_RIP];CHECK(!reuse_dead_block(key,3,hblocks[3].rip,&result));
    CHECK(stats.reuse_reject[TR_REUSE_RIP]==old_reject+1&&trace_n==0&&dead_slots[collision_slot]==3); // overwritten later-index hint stays lost
    clear_trace();CHECK(reuse_dead_block(key,3,winner_rip,&result));CHECK(map_find((int)key,winner_rip)==3&&map_count(3)==1);
    puts("PASS partial retention: same entry revived, fresh chains/map once, live rebuild, collision winner only");
}
static void off_control(void) {
    seed(false);flush(false,false);clean_structures();
    for(u32 i=0;i<DEAD_SLOTS;i++)CHECK(dead_slots[i]==0);
    CHECK(stats.shared_dead_retained_hints==0&&stats.shared_dead_cleared_hints==0);
    TrResult result={0};CHECK(!reuse_dead_block(key,3,hblocks[0].rip,&result));CHECK(stats.reuse_reject[TR_REUSE_NO_SLOT]==1&&trace_n==0);
    CHECK(map_find((int)key,hblocks[1].rip)==2&&map_count(2)==1);
    puts("PASS OFF original clearing and shared-live rebuild");
}
static void full_reset(bool escalate) {
    seed(true);if(escalate)hblock_count[0]=BLOCKS_MAX-2;
    CHECK(part_full(0)==escalate);flush(!escalate,false);clean_structures();
    for(u32 i=0;i<DEAD_SLOTS;i++)CHECK(dead_slots[i]==0);
    CHECK(stats.shared_dead_retained_hints==0&&stats.shared_dead_cleared_hints==5);
    CHECK(hblock_count[0]==0&&cc_cursor[0]==words&&pc_map_used[0]==0&&read_copied[0]==1&&ctx.block_count==0);
    CHECK(stats.cache_resets==1&&stats.cache_private_resets==0);
    // Simulate fresh index0 metadata with all reuse guards otherwise matching.
    block(0,0x4000,true);hblock_count[0]=1;TrResult result={0};clear_trace();
    CHECK(!reuse_dead_block(key,3,0x4000,&result));CHECK(trace_n==0&&stats.reuse_reject[TR_REUSE_NO_SLOT]==1&&words[hblocks[0].entry_word]==WORD_DEAD);
    puts(escalate?"PASS actual part_full(0) escalation clears before index reuse":"PASS explicit full reset clears before index reuse");
}
static void helper_boundaries(void) {
    seed(true);memset(dead_slots,0,sizeof dead_storage);hblock_count[0]=BLOCKS_MAX;
    dead_slots[0]=BLOCKS_MAX;dead_slots[1]=BLOCKS_MAX+1;dead_slots[2]=UINT32_MAX;
    flush_dead_slots(false);CHECK(dead_slots[0]==BLOCKS_MAX&&dead_slots[1]==0&&dead_slots[2]==0);
    CHECK(stats.shared_dead_retained_hints==1&&stats.shared_dead_cleared_hints==2);
    memset(dead_slots,0,sizeof dead_storage);hblock_count[0]=3;dead_slots[0]=3;dead_slots[1]=4;
    flush_dead_slots(false);CHECK(dead_slots[0]==3&&dead_slots[1]==0);
    // Defensive conjunction: even a bad shared count cannot retain private index8.
    hblock_count[0]=BLOCKS_MAX+1;dead_slots[0]=BLOCKS_MAX+1;flush_dead_slots(false);CHECK(dead_slots[0]==0);
    puts("PASS encoded current-count/BLOCKS_MAX boundaries and zero/action accounting");
}
static void rejection_guards(void) {
    for(u32 reason=0;reason<TR_REUSE_REJECT_KINDS;reason++) {
        seed(true);flush(false,false);HostBlock *b=&hblocks[0];TrResult result={0};
        switch(reason) {
        case TR_REUSE_DISABLED:cfg.no_reuse=true;break;
        case TR_REUSE_NO_SLOT:dead_slots[primary_slot]=0;break;
        case TR_REUSE_NOT_DEAD:b->dead=false;break;
        case TR_REUSE_RIP:b->rip++;break;
        case TR_REUSE_KEY:b->view++;break;
        case TR_REUSE_NO_READ_COPY:b->read_bytes=0;break;
        case TR_REUSE_CPL:b->cpl=0;break;
        case TR_REUSE_COMPAT:b->compat=true;break;
        case TR_REUSE_FETCH:fetch_ok=false;break;
        case TR_REUSE_FRAME:fetch_gpa=0x2000;break;
        case TR_REUSE_SMP_ENTRY:b->first_word=1;break;
        case TR_REUSE_BYTES:ram[0x1000]=0;break;
        case TR_REUSE_ENTRY_CAS:words[b->entry_word]=1;break;
        }
        HostBlock old=*b;u32 word=words[b->entry_word];clear_trace();
        CHECK(!reuse_dead_block(key,3,0x4000,&result));CHECK(result.arm==0&&memcmp(&old,b,sizeof old)==0&&words[b->entry_word]==word);
        CHECK(stats.reused==0&&stats.reuse_attempts==1);
        for(u32 i=0;i<TR_REUSE_REJECT_KINDS;i++)CHECK(stats.reuse_reject[i]==(u64)(i==reason));
        CHECK(fetch_count==(reason>=TR_REUSE_FETCH)&&bytes_count==(reason>=TR_REUSE_BYTES)&&cas_count==(reason>=TR_REUSE_ENTRY_CAS)&&icache_count==0);
    }
    puts("PASS actual reuse disabled/slot/dead/RIP/key/read-copy/permission/frame/mode/SMP/bytes/CAS negatives");
}
int main(int argc,char **argv) {
    if(argc==2&&strcmp(argv[1],"--baseline-negative-control")==0) {positive(true);return 2;}
    CHECK(argc==1);positive(false);off_control();full_reset(false);full_reset(true);helper_boundaries();rejection_guards();
    printf("PASS actual-C retain-shared-dead fixture checks=%u generated_words_executed=0\n",checks);return 0;
}
'''

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--emit-c', type=Path, required=True)
    emit(parser.parse_args().emit_c)
