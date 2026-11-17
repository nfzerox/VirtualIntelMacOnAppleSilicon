#!/usr/bin/env python3
"""Emit an actual-C page-REP service fixture. Generated guest words never execute.

Only native libc and RAM/lock/walk stubs run. ARM acquire/release leaf instructions
are replaced with observable data loads/stores; copy/tracking/service logic is actual.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
ENGINE=Path(__file__).resolve().parents[1]

def function(source,name):
    m=re.search(r'(?m)^(?:static )?(?:inline )?[^;{\n]+\b'+name+r'\([^;{]*\)\s*\{',source)
    if not m:raise ValueError('missing actual function '+name)
    end,depth=m.end(),1
    while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[m.start():end]

def typedef(source,name):
    end=source.index('} '+name+';')+len('} '+name+';');start=source.rfind('typedef struct {',0,end)
    return source[start:end]

def emit(path):
    monitor=ENGINE/'Monitor/monitor.c';source=monitor.read_text();header=ENGINE/'Sources/CEngine/include/engine_abi.h'
    names=['stubs_of','rep_page_site','is_ram','is_direct','rep_page_copy','rep_page_tracked','rep_page_fault','rep_page_service']
    bodies='\n\n'.join(function(source,n) for n in names)
    dispatch=function(source,'mon_sync');cut=dispatch.index('            case SVC_DIRECT:')
    dispatch=dispatch[:cut]+'            default: stop(STOP_MONITOR_FAULT,0,0,0,0);\n        }\n    }\n}'
    dispatch=dispatch.replace('void mon_sync(', 'void fixture_dispatch(',1)
    if 'case SVC_REP_PAGE:\n                rep_page_service(page_movs);\n                break;' not in dispatch:raise ValueError('dispatch reviewed path changed')
    if 'if (!page_rep && me->smc_held)' not in dispatch or 'if (!page_rep && me->shadow_stepping)' not in dispatch:raise ValueError('internal service must preserve pending SMC/shadow before eligibility')
    control=(ENGINE/'Sources/Engine/Engine.swift').read_text();translator=(ENGINE/'Sources/CEngine/translate.c').read_text()
    if 'config.page_rep = environment["LC_HVDBT_PAGE_REP"] == "1"' not in control:raise ValueError('explicit default-OFF config missing')
    if 'cfg.page_rep' not in translator:raise ValueError('emitter mode missing')
    prefix=PREFIX.replace('HEADER',json.dumps(str(header))).replace('TYPEDEFS',typedef(source,'MonCpu')+'\n'+typedef(source,'Walk'))
    data=prefix+bodies+'\n'+dispatch+CHECKS;path.write_text(data)
    receipt=dict(schema=1,result='ACTUAL_C_FIXTURE_EMITTED_NOT_YET_RUN',monitor_sha256=hashlib.sha256(source.encode()).hexdigest(),header_sha256=hashlib.sha256(header.read_bytes()).hexdigest(),generated_c_sha256=hashlib.sha256(data.encode()).hexdigest(),actual_functions=names,actual_dispatch_prologue_and_REP_case=True,guest_or_generated_code_executed=False,limits=['x86_walk, faults, locks, backing RAM and integer acquire/release leaves are stubs; no real MMU, SMP race, ARM barrier or executable code runs.','Actual service/copy/tracked/site functions execute on synthetic Walk metadata; current-permission errors are scripted walk returns.','Dispatch slice includes the actual prologue/REP case only; other production dispatch cases and trailing exit logic are not executed.','Ordinary external data writes are modeled as allowed races, never claimed excluded by mon_lock.'])
    path.with_suffix('.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps(receipt,sort_keys=True))

PREFIX=r'''
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include HEADER
typedef uint64_t u64;typedef uint32_t u32;typedef uint16_t u16;typedef uint8_t u8;
TYPEDEFS
static unsigned checks;
static void check(int ok,const char *s,int line){checks++;if(!ok){fprintf(stderr,"FAIL page-REP fixture line=%d: %s\n",line,s);exit(1);}}
#define CHECK(x) check((x),#x,__LINE__)
static EngineCtx context,*c=&context;static MonCpu local,*me=&local;
static _Alignas(4096) u8 ram[16*4096];static u8 page_info[16];
#undef RAM_ALIAS_VA
#define RAM_ALIAS_VA ((u64)(uintptr_t)ram)
#define RAM(gpa) ((void *)(ram+(gpa)))
#define RF_TF 0x100ULL
#define X_A 0x20ULL
#define X_D 0x40ULL
#define pt_step_slot (me->pt_step_slot)
#define LOCK_REQUIRED() CHECK(me->lock_depth==1)
static unsigned locks,unlocks,walks,loads,stores,fences,stop_count,fault_count;
static unsigned fail_walk,untracked_walk,mismatch_walk,bad_chain_walk,direct_walk,nonram_walk;
static unsigned final_remap,final_alias,external_write,allow_ad_race,leaf_level;
static bool no_store;
static u32 failure_error;static u64 fault_va,fault_error,fault_rip;
static u64 src_frame,dst_frame,initial_count,initial_src,initial_dst,initial_regs[sizeof(((EngineCtx*)0)->x)/sizeof(u64)],head;
static u32 element_size,expected_bytes;static bool operation_movs;
static jmp_buf stop_escape;
static u64 source_low,source_high,dest_low,dest_high;
static void mon_lock(void){CHECK(me->lock_depth==0);me->lock_depth=1;locks++;}
static void mon_unlock(void){
 CHECK(me->lock_depth==1);
 if(stores){CHECK(fences==1);CHECK(c->x[1]==initial_count-expected_bytes/element_size);CHECK(c->x[7]==initial_dst+expected_bytes);CHECK(c->x[6]==initial_src+(operation_movs?expected_bytes:0));CHECK(c->stat_rep_page_prefixes==1&&c->stat_rep_page_bytes==expected_bytes);}
 me->lock_depth=0;unlocks++;
}
static void observed_fence(int order){CHECK(order==__ATOMIC_SEQ_CST&&me->lock_depth==1&&stores>0);CHECK(c->x[1]==initial_count&&c->x[6]==initial_src&&c->x[7]==initial_dst);fences++;}
#define __atomic_thread_fence observed_fence
static u64 rep_page_load(const void *p,u32 size){
 CHECK(me->lock_depth==1&&(size==1||size==2||size==4||size==8));u64 at=(u64)((const u8*)p-ram);CHECK(at>=source_low&&at+size<=source_high&&!(at&(size-1)));loads++;u64 value=0;for(u32 i=0;i<size;i++)value|=(u64)((const u8*)p)[i]<<(i*8);return value;
}
static void rep_page_store(void *p,u64 value,u32 size){
 CHECK(me->lock_depth==1&&(size==1||size==2||size==4||size==8));u64 at=(u64)((u8*)p-ram);CHECK(at>=dest_low&&at+size<=dest_high&&!(at&(size-1)));CHECK(c->x[1]==initial_count&&c->x[6]==initial_src&&c->x[7]==initial_dst);stores++;if(!no_store)for(u32 i=0;i<size;i++)((u8*)p)[i]=(u8)(value>>(i*8));
}
static Walk x86_walk(u64 cr3,u64 va,u32 access,bool set_ad){
 LOCK_REQUIRED();CHECK(cr3==0x8000&&set_ad);walks++;bool source=(va>>16)==1;CHECK(access==(u32)((c->cpl==3?4:0)|(source?0:2)));
 if(operation_movs)CHECK(source==(walks&1));else CHECK(!source);
 Walk w={0};w.ok=walks!=fail_walk;w.fault_err=failure_error;if(!w.ok)return w;
 u64 base=source?src_frame:dst_frame;
 if(final_remap&&walks>2)base=(source?6:7)*4096;
 if(final_alias&&!source&&walks==4)base=6*4096;
 w.gpa=base+(va&0xfff);if(nonram_walk==walks)w.gpa=sizeof ram;
 if(direct_walk==walks&&w.gpa<sizeof ram)page_info[w.gpa>>12]|=PI_DIRECT;
 w.leaf_level=(int)leaf_level;
 for(int level=3;level>=w.leaf_level;level--){
  u64 *entry=(u64*)(ram+(8+level)*4096+(source?0:8));w.chain[level]=entry;w.read[level]=*entry;
  if(allow_ad_race)*entry^=X_A|X_D;
 }
 if(untracked_walk==walks)page_info[8+w.leaf_level]&=~PI_PT;
 if(mismatch_walk==walks)*w.chain[w.leaf_level]^=0x1000;
 if(bad_chain_walk==walks)w.chain[w.leaf_level]=(u64*)1;
 if(external_write&&walks==(operation_movs?4:2))ram[source_low]=0x3c;
 return w;
}
static u64 rip_here(u64 pc){CHECK(pc==head);return 0x123456;}
static void raise_gp(u64 error,u64 rip){CHECK(me->lock_depth==0&&error==0);fault_count++;fault_va=~0ULL;fault_error=~0u;fault_rip=rip;c->elr=0xf00d;}
static void raise_pf(u64 va,u32 error,u64 rip){CHECK(me->lock_depth==0);fault_count++;fault_va=va;fault_error=error;fault_rip=rip;c->elr=0xf00d;}
_Noreturn static void stop(u64 a,u64 b,u64 d,u64 e,u64 f){(void)a;(void)b;(void)d;(void)e;(void)f;stop_count++;longjmp(stop_escape,1);}
static void handle_data_abort(bool user){(void)user;CHECK(0);}
'''

CHECKS=r'''
#undef __atomic_thread_fence
static void seed(bool movs,u32 size,u64 count,u32 src_off,u32 dst_off){
 memset(c,0,sizeof *c);memset(me,0,sizeof *me);memset(ram,0x5a,sizeof ram);memset(page_info,PI_RAM,sizeof page_info);
 for(unsigned level=0;level<4;level++){page_info[8+level]|=PI_PT;u64 *entries=(u64*)(ram+(8+level)*4096);entries[0]=entries[1]=0x7007;}
 locks=unlocks=walks=loads=stores=fences=stop_count=fault_count=0;fail_walk=untracked_walk=mismatch_walk=bad_chain_walk=direct_walk=nonram_walk=0;final_remap=final_alias=external_write=allow_ad_race=leaf_level=0;
 c->ram_size=sizeof ram;c->cr3=0x8000;c->cpl=3;c->compat=0;c->hybrid=1;c->model_kernel=1;c->df=1;c->irq_shadow_rip=~0ULL;c->rflags_sys=0x202;
 for(unsigned i=0;i<sizeof c->x/sizeof c->x[0];i++)c->x[i]=0xabc00000+i;
 operation_movs=movs;element_size=size;initial_count=count;initial_src=0x10000+src_off;initial_dst=0x20000+dst_off;src_frame=4096;dst_frame=4*4096;
 head=PRIV_VA+OFF_STUBS+(movs?STUB_OFF_REP_PAGE_MOVS_HEAD:STUB_OFF_REP_PAGE_STOS_HEAD);c->elr=head+4;c->esr=(0x15ULL<<26)|SVC_REP_PAGE;c->x[R_LR]=0x77770000;c->x[R_S1]=32/size;c->x[1]=count;c->x[6]=initial_src;c->x[7]=initial_dst;
 u64 value=0x1122334455667788ULL,pattern=0;for(unsigned i=0;i<8;i++)pattern|=((value>>((i%size)*8))&255)<<(i*8);c->x[R_S2]=pattern;
 memcpy(initial_regs,c->x,sizeof initial_regs);failure_error=5;
 u64 prefix=(4096-dst_off)/size;if(movs&&prefix>(4096-src_off)/size)prefix=(4096-src_off)/size;if(prefix>count)prefix=count;expected_bytes=(u32)(prefix*size);
 source_low=src_frame+src_off;source_high=source_low+expected_bytes;dest_low=dst_frame+dst_off;dest_high=dest_low+expected_bytes;
 for(unsigned i=0;i<4096;i++){ram[src_frame+i]=(u8)(i*17+3);ram[6*4096+i]=(u8)(i*13+7);ram[dst_frame+i]=0xa5;ram[7*4096+i]=0xa5;}
}
static void unchanged(void){CHECK(c->x[1]==initial_count&&c->x[6]==initial_src&&c->x[7]==initial_dst);CHECK(stores==0&&loads==0&&fences==0&&c->stat_rep_page_prefixes==0&&c->stat_rep_page_bytes==0);CHECK(me->lock_depth==0);}
static void declined(bool movs){u64 regs[sizeof c->x/sizeof c->x[0]],flags=c->rflags_sys,df=c->df,irq=c->irq_shadow_rip,smc=me->smc_held;bool shadow=me->shadow_stepping;memcpy(regs,c->x,sizeof regs);rep_page_service(movs);unchanged();CHECK(!memcmp(regs,c->x,sizeof regs)&&c->rflags_sys==flags&&(u64)c->df==df&&c->irq_shadow_rip==irq&&me->smc_held==smc&&me->shadow_stepping==shadow);}
static void registers(void){for(unsigned i=0;i<sizeof c->x/sizeof c->x[0];i++)if(i!=1&&i!=6&&i!=7)CHECK(c->x[i]==initial_regs[i]);CHECK(c->df==1&&c->rflags_sys==0x202);}
static void successful(void){
 rep_page_service(operation_movs);CHECK(locks==1&&unlocks==1&&me->lock_depth==0&&fault_count==0);CHECK(walks==(operation_movs?4u:2u));CHECK(c->elr==(c->x[1]?head:c->x[R_LR]));CHECK(c->stat_rep_page_prefixes==1&&c->stat_rep_page_bytes==expected_bytes);registers();
 for(u32 i=0;i<4096;i++){u64 at=dest_low+i-(dest_low&0xfff);u8 wanted=0xa5;if(at>=dest_low&&at<dest_high){unsigned n=(unsigned)(at-dest_low);wanted=operation_movs?ram[source_low+n]:(u8)(c->x[R_S2]>>((n%8)*8));}CHECK(ram[at]==wanted);}
 CHECK(stores>0&&(operation_movs?loads==stores:loads==0));
}
static void positives(void){
 for(unsigned movs=0;movs<2;movs++)for(unsigned size=1;size<=8;size*=2){
  seed(movs,size,64/size,4080,4064);successful();CHECK(expected_bytes==(movs?16u:32u));
  seed(movs,size,1,128,256);c->hybrid=0;successful();CHECK(c->x[1]==0);
  seed(movs,size,64/size,128,256);c->cpl=0;successful();
  seed(movs,size,64/size,128,256);c->compat=1;successful();
 }
 seed(true,1,63,129,257);successful(); // byte tails never overread or require alignment
 seed(true,1,64,128,256);allow_ad_race=1;successful();
 seed(true,1,64,128,256);external_write=1;successful();CHECK(ram[dest_low]==0x3c); // allowed ordinary-data race, not blocked by lock
 seed(true,1,64,128,256);final_remap=1;source_low=6*4096+128;source_high=source_low+64;dest_low=7*4096+256;dest_high=dest_low+64;successful();
 for(unsigned level=1;level<=3;level++){seed(true,1,64,128,256);leaf_level=level;successful();}
}
static void declines(void){
 for(unsigned which=0;which<18;which++){
  seed(true,1,64,128,256);
  switch(which){case 0:c->x[1]=initial_count=0;break;case 1:c->model_kernel=0;break;case 2:c->cpl=1;break;case 3:c->compat=2;break;case 4:c->df=-1;break;case 5:c->rflags_sys|=RF_TF;break;case 6:me->smc_held=0x1001;break;case 7:me->shadow_stepping=true;break;case 8:c->irq_shadow_rip=0x123456;break;case 9:c->stepping=1;break;case 10:pt_step_slot=1;break;case 11:c->stop_requested=1;break;
  case 12:c->compat=1;c->x[1]=initial_count=0x100000000ULL;break;case 13:c->compat=1;c->x[7]=initial_dst=0xfffffff0;c->x[1]=initial_count=64;break;
  case 14:c->x[7]=initial_dst=c->x[6]+16;break;case 15:c->x[6]=initial_src=c->x[7]+16;break;
  case 16:c->x[R_S1]=4;element_size=8;c->x[6]=initial_src|1;initial_src|=1;break;case 17:c->x[R_S1]=4;element_size=8;c->x[7]=initial_dst|1;initial_dst|=1;break;}
  declined(true);CHECK(locks==0&&walks==0&&c->elr==c->x[R_LR]);
 }
 for(unsigned which=0;which<9;which++){
  seed(true,1,64,128,256);
  switch(which){case 0:untracked_walk=1;break;case 1:untracked_walk=2;break;case 2:mismatch_walk=1;break;case 3:bad_chain_walk=1;break;case 4:direct_walk=1;break;case 5:direct_walk=2;break;case 6:nonram_walk=1;break;case 7:page_info[4]|=PI_CODE;break;case 8:page_info[4]|=PI_PT;break;}
  declined(true);CHECK(locks==1&&unlocks==1&&c->elr==c->x[R_LR]);
 }
 for(unsigned nth=3;nth<=4;nth++)for(unsigned which=0;which<4;which++){seed(true,1,64,128,256);if(which==0)untracked_walk=nth;else if(which==1)mismatch_walk=nth;else if(which==2)direct_walk=nth;else nonram_walk=nth;declined(true);CHECK(walks==nth&&locks==1&&unlocks==1);}
 seed(true,1,64,128,256);dst_frame=src_frame;declined(true);CHECK(walks==2);
 seed(true,1,64,128,256);final_remap=final_alias=1;declined(true);CHECK(walks==4);
 seed(true,1,64,128,256);untracked_walk=1;declined(true);
 for(unsigned level=0;level<4;level++)page_info[8+level]|=PI_PT;untracked_walk=0;locks=unlocks=walks=0;c->elr=head+4;successful(); // explicit scalar-tracking preparation/retry
 seed(true,1,64,128,256);c->ram_size=4*4096+256+8;declined(true); // bounded backing, no partial copy
}
static void faults_and_sites(void){
 for(unsigned nth=1;nth<=4;nth++){
  seed(true,1,64,128,256);fail_walk=nth;rep_page_service(true);unchanged();CHECK(walks==nth&&fault_count==1&&fault_rip==0x123456&&fault_error==5&&fault_va==(nth&1?initial_src:initial_dst)&&c->elr==0xf00d);
 }
 seed(true,1,64,128,256);fail_walk=1;failure_error=~0u;rep_page_service(true);unchanged();CHECK(fault_error==~0u&&fault_va==~0ULL&&c->elr==0xf00d);
 for(unsigned part=0;part<2;part++)for(unsigned movs=0;movs<2;movs++){
  bool found=!movs;u64 pc=PRIV_VA+(part?OFF_STUBS2:OFF_STUBS)+(movs?STUB_OFF_REP_PAGE_MOVS_HEAD:STUB_OFF_REP_PAGE_STOS_HEAD);CHECK(rep_page_site(pc,&found)&&found==(bool)movs);CHECK(!rep_page_site(pc+4,&found));
 }
 for(unsigned which=0;which<3;which++){
  seed(true,1,64,128,256);if(which==0)c->elr+=4;else if(which==1)c->x[R_S1]=3;
  if(!setjmp(stop_escape)){rep_page_service(which==2?false:true);CHECK(0);}CHECK(stop_count==1&&locks==0&&walks==0);
 }
 for(unsigned which=0;which<2;which++){
  seed(true,1,64,128,256);if(which)me->shadow_stepping=true;else me->smc_held=0x1001;
  fixture_dispatch();unchanged();CHECK(which?me->shadow_stepping:me->smc_held==0x1001);CHECK(c->elr==c->x[R_LR]);
 }
 seed(true,1,64,4080,256);successful();u8 saved[16];memcpy(saved,ram+dest_low,16);
 initial_count=c->x[1];initial_src=c->x[6];initial_dst=c->x[7];locks=unlocks=walks=loads=stores=fences=0;fail_walk=1;c->elr=head+4;
 rep_page_service(true);CHECK(fault_count==1&&stores==0&&c->x[1]==48&&memcmp(saved,ram+dest_low,16)==0&&c->stat_rep_page_prefixes==1); // no replay after a committed prefix
}
int main(int argc,char **argv){CHECK(argc==1||(argc==2&&!strcmp(argv[1],"--negative-control-no-store")));no_store=argc==2;positives();declines();faults_and_sites();printf("PASS actual-C page-REP checks=%u guest_generated_code_executed=0\n",checks);return 0;}
'''

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--emit-c',type=Path,required=True);a=p.parse_args();emit(a.emit_c)
