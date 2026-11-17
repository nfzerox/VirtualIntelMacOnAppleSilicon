#!/usr/bin/env python3
"""Prepare a data-only production decoder/emitter SHR8 or SHR16 proof.

The printed nice commands compile libc fixtures only. Generated ARM words are
interpreted as integer data, never executed; no Engine, HV or guest is run.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex


def function(source, name):
    match=re.search(r'(?m)^(?:static )?(?:inline )?[^;{\n]+\b'+name+r'\([^;{]*\)\s*\{',source)
    if not match:raise ValueError('missing function '+name)
    depth,end=1,match.end()
    while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[match.start():end]


def baseline_emitter(source,bits=8):
    anchor=('    if (tr_inline_shr8 && !mem && sz == 1 && kind == SH_SHR && in->has_imm) {' if bits==8 else
            '    if (!mem && sz == 2 && kind == SH_SHR && in->has_imm) {')
    if source.count(anchor)!=1:raise ValueError('expected one bounded narrow SHR guard')
    start=source.index(anchor);end=start+len(anchor);depth=1
    while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
    if source[end:end+1]!='\n':raise ValueError('guard ending changed')
    return source[:start]+source[end+1:]


FIXTURE=r'''
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tr_internal.h"
#define PROOF_BITS @BITS@
void baseline_emit_int(const Insn *,int);void candidate_emit_int(const Insn *,int);
bool tr_precise_flags=true,tr_has_tzcnt=false,tr_hybrid=true,tr_no_bulk_rep=false,tr_compat=false;
bool tr_profile_shift_helpers=false,tr_inline_shr8=false,tr_page_rep=false;
int tr_cpl=3,cur_part=0;u64 tr_stub_lookup,tr_stub_ret_miss,tr_stub_pushf,tr_stub_sahf;
u32 *tr_cc;static u32 *current_words;
u64 tr_cc_va(const u32 *p){return 0x10000000ULL+(u64)(p-current_words)*4;}
void emit_stop(u64 rip,const char *why){(void)rip;(void)why;svc(SVC_UD);}
static u8 input[32];static int input_size;
int tr_fetch(u64 va,u8*out,int n){if(va<0x400000||va-0x400000>=(u64)input_size)return 0;int left=input_size-(int)(va-0x400000);if(n>left)n=left;memcpy(out,input+va-0x400000,(size_t)n);return n;}
static EngineCtx monitor_context,*c=&monitor_context;
#define NZCV_N (1ULL<<31)
#define NZCV_Z (1ULL<<30)
#define NZCV_C (1ULL<<29)
#define NZCV_V (1ULL<<28)
@HELPERS@
static uint64_t checks,states,layouts,controls,helper_layouts,zero_states,af_only,negative;
static void check(int good){++checks;if(!good){fprintf(stderr,"SHR8_PROOF_FAIL check=%"PRIu64" states=%"PRIu64" layouts=%"PRIu64"\n",checks,states,layouts);exit(1);}}
typedef struct{u32 words[256];unsigned count;}Code;
typedef struct{u64 x[32];unsigned nz;}State;
static u64 mask(unsigned bits){return bits==64?~0ULL:(1ULL<<bits)-1;}
static u64 readreg(const State*s,unsigned r){return r==31?0:s->x[r];}
static void writereg(State*s,unsigned r,u64 v,unsigned sf){if(r!=31)s->x[r]=v&mask(sf?64:32);}
static int is_rmif(u32 w){return (w&~((63u<<15)|(31u<<5)|15u))==0xba000400u;}
static void evaluate(State*s,const Code*code){
 for(unsigned i=0;i<code->count;i++){
  u32 w=code->words[i];unsigned sf=w>>31,rd=w&31,rn=(w>>5)&31,rm=(w>>16)&31;u64 a=readreg(s,rn)&mask(sf?64:32);
  if(w==0xd503201fu)continue;
  if(w==0xd500401fu){s->nz^=2;continue;}
  if(is_rmif(w)){unsigned shift=(w>>15)&63,bits=w&15;u64 v=readreg(s,rn),r=shift?(v>>shift)|(v<<(64-shift)):v;s->nz=(s->nz&~bits)|((unsigned)r&bits);continue;}
  if((w&~31u)==0xd51b4200u){s->nz=(unsigned)(readreg(s,rd)>>28)&15;continue;}
  if(w==(0xd4000001u|SVC_H_SHIFT<<5)){
   memcpy(c->x,s->x,sizeof(c->x));c->spsr=(u64)s->nz<<28;c->profile_shift_helpers=0;h_shift();memcpy(s->x,c->x,sizeof(c->x));continue;
  }
  if((w&0x1f800000u)==0x12800000u){
   unsigned opc=(w>>29)&3,shift=((w>>21)&3)*16;u64 v=(u64)((w>>5)&0xffff)<<shift;
   if(opc==2)writereg(s,rd,v,sf);else if(opc==3)writereg(s,rd,(readreg(s,rd)&~(0xffffULL<<shift))|v,sf);else check(0);continue;
  }
  if((w&0x1f800000u)==0x13000000u){
   unsigned bits=sf?64:32,r=(w>>16)&(bits-1),last=(w>>10)&(bits-1),opc=(w>>29)&3;u64 v;
   if(r<=last){unsigned width=last-r+1;u64 m=mask(width);v=(a>>r)&m;if(opc==1)v=(readreg(s,rd)&~m)|v;else check(opc==2);}
   else{unsigned lsb=bits-r,width=last+1;u64 m=mask(width)<<lsb;v=(a&mask(width))<<lsb;if(opc==1)v=(readreg(s,rd)&~m)|v;else check(opc==2);}
   writereg(s,rd,v,sf);continue;
  }
  if((w&0x1f000000u)==0x0a000000u){
   check(!(w&0x0060fc00u));unsigned opc=(w>>29)&3;u64 b=readreg(s,rm)&mask(sf?64:32),v;
   if(opc==1)v=a|b;else if(opc==3)v=a&b;else{check(0);v=0;}writereg(s,rd,v,sf);
   if(opc==3)s->nz=(v&(1ULL<<(sf?63:31))?8:0)|(!v?4:0);continue;
  }
  fprintf(stderr,"unmodelled ARM word %08x at %u\n",w,i);check(0);
 }
}
static Code emitted(const Insn*in,int live,int candidate){Code r={0};current_words=r.words;tr_cc=r.words;if(candidate)candidate_emit_int(in,live);else baseline_emit_int(in,live);r.count=(unsigned)(tr_cc-r.words);check(r.count<=256);return r;}
static Insn decoded(unsigned size,unsigned kind,unsigned memory,unsigned cl,unsigned count,unsigned reg,int compat,int rex){
 tr_compat=compat;input_size=0;if(size==2)input[input_size++]=0x66;
 if(!compat&&(size==8||reg>=8||rex))input[input_size++]=(u8)(0x40|(size==8?8:0)|(reg>=8?1:0));
 input[input_size++]=(u8)(cl?(size==1?0xd2:0xd3):(size==1?0xc0:0xc1));input[input_size++]=(u8)((memory?0:0xc0)|(kind<<3)|(reg&7));if(!cl)input[input_size++]=(u8)count;
 Insn in;x86_decode(0x400000,&in);check(in.op==OP_SHIFT&&in.sz==size&&in.next==0x400000+(u64)input_size);return in;
}
static State initial(const Insn*in,unsigned byte,unsigned count,unsigned live,int compat){
 State s={0};for(unsigned r=0;r<31;r++)s.x[r]=UINT64_C(0x94d049bb133111eb)*(r+1)^((u64)byte*UINT64_C(0x9e3779b97f4a7c15));
 if(compat)for(unsigned r=0;r<16;r++)s.x[r]&=0xffffffff;
 unsigned shift=in->high_rm?8:0,target=in->high_rm?in->rm-4:in->rm;s.x[target]=(s.x[target]&~(mask(PROOF_BITS)<<shift))|((u64)byte<<shift);
 unsigned flags=(byte^live^(count*7))&63;s.nz=(flags&8?8:0)|(flags&4?4:0)|(flags&1?0:2)|(flags&2?1:0);
 s.x[R_PF]=flags&16?0:1;s.x[R_AF]=s.x[R_PF]^(flags&32?16:0);return s;
}
static int correct(const Insn*in,unsigned byte,unsigned count,unsigned live,const State*before,const State*got){
 unsigned n=count&31,shift=in->high_rm?8:0,target=in->high_rm?in->rm-4:in->rm,result=n?(n>=PROOF_BITS?0:byte>>n):byte;
 u64 expected=(before->x[target]&~(mask(PROOF_BITS)<<shift))|((u64)result<<shift);
 for(unsigned r=0;r<16;r++)if(got->x[r]!=(r==target?expected:before->x[r]))return 0;
 unsigned wanted=live|(live&F_A?F_P:0),nz=before->nz;
 if(n){unsigned cf=n<=PROOF_BITS?((byte>>(n-1))&1):0;nz=(!result?4:0)|(cf?0:2)|(byte&(1u<<(PROOF_BITS-1))?1:0);}
 if((wanted&F_N)&&((got->nz^nz)&8))return 0;if((wanted&F_Z)&&((got->nz^nz)&4))return 0;
 if((wanted&F_C)&&((got->nz^nz)&2))return 0;if((wanted&F_V)&&((got->nz^nz)&1))return 0;
 if(wanted&F_P){unsigned parity=!(__builtin_popcount(result&255)&1),observed=!(__builtin_popcount((unsigned)got->x[R_PF]&255)&1);if(observed!=(n?parity:!(__builtin_popcount((unsigned)before->x[R_PF]&255)&1)))return 0;}
 if(wanted&F_A){unsigned af=(unsigned)((got->x[R_AF]^got->x[R_PF])>>4)&1,old=(unsigned)((before->x[R_AF]^before->x[R_PF])>>4)&1;if(af!=(n?0:old))return 0;}
 return 1;
}
static int has_helper(const Code*code){for(unsigned i=0;i<code->count;i++)if(code->words[i]==(0xd4000001u|SVC_H_SHIFT<<5))return 1;return 0;}
static void exhaustive(void){
 const unsigned regs[]={0,1,4,7,4,9},rexs[]={0,0,0,0,1,0},extra[]={0,32,64,255};
 for(unsigned compat=0;compat<2;compat++)for(unsigned reg=0;reg<6;reg++){
  if(compat&&reg>=4)continue;
  for(unsigned ci=0;ci<35;ci++){unsigned count=ci<31?ci+1:extra[ci-31];
   for(unsigned live=0;live<64;live++){
    Insn in=decoded(PROOF_BITS/8,SH_SHR,0,0,count,regs[reg],compat,rexs[reg]);
    tr_inline_shr8=false;Code old=emitted(&in,live,0),off=emitted(&in,live,1);
    if(PROOF_BITS==8)check(old.count==off.count&&!memcmp(old.words,off.words,old.count*4));layouts++;
    tr_inline_shr8=PROOF_BITS==8;Code on=emitted(&in,live,1);int eligible=(count&31)&&live;
    if(eligible){check(has_helper(&old)&&!has_helper(&on));helper_layouts++;}
    else check(old.count==on.count&&!memcmp(old.words,on.words,old.count*4));
    for(unsigned sample=0;sample<(PROOF_BITS==8?256:528);sample++){
     unsigned byte=sample<256?sample:sample<512?(sample-256)*257:1u<<(sample-512);
     State before=initial(&in,byte,count,live,compat),a=before,b=before;evaluate(&a,&old);evaluate(&b,&on);
     check(correct(&in,byte,count,live,&before,&a)&&correct(&in,byte,count,live,&before,&b));
     states++;if(!(count&31))zero_states++;if(live==F_A)af_only++;
    }
   }
  }
 }
}
static void unchanged(void){
 const unsigned counts[]={0,1,7,8,9,16,31,32,63,255};
 for(unsigned size=1;size<=8;size*=2)for(unsigned kind=0;kind<8;kind++)for(unsigned mem=0;mem<2;mem++)
 for(unsigned cl=0;cl<2;cl++)for(unsigned count=0;count<10;count++)for(unsigned live=0;live<64;live++)for(unsigned profile=0;profile<2;profile++){
  if(!mem&&size==PROOF_BITS/8&&kind==SH_SHR&&!cl)continue;
  tr_profile_shift_helpers=profile;Insn in=decoded(size,kind,mem,cl,counts[count],3,0,0);tr_inline_shr8=false;Code old=emitted(&in,live,0),off=emitted(&in,live,1);tr_inline_shr8=PROOF_BITS==8;Code on=emitted(&in,live,1);
  check(old.count==off.count&&!memcmp(old.words,off.words,old.count*4)&&old.count==on.count&&!memcmp(old.words,on.words,old.count*4));controls++;
 }
 tr_profile_shift_helpers=false;
}
static void negative_controls(void){
 const unsigned counts[]={PROOF_BITS,2,3},lives[]={F_ARITH,F_ARITH,F_A};
 for(unsigned k=0;k<3;k++){
  Insn in=decoded(PROOF_BITS/8,SH_SHR,0,0,counts[k],0,0,0);tr_inline_shr8=PROOF_BITS==8;Code bad=emitted(&in,lives[k],1);unsigned changed=0;
  for(unsigned i=0;i<bad.count;i++){
   if(k<2&&is_rmif(bad.words[i])&&(bad.words[i]&15)==(k==0?2u:1u)){bad.words[i]=0xd503201fu;changed++;break;}
   if(k==2&&(bad.words[i]&0xffe0ffffu)==(0xaa0003e0u|R_AF)){bad.words[i]=0xaa1f03e0u|R_AF;changed++;break;}
  }
  check(changed==1);unsigned value=k==2?128:1u<<(PROOF_BITS-1);State before=initial(&in,value,counts[k],lives[k],0),got=before;evaluate(&got,&bad);check(!correct(&in,value,counts[k],lives[k],&before,&got));negative++;
 }
}
int main(void){exhaustive();unchanged();negative_controls();printf("{\"result\":\"PASS_DATA_ONLY\",\"operand_bits\":%u,\"checks\":%"PRIu64",\"operand_count_live_reg_mode_states\":%"PRIu64",\"emission_layouts\":%"PRIu64",\"unaffected_controls\":%"PRIu64",\"helper_eliminated_layouts\":%"PRIu64",\"zero_count_states\":%"PRIu64",\"AF_only_states\":%"PRIu64",\"negative_controls_detected\":%"PRIu64",\"generated_ARM_executed\":false,\"HV_or_guest_executed\":false}\n",PROOF_BITS,checks,states,layouts,controls,helper_layouts,zero_states,af_only,negative);return 0;}
'''


def prepare(engine,work,bits=8):
    src=engine/'Sources/CEngine';candidate=(src/'emit_int.c').read_text();baseline=baseline_emitter(candidate,bits)
    monitor=(engine/'Monitor/monitor.c').read_text()
    helpers='\n\n'.join(function(monitor,n) for n in ['cur_nzcv','helper_flags','helper_keep_flags','count_shift_helper','h_shift'])
    fixture=FIXTURE.replace('@HELPERS@',helpers).replace('@BITS@',str(bits))
    work.mkdir(exist_ok=False);target=work/'input';(target/'include').mkdir(parents=True)
    names=['x86_decode.c','tr_internal.h','include/engine_abi.h','include/translator.h']
    for n in names:(target/n).write_bytes((src/n).read_bytes())
    (target/'emit_int-baseline.c').write_text(baseline);(target/'emit_int-candidate.c').write_text(candidate)
    (work/'shr8-immediate.c').write_text(fixture)
    record={'scope':'actual production decoder/emitter and monitor helper; narrow ARM data evaluator plus independent x86 byte/flags oracle; no generated ARM/HV/guest execution',
            'candidate_sha256':hashlib.sha256(candidate.encode()).hexdigest(),'baseline_sha256':hashlib.sha256(baseline.encode()).hexdigest(),
            'proof_sha256':hashlib.sha256(fixture.encode()).hexdigest(),'production_inputs':{n:hashlib.sha256((src/n).read_bytes()).hexdigest() for n in names},
            'following_fault_integration':'not executed; parent-owned guest fault probe must qualify before promotion'}
    (work/'input-receipt.json').write_text(json.dumps(record,indent=2)+'\n')
    rename=['movimm','logimm','addr_reg','addr_prepare','ldst','gpr_read','gpr_write','emit_parity','emit_int'];flags=['-std=c11','-O2','-Wall','-Wextra','-Werror','-I'+str(target),'-I'+str(target/'include')]
    for v in ['baseline','candidate']:
        print(shlex.join(['nice','-n','10','xcrun','clang',*flags,*['-D'+n+'='+v+'_'+n for n in rename],'-c',str(target/('emit_int-'+v+'.c')),'-o',str(work/(v+'.o'))]))
    print(shlex.join(['nice','-n','10','xcrun','clang',*flags,str(work/'shr8-immediate.c'),str(target/'x86_decode.c'),str(work/'baseline.o'),str(work/'candidate.o'),'-o',str(work/'shr8-immediate')]))
    print(shlex.join(['nice','-n','10',str(work/'shr8-immediate')]))
    return record


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--engine',type=Path,default=Path(__file__).resolve().parents[1]);parser.add_argument('--work',type=Path,required=True);parser.add_argument('--bits',type=int,choices=(8,16),default=8);a=parser.parse_args();prepare(a.engine.resolve(),a.work.resolve(),a.bits)
