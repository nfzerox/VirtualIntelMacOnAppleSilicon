// Pure production decoder/emitter fixture. ARM words are never mapped executable.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "tr_internal.h"
bool tr_precise_flags=true,tr_has_tzcnt=false,tr_hybrid=true,tr_no_bulk_rep=false,tr_compat=false;
bool tr_profile_shift_helpers=false;
bool tr_inline_shr8=false;
int tr_cpl=3,cur_part=0;
u64 tr_stub_lookup,tr_stub_ret_miss,tr_stub_pushf,tr_stub_sahf;
static u32 encoded[65536];u32 *tr_cc=encoded;
u64 tr_cc_va(const u32*p){return 0x10000000ULL+(u64)(p-encoded)*4;}
void emit_stop(u64 rip,const char*why){(void)rip;(void)why;svc(SVC_UD);}
static u8 bytes[32];static u64 code_base;static int code_size;static unsigned cases;
int tr_fetch(u64 va,u8*out,int n){
 u64 off=va-code_base;
 if(off>=(u64)code_size)return 0;
 if((u64)n>(u64)code_size-off)n=(int)((u64)code_size-off);
 memcpy(out,bytes+off,(size_t)n);return n;
}
static void fail(const char*name,const char*reason){fprintf(stderr,"FAIL %s: %s\n",name,reason);exit(1);}
static void emit_case(const char*name,const u8*input,int n,u64 rip,int expected_elision,int expected_guards,
                      int expected_riprel,int expected_seg,int expected_addr32,int expected_rmreg,int compat){
 assert(n>0 && n<=15);memcpy(bytes,input,(size_t)n);code_base=rip;code_size=n;tr_compat=compat;
 Insn in;x86_decode(rip,&in);
 if(in.op!=OP_SSE || in.next-rip!=(u64)n)fail(name,"decode/opcode/length");
 if(in.riprel!=expected_riprel || in.seg!=expected_seg || in.addr32!=expected_addr32 || in.rm_reg!=expected_rmreg)
  fail(name,"unexpected addressing metadata");
 // Obtain the actual address register with the SAME production backend. No alignment predicate here.
 tr_cc=encoded;int address_register=in.rm_reg?AD:addr_reg(&in);
 u32 guard[3];u32 *saved=tr_cc;tr_cc=guard;
 ubfx(1,S0,address_register,0,4);cbz(0,1,S0,2);svc(SVC_GP0);assert(tr_cc==guard+3);tr_cc=saved;
 tr_cc=encoded;emit_sse(&in,F_ARITH);size_t count=(size_t)(tr_cc-encoded);
 if(count>=sizeof(encoded)/sizeof(*encoded))fail(name,"emission overflow");
 printf("%s{\"name\":\"%s\",\"expected_elision\":%d,\"expected_old_guards\":%d,",cases++?",\n":"[\n",name,expected_elision,expected_guards);
 printf("\"rip\":\"0x%llx\",\"next\":\"0x%llx\",\"disp\":%lld,\"guard\":[%u,%u,%u],\"words\":[",
  (unsigned long long)in.rip,(unsigned long long)in.next,(long long)in.disp,guard[0],guard[1],guard[2]);
 for(size_t i=0;i<count;i++)printf("%s%u",i?",":"",encoded[i]);
 printf("]}");
}
static void rip_case(const char*name,const u8*op,int opn,u64 rip,u64 target,int prefix,int elide,int guards){
 u8 b[16];int k=0;if(prefix)b[k++]=(u8)prefix;
 memcpy(b+k,op,(size_t)opn);k+=opn;
 u64 next=rip+(u64)k+4;u32 displacement=(u32)(target-next);
 // Generated real instruction target is fixed by the explicit test vector, not by the optimization predicate.
 b[k++]=(u8)displacement;b[k++]=(u8)(displacement>>8);b[k++]=(u8)(displacement>>16);b[k++]=(u8)(displacement>>24);
 emit_case(name,b,k,rip,elide,guards,1,prefix==0x64?1:prefix==0x65?2:0,prefix==0x67,0,0);
}
int main(void){
 struct Form {const char*name;u8 op[5];int n;} forms[]={
  {"movaps-load",{0x0f,0x28,0x05},3},{"movaps-store",{0x0f,0x29,0x05},3},
  {"movdqa-load",{0x66,0x0f,0x6f,0x05},4},{"movdqa-store",{0x66,0x0f,0x7f,0x05},4},
  {"addps",{0x0f,0x58,0x05},3},{"pxor",{0x66,0x0f,0xef,0x05},4},
  {"pshufb",{0x66,0x0f,0x38,0x00,0x05},5}};
 // Manual oracle: only the first known target in a16-byte series is aligned.
 static const int elisions[16]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
 for(size_t f=0;f<sizeof(forms)/sizeof(forms[0]);f++){
  for(int offset=0;offset<16;offset++){
   char name[96];snprintf(name,sizeof(name),"%s-rip-offset%d",forms[f].name,offset);
   rip_case(name,forms[f].op,forms[f].n,0x1000,0x2000+(u64)offset,0,elisions[offset],1);
  }
  for(int s=0;s<2;s++){
   char name[96];snprintf(name,sizeof(name),"%s-%s",forms[f].name,s?"GS":"FS");
   rip_case(name,forms[f].op,forms[f].n,0x1000,0x2000,s?0x65:0x64,0,1);
  }
  u8 b[16];memcpy(b,forms[f].op,(size_t)forms[f].n);b[forms[f].n-1]=0x00;
  char name[96];snprintf(name,sizeof(name),"%s-unknown-RAX",forms[f].name);
  emit_case(name,b,forms[f].n,0x1000,0,1,0,0,0,0,0);
 }
 static const u8 movaps[]={0x0f,0x28,0x05};
 rip_case("addr32-wrap-to-zero",movaps,3,0x100000000ULL,0x100000000ULL,0x67,1,1);
 rip_case("addr32-wrap-to-one",movaps,3,0x100000000ULL,0x100000001ULL,0x67,0,1);
 rip_case("negative-displacement",movaps,3,0x1000,0x800,0,1,1);
 rip_case("negative-address-canonical",movaps,3,0xffffffff81000000ULL,0xffffffff81001000ULL,0,1,1);
 rip_case("64bit-add-wrap-to-zero",movaps,3,0xffffffffffffffe0ULL,0,0,1,1);
 rip_case("noncanonical-aligned",movaps,3,0x7fffffffffe0ULL,0x800000000000ULL,0,1,1);
 rip_case("disp-INT32_MIN",movaps,3,0x80000009ULL,0x10,0,1,1);
 rip_case("disp-INT32_MAX",movaps,3,0x10000000aULL,0x180000010ULL,0,1,1);
 static const u8 movups[]={0x0f,0x10,0x05},movss[]={0xf3,0x0f,0x10,0x05};
 for(int off=0;off<2;off++){
  char name[64];snprintf(name,sizeof(name),"movups-checkfalse-%d",off);
  rip_case(name,movups,3,0x1000,0x2000+(u64)off,0,0,0);
  snprintf(name,sizeof(name),"movss-scalar-%d",off);
  rip_case(name,movss,4,0x1000,0x2000+(u64)off,0,0,0);
 }
 static const u8 regmov[]={0x0f,0x28,0xc1},absolute[]={0x0f,0x28,0x05,0,0x20,0,0};
 emit_case("movaps-register-source",regmov,sizeof(regmov),0x1000,0,0,0,0,0,1,0);
 emit_case("compat-absolute-aligned-retains",absolute,sizeof(absolute),0x1000,0,1,0,0,1,0,1);
 static const u8 sib[]={0x0f,0x28,0x04,0x25,0,0x20,0,0};
 emit_case("absolute-SIB-aligned-retains",sib,sizeof(sib),0x1000,0,1,0,0,0,0,0);
 puts("\n]");return 0;
}
