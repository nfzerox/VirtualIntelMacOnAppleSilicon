#!/usr/bin/env python3
"""Emit a host-only fixture comparing production census behavior with the pre-census a0c12f4f reuse implementation.

    python3 Tests/test-invalidation-census.py --emit-c /tmp/invalidation-census.c
    nice -n 10 clang -O2 -Wall -Wextra -Werror /tmp/invalidation-census.c -o /tmp/invalidation-census
    /tmp/invalidation-census

No guest, Engine or executable generated code is run. fetch/bytes/CAS/cache helpers are observable stubs.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess

engine = Path(__file__).resolve().parents[1]
current = (engine / 'Sources/CEngine/translate.c').read_text()
baseline = subprocess.check_output(['git', 'show', 'a0c12f4f:Source/Engine/Sources/CEngine/translate.c'], cwd=engine, text=True)
monitor = (engine / 'Monitor/monitor.c').read_text()
baseline_monitor = subprocess.check_output(['git', 'show', 'd5207348:Source/Engine/Monitor/monitor.c'], cwd=engine, text=True)


def function(source, name):
    match = re.search(r'(?m)^(?:static )?(?:inline )?[^;{\n]+\b' + name + r'\([^;{]*\)\s*\{', source)
    assert match, name
    at, depth = match.end(), 1
    while depth:
        depth += (source[at] == '{') - (source[at] == '}')
        at += 1
    return source[match.start():at]


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--emit-c', type=Path, required=True)
args = parser.parse_args()
# Retain the original state-changing dispatch, walks, invalidation and return-stack reset sites.
assert 'if (pt_flood_decision(d, nobody_home)) {' in monitor
assert 'mmu_xmark_zap(v, d->view);\n            walk_any_tables = true;' in monitor
for name in ('pt_slot_changed', 'handle_data_abort', 'shadow_write_finish', 'pool_reclaim'):
    assert function(monitor, name) == function(baseline_monitor, name), name + ' behavior unexpectedly changed'
# Runtime policy/path coverage comes from the actual two/four-CPU and pool-
# pressure fixtures, not the spelling of an environment initializer. This
# historical census comparison is narrower than those functional tests.
header = json.dumps(str(engine / 'Sources/CEngine/include/translator.h'))
prefix = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include HEADER
typedef uint64_t u64;
typedef uint32_t u32;
#define KEY_DOMAIN 0x40000000u
#define WORD_NOP 0xd503201fu
#define WORD_DEAD (0xd4000001u | SVC_DEAD << 5)
#define PT_FLOOD_WRITES 64
typedef struct { u64 rip; u32 view,entry_word; u64 frame[2]; u32 next_on_frame[2],next_on_page[2],next_in_view;
    bool dead,on_frame,on_page,on_view,compat; int cpl; u32 first_word,read_copy; uint16_t read_bytes,read_off; } HostBlock;
typedef struct { u32 level,view,writes; u64 *parent; } ShadowDesc;
static struct {u32 keeper;} views[2];
static EngineCtx context,*ctx_ptr=&context,*c=&context;
static TrConfig cfg;
static TrStats stats;
static HostBlock hblocks[1];
static u32 dead_slots[1],frame_head[2],page_head[1],view_head[2],words[1],*cc_base=words;
static unsigned char ram[256],saved[256],*read_copies=saved;
static bool tr_compat,fetch_ok;
static u64 fetch_gpa;
static u32 zap_why;
static char trace[32];static size_t trace_n;
static void event(char c){assert(trace_n<sizeof(trace)-1);trace[trace_n++]=c;trace[trace_n]=0;}
static u32 dead_slot(u32 key,u64 rip){(void)key;(void)rip;return 0;}
static u32 page_slot(u64 rip){(void)rip;return 0;}
static unsigned char *ram_at(u64 a){assert(a>=0x1000&&a<0x1100);return ram+(a-0x1000);}
static bool fetch_walk(u64 rip,u64 *gpa){assert(rip==0x4000);event('F');*gpa=fetch_gpa;return fetch_ok;}
static int fixture_memcmp(const void *a,const void *b,size_t n){event('B');return memcmp(a,b,n);}
static bool fixture_cas(u32 *p,u32 *expected,u32 desired,bool weak,int success,int failure){
    (void)weak;(void)success;(void)failure;event('C');if(*p!=*expected){*expected=*p;return false;}*p=desired;return true;}
static void sys_icache_invalidate(void *p,size_t n){assert(p==words&&n==4);event('I');}
static void map_insert(int key,u64 rip,u32 idx){assert(key==1&&rip==0x4000&&idx==1);event('M');}
static u64 tr_cc_va(const u32 *p){return (u64)(uintptr_t)p;}
#define memcmp fixture_memcmp
#define __atomic_compare_exchange_n fixture_cas
'''.replace('HEADER', header)
functions = '\n'.join((function(current, 'reuse_rejected'), function(current, 'reuse_dead_block'),
                        function(baseline, 'reuse_dead_block').replace('reuse_dead_block(', 'reuse_baseline(', 1),
                        function(monitor, 'mmu_xmark_zap'), function(monitor, 'pt_flood_decision')))
checks = r'''
#undef memcmp
static void reset(int failure,bool enabled){
    memset(&context,0,sizeof(context));memset(&stats,0,sizeof(stats));memset(&cfg,0,sizeof(cfg));
    memset(hblocks,0,sizeof(hblocks));memset(frame_head,0,sizeof(frame_head));memset(page_head,0,sizeof(page_head));memset(view_head,0,sizeof(view_head));
    memset(ram,0,sizeof(ram));memset(saved,0,sizeof(saved));memset(trace,0,sizeof(trace));trace_n=0;
    cfg.profile_invalidations=enabled;dead_slots[0]=1;fetch_ok=true;fetch_gpa=0x1000;tr_compat=false;
    context.vcpu_count=2;words[0]=WORD_DEAD;
    HostBlock *b=hblocks;b->dead=true;b->rip=0x4000;b->view=1;b->cpl=3;b->read_bytes=4;b->frame[0]=0x1000;b->first_word=WORD_NOP;
    switch(failure){
    case TR_REUSE_DISABLED:cfg.no_reuse=true;break;
    case TR_REUSE_NO_SLOT:dead_slots[0]=0;break;
    case TR_REUSE_NOT_DEAD:b->dead=false;break;
    case TR_REUSE_RIP:b->rip++;break;
    case TR_REUSE_KEY:b->view=0;break;
    case TR_REUSE_NO_READ_COPY:b->read_bytes=0;break;
    case TR_REUSE_CPL:b->cpl=0;break;
    case TR_REUSE_COMPAT:b->compat=true;break;
    case TR_REUSE_FETCH:fetch_ok=false;break;
    case TR_REUSE_FRAME:fetch_gpa=0x2000;break;
    case TR_REUSE_SMP_ENTRY:b->first_word=1;break;
    case TR_REUSE_BYTES:ram[0]=1;break;
    case TR_REUSE_ENTRY_CAS:words[0]=1;break;
    default:break;
    }
    // Later failed conditions must not be touched after an earlier rejection.
    if(failure<TR_REUSE_FETCH)fetch_ok=false;
}
int main(void){
    for(int enabled=0;enabled<2;enabled++)for(int failure=0;failure<=TR_REUSE_REJECT_KINDS;failure++){
        TrResult a={0},b={0};reset(failure,enabled);bool old=reuse_baseline(1,3,0x4000,&a);
        HostBlock block=hblocks[0];u32 word=words[0];char oldtrace[32];memcpy(oldtrace,trace,sizeof(trace));
        u32 frame=frame_head[1],page=page_head[0],view=view_head[1];u64 reused=stats.reused;
        reset(failure,enabled);bool now=reuse_dead_block(1,3,0x4000,&b);
        assert(old==now&&memcmp(&a,&b,sizeof(a))==0&&memcmp(&block,hblocks,sizeof(block))==0);
        assert(word==words[0]&&frame==frame_head[1]&&page==page_head[0]&&view==view_head[1]&&reused==stats.reused);
        assert(strcmp(oldtrace,trace)==0);assert(stats.reuse_attempts==(u64)enabled);
        for(int i=0;i<TR_REUSE_REJECT_KINDS;i++)assert(stats.reuse_reject[i]==(u64)(enabled&&i==failure));
    }
    // Exhaustive eligibility/threshold/inactive/keeper combinations, including u32 wraparound.
    const u32 writes[]={0,63,64,UINT32_MAX};
    for(int enabled=0;enabled<2;enabled++)for(u32 level=0;level<4;level++)for(int parent=0;parent<2;parent++)
    for(int inactive=0;inactive<2;inactive++)for(int keeper=0;keeper<2;keeper++)for(size_t i=0;i<4;i++){
        memset(&context,0,sizeof(context));context.profile_mmu_observations=enabled;views[0].keeper=keeper;
        ShadowDesc a={level,0,writes[i],parent?(u64 *)words:NULL},b=a;
        bool old=a.level<3&&a.parent&&(++a.writes>PT_FLOOD_WRITES||inactive);
        bool now=pt_flood_decision(&b,inactive);assert(old==now&&a.writes==b.writes);
        u32 threshold=b.writes>PT_FLOOD_WRITES,cause=threshold?(inactive?2:0):1;
        for(u32 k=0;k<6;k++)assert(context.stat_mmu_flood_decisions[k]==(u64)(enabled&&old&&k==cause*2+(u32)keeper));
    }
    for(int enabled=0;enabled<2;enabled++)for(u32 reason=0;reason<KILL_KINDS;reason++)for(int keeper=0;keeper<2;keeper++)for(int valid=0;valid<2;valid++){
        memset(&context,0,sizeof(context));context.profile_mmu_observations=enabled;views[0].keeper=keeper;zap_why=reason;
        mmu_xmark_zap((1ULL<<55)|valid,0);
        for(u32 i=0;i<KILL_KINDS*4;i++)assert(context.stat_mmu_xmark_zaps[i]==(u64)(enabled&&i==reason*4+(u32)keeper*2+(u32)valid));
    }
    // Experimental guard alters only eligible inactive-keeper decisions below the unchanged threshold.
    for(int profile=0;profile<2;profile++)for(int guard=0;guard<2;guard++)for(u32 level=0;level<4;level++)
    for(int parent=0;parent<2;parent++)for(int inactive=0;inactive<2;inactive++)for(int keeper=0;keeper<2;keeper++)for(size_t i=0;i<4;i++){
        memset(&context,0,sizeof(context));context.profile_mmu_observations=profile;context.keeper_inactive_guard=guard;views[0].keeper=keeper;
        ShadowDesc a={level,0,writes[i],parent?(u64 *)words:NULL},b=a;
        bool eligible=a.level<3&&a.parent;
        bool old=eligible&&(++a.writes>PT_FLOOD_WRITES||inactive);
        bool suppressed=guard&&eligible&&inactive&&keeper&&a.writes<=PT_FLOOD_WRITES;
        bool fallback=guard&&eligible&&inactive&&keeper&&a.writes>PT_FLOOD_WRITES;
        bool now=pt_flood_decision(&b,inactive);
        assert(now==(old&&!suppressed)&&a.writes==b.writes);
        assert(context.stat_keeper_inactive_suppressed==(u64)suppressed);
        assert(context.stat_keeper_inactive_threshold_fallbacks==(u64)fallback);
        u64 decisions=0;for(u32 k=0;k<6;k++)decisions+=context.stat_mmu_flood_decisions[k];
        assert(decisions==(u64)(profile&&now));
    }
    // With no walks resetting writes, stores1..64 stay tracked and store65 takes the original fallback.
    memset(&context,0,sizeof(context));context.keeper_inactive_guard=1;views[0].keeper=1;
    ShadowDesc table={0,0,0,(u64 *)words};
    for(u32 n=1;n<=65;n++){assert(pt_flood_decision(&table,true)==(n>64));assert(table.writes==n);}
    assert(context.stat_keeper_inactive_suppressed==64&&context.stat_keeper_inactive_threshold_fallbacks==1);
    puts("PASS: reuse state/order and flood baseline equivalence; guard on/off eligibility/increments/wrap/counters and65-store fallback");
    return 0;
}
'''
args.emit_c.write_text(prefix + functions + checks)
print('Wrote', args.emit_c)
