#!/usr/bin/env python3
"""Prepare a data-only census proof from production emitter/monitor functions.

This script never compiles or runs programs. Its printed commands compile only
libc fixtures: generated ARM words are inspected as data, never executed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex


def function(source, name):
    match = re.search(r'(?m)^(?:static )?(?:inline )?[^;{\n]+\b'+name+r'\([^;{]*\)\s*\{', source)
    if not match:
        raise ValueError('function missing: '+name)
    depth, end = 1, match.end()
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


def struct(source, name):
    end = source.index('} '+name+';') + len('} '+name+';')
    start = source.rfind('typedef struct {', 0, end)
    return source[start:end]


def fields(declaration):
    body = re.sub(r'//[^\n]*|/\*[\s\S]*?\*/', '', declaration)
    body = body[body.index('{')+1:body.rindex('}')]
    return [re.search(r'(\w+)\s*(?:\[[^\]]+\])?\s*$', part).group(1)
            for line in body.split(';') if line.strip() for part in line.split(',')]


FIXTURE = r'''
#include <assert.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tr_internal.h"
void baseline_emit_int(const Insn *, int);
void candidate_emit_int(const Insn *, int);
bool tr_precise_flags=true,tr_has_tzcnt=false,tr_hybrid=true,tr_no_bulk_rep=false,tr_compat=false;
bool tr_profile_shift_helpers=false;
bool tr_inline_shr8=false,tr_page_rep=false;
int tr_cpl=3,cur_part=0;
u64 tr_stub_lookup,tr_stub_ret_miss,tr_stub_pushf,tr_stub_sahf;
u32 *tr_cc; static u32 *current_words;
u64 tr_cc_va(const u32 *p){return 0x10000000ULL+(u64)(p-current_words)*4;}
void emit_stop(u64 rip,const char *why){(void)rip;(void)why;svc(SVC_UD);}
static u8 input[32];static int input_size;
int tr_fetch(u64 va,u8 *out,int n){if(va<0x400000||va-0x400000>=(u64)input_size)return 0;int left=input_size-(int)(va-0x400000);if(n>left)n=left;memcpy(out,input+va-0x400000,(size_t)n);return n;}
static EngineCtx contexts[2], *c;
#define NZCV_N (1ULL << 31)
#define NZCV_Z (1ULL << 30)
#define NZCV_C (1ULL << 29)
#define NZCV_V (1ULL << 28)
@FUNCTIONS@
@ABI@
static uint64_t checks,layouts,semantic_pairs,classified,invalid,disabled,negative;
static void check(int good){++checks;if(!good){fprintf(stderr,"SHIFT_CENSUS_FAIL check=%"PRIu64"\n",checks);exit(1);}}
static unsigned oracle_bucket(unsigned kind,unsigned width,unsigned memory,unsigned cl,u64 count){
    unsigned zero=(count%(width==3?64:32))==0;
    return kind*32+width*8+memory*4+cl*2+zero;
}
static void counters(void){
    memset(contexts,0,sizeof(contexts));c=&contexts[0];
    for(unsigned kind=0;kind<8;kind++)for(unsigned width=0;width<4;width++)
    for(unsigned memory=0;memory<2;memory++)for(unsigned cl=0;cl<2;cl++)for(unsigned count=0;count<128;count++){
        Insn in={.sz=1u<<width,.rm_reg=!memory,.by_cl=cl};
        tr_profile_shift_helpers=false;u64 plain=shift_helper_descriptor(&in,(int)kind);
        check(plain==(kind|((u64)(1u<<width)<<8)));
        tr_profile_shift_helpers=true;u64 desc=shift_helper_descriptor(&in,(int)kind);
        check((desc&0xffff)==plain && (desc&SHIFT_HELPER_PROFILE_MARK));
        check(!!(desc&SHIFT_HELPER_PROFILE_MEM)==memory && !!(desc&SHIFT_HELPER_PROFILE_CL)==cl);
        unsigned bucket=oracle_bucket(kind,width,memory,cl,count);
        check(bucket<SHIFT_HELPER_PROFILE_BUCKETS);
        u64 before=c->stat_shift_helpers[bucket];
        c->profile_shift_helpers=0;count_shift_helper(desc,count);check(c->stat_shift_helpers[bucket]==before);disabled++;
        c->profile_shift_helpers=1;count_shift_helper(desc,count);check(c->stat_shift_helpers[bucket]==before+1);classified++;
    }
    u64 sum=0;for(unsigned i=0;i<SHIFT_HELPER_PROFILE_BUCKETS;i++){unsigned zeros=((i>>3)&3)==3?2:4;check(c->stat_shift_helpers[i]==(i&1?zeros:128-zeros));sum+=c->stat_shift_helpers[i];}
    check(sum==classified && !c->stat_shift_helpers_unclassified);
    const u64 bad[]={0x104,SHIFT_HELPER_PROFILE_MARK|(3ULL<<8)|4,SHIFT_HELPER_PROFILE_MARK|4,
        SHIFT_HELPER_PROFILE_MARK|(16ULL<<8)|4,SHIFT_HELPER_PROFILE_MARK|(255ULL<<8)|4,
        SHIFT_HELPER_PROFILE_MARK|0x104|8,SHIFT_HELPER_PROFILE_MARK|0x104|(1ULL<<63)};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++){
        u64 before=c->stat_shift_helpers_unclassified;c->profile_shift_helpers=0;count_shift_helper(bad[i],0);check(c->stat_shift_helpers_unclassified==before);
        c->profile_shift_helpers=1;count_shift_helper(bad[i],0);check(c->stat_shift_helpers_unclassified==before+1);invalid++;
    }
    // Per-vCPU storage and high count bits do not change classification.
    c=&contexts[1];c->profile_shift_helpers=1;count_shift_helper(SHIFT_HELPER_PROFILE_MARK|0x204,UINT64_C(0xffffffffffffffa0));
    check(c->stat_shift_helpers[oracle_bucket(4,1,0,0,0)]==1);
    c=&contexts[0];check(c->stat_shift_helpers[oracle_bucket(4,1,0,0,0)]==4);
    // Deliberately wrong claimed dimensions must not match the actual counter.
    c=&contexts[1];unsigned wrong=oracle_bucket(4,1,0,0,0);
    u64 before=c->stat_shift_helpers[wrong];
    count_shift_helper(SHIFT_HELPER_PROFILE_MARK|SHIFT_HELPER_PROFILE_MEM|0x204,0);
    negative+=c->stat_shift_helpers[wrong]==before;
    count_shift_helper(SHIFT_HELPER_PROFILE_MARK|SHIFT_HELPER_PROFILE_CL|0x204,0);
    negative+=c->stat_shift_helpers[wrong]==before;
    count_shift_helper(SHIFT_HELPER_PROFILE_MARK|0x204,1);
    negative+=c->stat_shift_helpers[wrong]==before;
}
static void semantics(void){
    static const u64 values[]={0,1,0x8000000080008080ULL,~0ULL};
    memset(contexts,0,sizeof(contexts));
    for(unsigned kind=0;kind<8;kind++)for(unsigned width=0;width<4;width++)
    for(unsigned memory=0;memory<2;memory++)for(unsigned cl=0;cl<2;cl++)
    for(unsigned count=0;count<128;count++)for(unsigned value=0;value<4;value++)for(unsigned flags=0;flags<4;flags++){
        u64 plain=kind|((u64)(1u<<width)<<8);
        u64 profiled=plain|SHIFT_HELPER_PROFILE_MARK|(memory?SHIFT_HELPER_PROFILE_MEM:0)|(cl?SHIFT_HELPER_PROFILE_CL:0);
        for(unsigned cpu=0;cpu<2;cpu++){
            EngineCtx *q=&contexts[cpu];for(unsigned reg=0;reg<31;reg++)q->x[reg]=UINT64_C(0x94d049bb133111eb)*(reg+1);
            q->spsr=(u64)(flags*5)<<28;q->x[R_S0]=values[value];q->x[R_S1]=count|UINT64_C(0x1234567800000000);q->x[R_S2]=cpu?profiled:plain;
        }
        c=&contexts[0];baseline_h_shift();c=&contexts[1];c->profile_shift_helpers=1;
        unsigned bucket=oracle_bucket(kind,width,memory,cl,count);u64 before=c->stat_shift_helpers[bucket];h_shift();
        check(c->stat_shift_helpers[bucket]==before+1);
        check(!memcmp(contexts[0].x,contexts[1].x,sizeof(contexts[0].x)) && contexts[0].spsr==contexts[1].spsr);semantic_pairs++;
    }
}
typedef struct {u32 words[256];unsigned count;} Code;
static Code emitted(const Insn *in,int live,int candidate){Code r={0};current_words=r.words;tr_cc=r.words;if(candidate)candidate_emit_int(in,live);else baseline_emit_int(in,live);r.count=(unsigned)(tr_cc-r.words);check(r.count<=256);return r;}
static Insn decoded(unsigned size,unsigned kind,unsigned memory,unsigned cl,unsigned count,unsigned reg,int compat){
    tr_compat=compat;input_size=0;if(size==2)input[input_size++]=0x66;
    if(!compat&&(size==8||reg>=8))input[input_size++]=(u8)(0x40|(size==8?8:0)|(reg>=8?1:0));
    input[input_size++]=(u8)(cl?(size==1?0xd2:0xd3):(size==1?0xc0:0xc1));
    input[input_size++]=(u8)((memory?0:0xc0)|(kind<<3)|(reg&7));if(!cl)input[input_size++]=(u8)count;
    Insn in;x86_decode(0x400000,&in);check(in.op==OP_SHIFT && in.next==0x400000+(u64)input_size);return in;
}
static int svc_at(const Code *code){for(unsigned i=0;i<code->count;i++)if(code->words[i]==(0xd4000001u|SVC_H_SHIFT<<5))return (int)i;return -1;}
static void emissions(void){
    const unsigned counts[]={0,1,7,8,16,31,32,63,64,255};
    for(unsigned width=0;width<4;width++)for(unsigned kind=0;kind<8;kind++)for(unsigned memory=0;memory<2;memory++)
    for(unsigned cl=0;cl<2;cl++)for(unsigned count=0;count<sizeof(counts)/sizeof(counts[0]);count++)
    for(unsigned compat=0;compat<2;compat++)for(unsigned live=0;live<64;live++){
        if(compat&&width==3)continue;
        unsigned reg=memory?3:(count%3==0?1:count%3==1?4:compat?2:9);
        Insn in=decoded(1u<<width,kind,memory,cl,counts[count],reg,(int)compat);
        tr_profile_shift_helpers=false;Code old=emitted(&in,(int)live,0),off=emitted(&in,(int)live,1);
        check(old.count==off.count && !memcmp(old.words,off.words,old.count*4));layouts++;
        tr_profile_shift_helpers=true;Code on=emitted(&in,(int)live,1);int a=svc_at(&old),z=svc_at(&on);
        if(a<0){check(z<0 && old.count==on.count && !memcmp(old.words,on.words,old.count*4));continue;}
        check(z==a+1 && on.count==old.count+1 && a>0);
        check(!memcmp(old.words,on.words,(unsigned)(a-1)*4));
        check(!memcmp(old.words+a,on.words+z,(old.count-(unsigned)a)*4));
        u64 desc=shift_helper_descriptor(&in,kind==SH_SAL?SH_SHL:(int)kind);
        check(on.words[z-2]==(0xd2800000u|((u32)desc&0xffff)<<5|R_S2));
        check(on.words[z-1]==(0xf2a00000u|((u32)(desc>>16)&0xffff)<<5|R_S2));
    }
}
int main(void){counters();semantics();emissions();check(negative==3);printf("{\"result\":\"PASS_DATA_ONLY\",\"checks\":%"PRIu64",\"classified_cases\":%"PRIu64",\"disabled_cases\":%"PRIu64",\"unclassified_cases\":%"PRIu64",\"semantic_pairs\":%"PRIu64",\"disabled_emission_pairs\":%"PRIu64",\"negative_controls\":%"PRIu64",\"old_ctx_bytes\":%zu,\"new_ctx_bytes\":%zu,\"generated_ARM_executed\":false,\"HV_or_guest_executed\":false}\n",checks,classified,disabled,invalid,semantic_pairs,layouts,negative,sizeof(LegacyEngineCtx),sizeof(EngineCtx));return 0;}
'''


def prepare(engine, work):
    src = engine/'Sources/CEngine'
    emitter = (src/'emit_int.c').read_text()
    monitor = (engine/'Monitor/monitor.c').read_text()
    abi = (src/'include/engine_abi.h').read_text()
    config = (src/'include/translator.h').read_text()
    descriptor = function(emitter, 'shift_helper_descriptor')
    baseline = emitter.replace(descriptor+'\n\n', '')
    call = 'movimm(S2, shift_helper_descriptor(in, kind));'
    assert baseline.count(call) == 1
    baseline = baseline.replace(call, 'movimm(S2, (u64)kind | (u64)sz << 8);')
    h_shift = function(monitor, 'h_shift')
    assert h_shift.count('count_shift_helper(desc_, cnt);') == 1
    old_h_shift = h_shift.replace('    count_shift_helper(desc_, cnt);\n', '').replace(
        'size = (u32)(desc_ >> 8) & 0xff;', 'size = (u32)(desc_ >> 8);')
    old_ctx = struct(abi, 'EngineCtx')
    begin = old_ctx.index('    // Opt-in, per-vCPU helper-entry counts')
    old_ctx = old_ctx[:begin]+'} EngineCtx;'
    old_config = struct(config, 'TrConfig')
    # Freeze the original prefix; later appended diagnostics are not legacy fields.
    old_config = old_config[:old_config.index('    bool profile_shift_helpers;')] + '} TrConfig;'
    legacy = old_ctx.replace('} EngineCtx;', '} LegacyEngineCtx;')+'\n'+old_config.replace('} TrConfig;', '} LegacyTrConfig;')
    assertions = [f'_Static_assert(offsetof({new}, {field}) == offsetof({old}, {field}), "old {new}.{field} offset");'
                  for new, old, declaration in [('EngineCtx','LegacyEngineCtx',old_ctx),('TrConfig','LegacyTrConfig',old_config)]
                  for field in fields(declaration)]
    assertions += ['_Static_assert(offsetof(EngineCtx, profile_shift_helpers) == sizeof(LegacyEngineCtx), "append only");',
                   '_Static_assert(offsetof(EngineCtx, stat_shift_helpers)+sizeof(((EngineCtx *)0)->stat_shift_helpers)-offsetof(EngineCtx, profile_shift_helpers) == 2064, "bounded census span; later appended fields allowed");',
                   '_Static_assert(sizeof(EngineCtx) <= CTX_SLOT, "existing slot");']
    helpers = '\n\n'.join(function(monitor,name) for name in ['cur_nzcv','helper_flags','helper_keep_flags','count_shift_helper'])
    functions = descriptor+'\n'+helpers+'\n'+old_h_shift.replace('h_shift(', 'baseline_h_shift(',1)+'\n'+h_shift
    fixture = FIXTURE.replace('@FUNCTIONS@',functions).replace('@ABI@',legacy+'\n'+'\n'.join(assertions))
    work.mkdir(exist_ok=False)
    inputs = work/'input';(inputs/'include').mkdir(parents=True)
    for name in ('x86_decode.c','tr_internal.h','include/engine_abi.h','include/translator.h'):
        (inputs/name).write_bytes((src/name).read_bytes())
    (inputs/'emit_int-baseline.c').write_text(baseline)
    (inputs/'emit_int-candidate.c').write_text(emitter)
    (work/'shift-helper-census.c').write_text(fixture)
    # Source-only guards: no guest load/store or flag statement is reordered.
    assert old_h_shift.replace('baseline_h_shift(', 'h_shift(')==h_shift.replace('    count_shift_helper(desc_, cnt);\n','').replace('size = (u32)(desc_ >> 8) & 0xff;','size = (u32)(desc_ >> 8);')
    swift=(engine/'Sources/Engine/Engine.swift').read_text()
    assert 'config.profile_shift_helpers = environment["LC_HVDBT_PROFILE_SHIFT_HELPERS"] == "1"' in swift
    assert 'ctx.pointee.profile_shift_helpers = environment["LC_HVDBT_PROFILE_SHIFT_HELPERS"] == "1" ? 1 : 0' in swift
    record={'scope':'actual-function pure libc counter/semantic proof and generated ARM data comparison; no generated ARM/HV/guest execution',
            'source_sha256':{name:hashlib.sha256(text.encode()).hexdigest() for name,text in [('emit_int.c',emitter),('monitor.c',monitor),('engine_abi.h',abi),('translator.h',config)]},
            'baseline_emitter_sha256':hashlib.sha256(baseline.encode()).hexdigest(),
            'baseline_h_shift_sha256':hashlib.sha256(old_h_shift.encode()).hexdigest(),
            'proof_sha256':hashlib.sha256(fixture.encode()).hexdigest(),'old_offset_assertions':len(assertions)}
    (work/'input-receipt.json').write_text(json.dumps(record,indent=2)+'\n')
    rename=('movimm','logimm','addr_reg','addr_prepare','ldst','gpr_read','gpr_write','emit_parity','emit_int')
    flags=['-std=c11','-O2','-Wall','-Wextra','-Werror','-I'+str(inputs),'-I'+str(inputs/'include')]
    for variant in ('baseline','candidate'):
        print(shlex.join(['nice','-n','10','xcrun','clang',*flags,*['-D'+name+'='+variant+'_'+name for name in rename],'-c',str(inputs/('emit_int-'+variant+'.c')),'-o',str(work/(variant+'.o'))]))
    print(shlex.join(['nice','-n','10','xcrun','clang',*flags,str(work/'shift-helper-census.c'),str(inputs/'x86_decode.c'),str(work/'baseline.o'),str(work/'candidate.o'),'-o',str(work/'shift-helper-census')]))
    print(shlex.join(['nice','-n','10',str(work/'shift-helper-census')]))
    return record


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--engine',type=Path,default=Path(__file__).resolve().parents[1])
    parser.add_argument('--work',type=Path,required=True)
    args=parser.parse_args();prepare(args.engine.resolve(),args.work.resolve())
