#!/usr/bin/env python3
"""Emit an actual-C, data-only fixture for the opt-in compile-input census.

The fixture calls production capture/lookup functions and tr_fetch with bounded
allocation and RAM-walk stubs. Guest bytes remain data; it neither translates nor
executes guest/generated code. Empty direct-map slots mean unseen OR evicted.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


ENGINE = Path(__file__).resolve().parents[1]
BASELINE = 'cdf727f5b87abb8d656e20d9eb9f957c5b06f570'


def function(source, name):
    found = re.search(r'(?m)^(?:static )?(?:inline )?[^;{\n]+\b' + name + r'\([^;{]*\)\s*\{', source)
    if not found:
        raise ValueError('missing actual function: ' + name)
    at, depth = found.end(), 1
    while depth:
        depth += (source[at] == '{') - (source[at] == '}')
        at += 1
    return source[found.start():at]


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--emit-c', type=Path, required=True)
    args = parser.parse_args()
    emit(args.emit_c)


def emit(path):
    source_path = ENGINE/'Sources/CEngine/translate.c'
    header = ENGINE/'Sources/CEngine/include/translator.h'
    control_path = ENGINE/'Sources/Engine/Engine.swift'
    control = control_path.read_text()
    if 'config.profile_compile_inputs = environment["LC_HVDBT_PROFILE_COMPILE_INPUTS"] == "1"' not in control:
        raise ValueError('census must remain an explicit default-OFF switch')
    source = source_path.read_text()
    baseline = subprocess.check_output(['git', 'show', BASELINE+':Source/Engine/Sources/CEngine/translate.c'], cwd=ENGINE, text=True)
    start = source.index('// ---- Inert analyzed-input recurrence census ----')
    end = source.index('// ---- End inert analyzed-input recurrence census ----', start)
    section = source[start:end]
    names = ('compile_inputs_init', 'compile_inputs_begin', 'compile_inputs_reject',
             'compile_inputs_request', 'compile_inputs_read', 'compile_inputs_finish', 'tr_fetch')
    for name in names[:-1]:
        if function(source, name) not in section:
            raise ValueError('actual helper outside extracted section: '+name)
    # Lifetime is source-controlled without pretending to run the decoder/emitter.
    translate = function(source, 'do_translate')
    begin = 'compile_inputs_begin(rip, cpl, tr_compat, (u32)key, res.frames[0], single);'
    if not (translate.index('reuse_dead_block(') < translate.index('fetch_walk(rip, &gpa0);') < translate.index(begin) < translate.index('x86_decode(')):
        raise ValueError('capture must begin after reuse and initial frame walk')
    if translate.count('compile_inputs_begin(') != 1 or translate.count('compile_inputs_finish();') != 3:
        raise ValueError('review translation capture lifetime')
    if not (translate.index('for (int i = 0; i < pend_count; i++)') < translate.rindex('compile_inputs_finish();') < translate.index('// How many blocks')):
        raise ValueError('normal capture must include liveness and pending fixups')
    for marker in ('TR_CI_UNPROTECTED', 'TR_CI_FETCH_FAULT', 'TR_CI_UNSUPPORTED'):
        if marker not in translate:
            raise ValueError('missing translation rejection: '+marker)
    if 'compile_inputs_reject(TR_CI_UNSUPPORTED);' not in function(source, 'emit_stop'):
        raise ValueError('emit_stop missing capture rejection')
    if function(source, 'tr_init').count('compile_inputs_init();') != 1:
        raise ValueError('init must establish one census epoch')
    for name in ('tr_flush_parked', 'tr_flush_all', 'flush_everything'):
        if 'compile_inputs_' in function(source, name):
            raise ValueError('code-cache reset must not clear census history')
    fetch = function(source, 'tr_fetch')
    if not (fetch.index('compile_inputs_request(va, n);') < fetch.index('fetch_walk(') < fetch.index('memcpy(') < fetch.index('compile_inputs_read(') < fetch.index('got += chunk;')):
        raise ValueError('tr_fetch must capture only bytes already copied')
    if 'compile_inputs_reject(TR_CI_PARTIAL);\n            return got;' not in fetch:
        raise ValueError('partial fetch must reject before its existing return')
    expected_fetch = function(baseline, 'tr_fetch')
    expected_fetch = expected_fetch.replace('    int got = 0;\n', '    int got = 0;\n    if (cfg.profile_compile_inputs)\n        compile_inputs_request(va, n);\n', 1)
    expected_fetch = expected_fetch.replace('        if (!fetch_walk(va + got, &gpa))\n            return got;', '        if (!fetch_walk(va + got, &gpa)) {\n            compile_inputs_reject(TR_CI_PARTIAL);\n            return got;\n        }', 1)
    expected_fetch = expected_fetch.replace('        memcpy(out + got, ram_at(gpa), chunk);\n', '        memcpy(out + got, ram_at(gpa), chunk);\n        if (cfg.profile_compile_inputs)\n            compile_inputs_read(va + got, out + got, chunk, gpa);\n', 1)
    if fetch != expected_fetch:
        raise ValueError('tr_fetch may add only the reviewed observation hooks')
    original = function(baseline, 'tr_fetch').replace('int tr_fetch(', 'int baseline_fetch(', 1)
    data = (PREFIX.replace('HEADER', json.dumps(str(header))) + section + '\n' + fetch + '\n' + original + CHECKS)
    path.write_text(data)
    receipt = {'schema': 1, 'source': str(source_path), 'source_sha256': digest(source.encode()),
               'header_sha256': digest(header.read_bytes()), 'baseline': BASELINE,
               'control_sha256': digest(control.encode()), 'default_off_source_checked': True,
               'actual_functions': list(names), 'actual_section_sha256': digest(section.encode()),
               'generated_c_sha256': digest(data.encode()), 'generated_c_bytes': len(data.encode()),
               'production_capacity_preserved': 8192, 'generated_or_guest_code_executed': False,
               'limitations': ['Only libc/data capture and RAM-walk/allocation stubs execute.',
                               'Decoder/emitter, real permissions, concurrent writes and generated code do not execute.',
                               'Translation/stop/init/reset hook placement is source-controlled, not a live Engine proof.',
                               'A direct-map miss means unseen or evicted; prior means preceding retained eligible observation.']}
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
typedef uint8_t u8;
static unsigned checks;
static void check(int ok,const char *why,int line) {
    checks++;if(!ok){fprintf(stderr,"FAIL compile-input fixture line=%d: %s\n",line,why);exit(1);}
}
#define CHECK(x) check((x),#x,__LINE__)
static TrConfig cfg;
static TrStats stats;
static EngineCtx context,*ctx_ptr=&context;
typedef struct { void *user,*base;size_t bytes; } Allocation;
static Allocation allocations[4];
static unsigned alloc_calls,free_calls,live_allocs,fail_alloc;
static size_t live_bytes,peak_bytes;
static void guards(void) {
    for(unsigned i=0;i<4;i++)if(allocations[i].user) {
        unsigned char *p=allocations[i].base;
        for(unsigned j=0;j<16;j++)CHECK(p[j]==0xa7&&p[16+allocations[i].bytes+j]==0xa7);
    }
}
static void *observed_calloc(size_t n,size_t size) {
    alloc_calls++;CHECK(size&&n<=40u*1024*1024/size);
    if(alloc_calls==fail_alloc)return NULL;
    size_t bytes=n*size;CHECK(live_bytes+bytes<=40u*1024*1024);
    unsigned i=0;while(i<4&&allocations[i].user)i++;CHECK(i<4);
    unsigned char *p=malloc(bytes+32);CHECK(p!=NULL);
    memset(p,0xa7,16);memset(p+16,0,bytes);memset(p+16+bytes,0xa7,16);
    allocations[i]=(Allocation){p+16,p,bytes};live_allocs++;live_bytes+=bytes;
    if(live_bytes>peak_bytes)peak_bytes=live_bytes;
    return p+16;
}
static void observed_free(void *p) {
    if(!p)return;guards();unsigned i=0;while(i<4&&allocations[i].user!=p)i++;CHECK(i<4);
    live_allocs--;live_bytes-=allocations[i].bytes;free_calls++;
    free(allocations[i].base);memset(&allocations[i],0,sizeof allocations[i]);
}
static u8 ram[4*4096];
static u64 walk_page,walk_frame,fetch_lo,fetch_hi;
static unsigned walk_calls,walk_fail;
static bool fetch_walk(u64 va,u64 *gpa) {
    walk_calls++;if(walk_calls==walk_fail)return false;
    CHECK(va>=walk_page&&va-walk_page<8192);
    *gpa=walk_frame+(va-walk_page);CHECK(*gpa<sizeof ram);return true;
}
static u8 *ram_at(u64 gpa) { CHECK(gpa<sizeof ram);return ram+gpa; }
#define calloc observed_calloc
#define free observed_free
'''


CHECKS = r'''
#undef calloc
#undef free
static void conserved(void) {
    u64 rejected=0,joint=0;
    for(unsigned i=0;i<TR_CI_REJECT_KINDS;i++)rejected+=stats.compile_inputs_reject[i];
    for(unsigned i=0;i<4;i++)joint+=stats.compile_inputs_exact_joint[i];
    CHECK(stats.compile_inputs_attempts==stats.compile_inputs_admitted+rejected);
    CHECK(stats.compile_inputs_admitted==stats.compile_inputs_empty+stats.compile_inputs_evictions+stats.compile_inputs_prior);
    CHECK(stats.compile_inputs_prior==stats.compile_inputs_exact+stats.compile_inputs_coverage_mismatch+stats.compile_inputs_bytes_mismatch);
    CHECK(stats.compile_inputs_exact==joint);
    CHECK(stats.compile_inputs_exact==stats.compile_inputs_exact_same_key+stats.compile_inputs_exact_different_key);
    CHECK(stats.compile_inputs_exact==stats.compile_inputs_exact_same_pfn+stats.compile_inputs_exact_different_pfn);
    CHECK(stats.compile_inputs_exact_same_key==stats.compile_inputs_exact_joint[0]+stats.compile_inputs_exact_joint[1]);
    CHECK(stats.compile_inputs_exact_same_pfn==stats.compile_inputs_exact_joint[0]+stats.compile_inputs_exact_joint[2]);guards();
}
static void reset(bool enabled,unsigned fail) {
    memset(&cfg,0,sizeof cfg);cfg.profile_compile_inputs=enabled;
    memset(&context,0,sizeof context);alloc_calls=0;fail_alloc=fail;
    compile_inputs_init();walk_calls=walk_fail=0;walk_page=0x4000;walk_frame=0x1000;
    fetch_lo=~0ULL;fetch_hi=0;memset(ram,0,sizeof ram);
    CHECK(stats.compile_inputs_enabled==(u64)enabled&&stats.compile_inputs_sampling_shift==0);
    conserved();
}
static void begin(u64 rip,u32 key,u64 frame,int cpl,bool compat) {
    walk_page=rip&~0xfffULL;walk_frame=frame;walk_calls=walk_fail=0;
    compile_inputs_begin(rip,cpl,compat,key,frame,false);
}
static void fetch(u64 va,int n) {
    u8 now[64],old[64];CHECK(n>=0&&n<=64);memset(now,0xa5,sizeof now);memset(old,0xa5,sizeof old);
    fetch_lo=~0ULL;fetch_hi=0;walk_calls=0;
    int got=tr_fetch(va,now,n);u64 lo=fetch_lo,hi=fetch_hi;unsigned walks=walk_calls;
    fetch_lo=~0ULL;fetch_hi=0;walk_calls=0;int before=baseline_fetch(va,old,n);
    CHECK(got==before&&memcmp(now,old,sizeof now)==0&&fetch_lo==lo&&fetch_hi==hi&&walk_calls==walks);
}
static void finish(void) {
    compile_inputs_finish();TrStats before=stats;compile_inputs_finish();
    CHECK(memcmp(&before,&stats,sizeof stats)==0);conserved();
}
static void observe(u64 rip,u32 key,u64 frame,int cpl,bool compat,int n) {
    begin(rip,key,frame,cpl,compat);fetch(rip,n);finish();
}
static unsigned occupied(void) {
    unsigned n=0;for(unsigned i=0;i<COMPILE_INPUT_SLOTS;i++)n+=compile_inputs_table[i].valid;
    return n;
}
static void allocation_and_epoch(void) {
    reset(false,0);CHECK(alloc_calls==0&&live_allocs==0&&compile_inputs_table==NULL&&compile_inputs_capture==NULL);
    compile_inputs_begin(0x4020,3,false,1,0x1000,false);fetch(0x4020,15);compile_inputs_reject(TR_CI_UNSUPPORTED);finish();
    CHECK(stats.compile_inputs_attempts==0&&stats.compile_inputs_bytes==0&&stats.compile_inputs_config_base==0);
    for(unsigned fail=1;fail<=2;fail++) {
        reset(true,fail);CHECK(alloc_calls==2&&live_allocs==0&&stats.compile_inputs_available==0&&stats.compile_inputs_bytes==0);
        CHECK(stats.compile_inputs_allocation_failures==1&&compile_inputs_table==NULL&&compile_inputs_capture==NULL);
        compile_inputs_begin(0x4020,3,false,1,0x1000,false);fetch(0x4020,15);finish();
        CHECK(stats.compile_inputs_reject[TR_CI_UNAVAILABLE]==1);
        compile_inputs_begin(0x4020,3,false,1,0x1000,true);finish();CHECK(stats.compile_inputs_reject[TR_CI_SINGLE]==1);
    }
    reset(true,0);CHECK(live_allocs==2&&stats.compile_inputs_available==1&&stats.compile_inputs_bytes==live_bytes);
    CHECK(COMPILE_INPUT_SLOTS==8192&&COMPILE_INPUT_PAGE==4096&&live_bytes<=40u*1024*1024);
    observe(0x4020,1,0x1000,3,false,15);observe(0x4020,1,0x1000,3,false,15);
    CHECK(stats.compile_inputs_exact==1);unsigned freed=free_calls;compile_inputs_init();
    CHECK(free_calls==freed+2&&stats.compile_inputs_attempts==0&&occupied()==0&&live_allocs==2);
    observe(0x4020,1,0x1000,3,false,15);CHECK(stats.compile_inputs_empty==1&&stats.compile_inputs_prior==0);
    cfg.profile_compile_inputs=false;compile_inputs_init();CHECK(live_allocs==0&&stats.compile_inputs_available==0);
}
static void exact_and_masks(void) {
    reset(true,0);u32 key=7;u64 frame=0x1000;observe(0x4020,key,frame,3,false,15);
    for(unsigned joint=0;joint<4;joint++) {
        if(joint&2)key++;if(joint&1)frame=frame==0x1000?0x2000:0x1000;
        observe(0x4020,key,frame,3,false,15);CHECK(stats.compile_inputs_exact_joint[joint]==1);
    }
    CHECK(stats.compile_inputs_empty==1&&stats.compile_inputs_exact==4&&occupied()==1);
    // An ineligible compile cannot replace the preceding retained eligible input.
    begin(0x4020,key,frame,3,false);compile_inputs_reject(TR_CI_UNSUPPORTED);fetch(0x4020,15);finish();
    observe(0x4020,key,frame,3,false,15);CHECK(stats.compile_inputs_exact==5);
    // All-zero captured bytes are not an empty input, and coverage is independent of byte content.
    reset(true,0);observe(0x4020,1,0x1000,3,false,1);observe(0x4020,1,0x1000,3,false,2);
    CHECK(stats.compile_inputs_coverage_mismatch==1&&stats.compile_inputs_bytes_mismatch==0);
    observe(0x4020,1,0x1000,3,false,2);CHECK(stats.compile_inputs_exact==1);
    // Unread holes remain canonical zero even when underlying RAM changes.
    ram[0x1000+0x80]=0xfe;observe(0x4020,1,0x1000,3,false,2);CHECK(stats.compile_inputs_exact==2);
    CHECK(compile_inputs_capture->input.bytes[0x80]==0);
    // Reads used only for lookahead/liveness still participate in the exact input.
    reset(true,0);
    for(unsigned i=0;i<3;i++) {
        begin(0x4020,1,0x1000,3,false);fetch(0x4020,8);fetch(0x4080,15);finish();
        if(i==0)ram[0x1083]=1;
    }
    CHECK(stats.compile_inputs_bytes_mismatch==1&&stats.compile_inputs_exact==1);
    // Consistent overlaps are accepted; mismatches update history only when admitted.
    begin(0x4020,1,0x1000,3,false);fetch(0x4020,15);fetch(0x4028,15);finish();
    CHECK(stats.compile_inputs_coverage_mismatch==1);
}
static void rejections(void) {
    reset(true,0);
    for(unsigned reason=0;reason<TR_CI_REJECT_KINDS;reason++) {
        u64 before=stats.compile_inputs_reject[reason];u64 admitted=stats.compile_inputs_admitted;
        if(reason==TR_CI_SINGLE)compile_inputs_begin(0x4020,3,false,1,0x1000,true);
        else {
            begin(0x4020,1,0x1000,3,false);
            switch(reason) {
            case TR_CI_CROSS_PAGE:fetch(0x4ffe,4);break;
            case TR_CI_PARTIAL:walk_fail=1;fetch(0x4020,15);walk_fail=0;break;
            case TR_CI_INCONSISTENT:fetch(0x4020,15);ram[0x1028]^=1;fetch(0x4028,15);break;
            case TR_CI_MIXED_FRAME:walk_frame=0x2000;fetch(0x4020,15);break;
            case TR_CI_EMPTY:break;
            default:fetch(0x4020,15);compile_inputs_reject(reason);break;
            }
        }
        finish();CHECK(stats.compile_inputs_reject[reason]==before+1&&stats.compile_inputs_admitted==admitted);
        // Finished/rejected captures cannot be revived by a later fetch without begin.
        TrStats ended=stats;walk_page=0x4000;walk_frame=0x1000;fetch(0x4020,1);finish();CHECK(memcmp(&ended,&stats,sizeof stats)==0);
    }
    // First encountered rejection wins, even if later callers report another cause.
    for(unsigned first=0;first<TR_CI_REJECT_KINDS;first++)for(unsigned later=0;later<TR_CI_REJECT_KINDS;later++) {
        u64 before=stats.compile_inputs_reject[first];begin(0x4020,1,0x1000,3,false);fetch(0x4020,1);
        compile_inputs_reject(first);compile_inputs_reject(later);finish();CHECK(stats.compile_inputs_reject[first]==before+1);
    }
    // Negative/zero request and wraparound/preceding-page requests reject without reading out of bounds.
    for(unsigned i=0;i<4;i++) {
        u64 before=stats.compile_inputs_admitted;begin(0x4020,1,0x1000,3,false);
        compile_inputs_request(i==2?0x3fff:i==3?~0ULL:0x4020,i==0?0:i==1?-1:2);finish();CHECK(stats.compile_inputs_admitted==before);
    }
}
static void identity_and_collision(void) {
    reset(true,0);observe(0x4020,1,0x1000,3,false,1);u64 prior=stats.compile_inputs_prior;
    observe(0x4020,1,0x1000,0,false,1);CHECK(stats.compile_inputs_prior==prior);
    observe(0x4020,1,0x1000,0,false,1);CHECK(stats.compile_inputs_prior==prior+1);
    prior=stats.compile_inputs_prior;observe(0x4020,1,0x1000,0,true,1);CHECK(stats.compile_inputs_prior==prior);
    // tr_init precedes SMP attachment: this dynamic identity bit must be captured at begin.
    context.vcpu_count=2;prior=stats.compile_inputs_prior;observe(0x4020,1,0x1000,0,true,1);CHECK(stats.compile_inputs_prior==prior);
    CHECK(compile_inputs_capture->input.config&(1ULL<<16));observe(0x4020,2,0x2000,0,true,1);CHECK(stats.compile_inputs_exact_joint[3]==1);
    reset(true,0);u64 previous[COMPILE_INPUT_SLOTS];memset(previous,0,sizeof previous);u64 lost=0;
    for(unsigned i=0;i<=COMPILE_INPUT_SLOTS;i++) {
        u64 rip=0x4000+(u64)i*4096,evictions=stats.compile_inputs_evictions;observe(rip,1,0x1000,3,false,1);
        unsigned slot=0;while(slot<COMPILE_INPUT_SLOTS&&(!compile_inputs_table[slot].valid||compile_inputs_table[slot].rip!=rip))slot++;
        CHECK(slot<COMPILE_INPUT_SLOTS);
        if(stats.compile_inputs_evictions>evictions){lost=previous[slot];CHECK(lost&&lost!=rip);break;}
        previous[slot]=rip;
    }
    CHECK(lost&&occupied()<=COMPILE_INPUT_SLOTS);u64 exact=stats.compile_inputs_exact,evictions=stats.compile_inputs_evictions;
    observe(lost,1,0x1000,3,false,1);CHECK(stats.compile_inputs_exact==exact&&stats.compile_inputs_evictions==evictions+1);
    observe(lost,1,0x1000,3,false,1);CHECK(stats.compile_inputs_exact==exact+1);
    // Full-population capture admits every eligible occurrence, including repeated RIPs.
    u64 admitted=stats.compile_inputs_admitted;for(unsigned i=0;i<257;i++)observe(lost,i&1?2:1,0x1000,3,false,1);
    CHECK(stats.compile_inputs_admitted==admitted+257&&stats.compile_inputs_sampling_shift==0);
    // Every fixed emitter configuration flag participates in a freshly cleared epoch.
    bool *flags[]={&cfg.precise_flags,&cfg.has_tzcnt_lzcnt,&cfg.hybrid,&cfg.keep_kernel_tables,&cfg.no_bulk_rep,
                   &cfg.no_return_prediction,&cfg.all_flags_live,&cfg.profile_shift_helpers,&cfg.inline_shr8,&cfg.page_rep};
    for(unsigned i=0;i<sizeof flags/sizeof flags[0];i++) {
        *flags[i]=true;compile_inputs_init();CHECK(stats.compile_inputs_config_base==((1ULL<<32)|((1ULL<<(i+1))-1)));
        CHECK(stats.compile_inputs_attempts==0&&occupied()==0);
    }
}
int main(int argc,char **argv) {
    CHECK(argc==1||(argc==2&&strcmp(argv[1],"--negative-control-omit-fetch-capture")==0));
    if(argc==2) {
        reset(true,0);begin(0x4020,1,0x1000,3,false);u8 out[15];
        baseline_fetch(0x4020,out,15);finish();CHECK(stats.compile_inputs_admitted==1); // must fail: old tr_fetch never captures
        return 0;
    }
    allocation_and_epoch();exact_and_masks();rejections();identity_and_collision();
    conserved();cfg.profile_compile_inputs=false;compile_inputs_init();CHECK(live_allocs==0&&live_bytes==0);
    printf("PASS actual-C compile-input census checks=%u slots=%u peak_bytes=%zu generated_code_executed=0\n",checks,COMPILE_INPUT_SLOTS,peak_bytes);
    return 0;
}
'''


if __name__ == '__main__':
    main()
