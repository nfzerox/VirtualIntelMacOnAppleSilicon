// DATA-ONLY production decoder/emitter proof. Generated ARM words are never executed.
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tr_internal.h"
void baseline_emit_int(const Insn *,int);
void candidate_emit_int(const Insn *,int);
bool baseline_logimm(u64,int,u32 *);
bool tr_precise_flags=true,tr_has_tzcnt=false,tr_hybrid=true,tr_no_bulk_rep=false,tr_compat=false;
bool tr_profile_shift_helpers=false;
bool tr_inline_shr8=false;
int tr_cpl=3,cur_part=0;u64 tr_stub_lookup,tr_stub_ret_miss,tr_stub_pushf,tr_stub_sahf;
u32 *tr_cc;static u32 *current_words;
u64 tr_cc_va(const u32 *p){return 0x10000000ULL+(u64)(p-current_words)*4;}
void emit_stop(u64 rip,const char *why){(void)rip;(void)why;svc(SVC_UD);}
static u8 input[32];static int input_size;
int tr_fetch(u64 va,u8 *out,int n){if(va<0x400000 || va-0x400000>=(u64)input_size)return 0;int left=input_size-(int)(va-0x400000);if(n>left)n=left;memcpy(out,input+va-0x400000,(size_t)n);return n;}
static uint64_t layouts,states,controls,memory_cmp,words_saved,raw_af_diffs,scratch_diffs;
static unsigned maximum_words;
static void die(const char *why){fprintf(stderr,"AF_PROOF_FAIL %s layouts=%"PRIu64" states=%"PRIu64"\n",why,layouts,states);exit(1);}
#define CHECK(x,msg) do{if(!(x))die(msg);}while(0)
typedef struct{u32 w[256];int n,af,start;} Code;
typedef struct{u64 x[32];unsigned nz;} State;
static u64 mask_for(int sf){return sf?~0ULL:0xffffffffULL;}
static u64 rdreg(const State *s,int r){return r==31?0:s->x[r];}
static void wrreg(State *s,int r,u64 v,int sf){if(r!=31)s->x[r]=v&mask_for(sf);}
static int logical_reg(u32 w){return (w&0x1F000000u)==0x0A000000u;}
static int logical_imm(u32 w){return (w&0x1F800000u)==0x12000000u;}
static int move_wide(u32 w){return (w&0x7F800000u)==0x52800000u;}
static u64 logical_mask(u32 w){ // ARM DecodeBitMasks, independent of engine logimm encoder.
 unsigned n=(w>>22)&1,imms=(w>>10)&63,r=(w>>16)&63,v=(n<<6)|((~imms)&63),len=0;
 CHECK(v>=2,"logical mask length");while(v>>=1)len++;unsigned bits=1u<<len,levels=bits-1,s=imms&levels;r&=levels;
 CHECK(s!=levels,"reserved logical immediate");u64 element=s==63?~0ULL:((1ULL<<(s+1))-1),emask=bits==64?~0ULL:((1ULL<<bits)-1);
 if(r)element=((element>>r)|(element<<(bits-r)))&emask;
 u64 result=0;for(unsigned i=0;i<64;i+=bits)result|=element<<i;return result;
}
static int eval(State *s,const Code *c,int start){
 for(int i=start;i<c->n;i++){u32 w=c->w[i];int sf=w>>31,rd=w&31,rn=(w>>5)&31,rm=(w>>16)&31;u64 mask=mask_for(sf),a=rdreg(s,rn)&mask,b=0,result;
  if(w==0xD500401Fu){s->nz^=2;continue;}
  if(move_wide(w)){u64 imm=(w>>5)&0xffff;wrreg(s,rd,imm<<(((w>>21)&3)*16),sf);continue;}
  if(logical_imm(w)){CHECK(((w>>29)&3)==2,"only EOR logical immediate in proof");wrreg(s,rd,a^logical_mask(w),sf);continue;}
  if(logical_reg(w)){unsigned opc=(w>>29)&3;CHECK(!(w&0x0060fc00u),"unmodelled shifted/inverted logical register");b=rdreg(s,rm)&mask;
   CHECK(opc==1||opc==2,"only MOV/EOR register in proof");wrreg(s,rd,opc==1?(a|b):(a^b),sf);continue;}
  if((w&0x1F000000u)==0x11000000u){int sub=(w>>30)&1,set=(w>>29)&1;b=(w>>10)&4095;if((w>>22)&1)b<<=12;
   result=(sub?a-b:a+b)&mask;wrreg(s,rd,result,sf);
   if(set){u64 sign=sf?(1ULL<<63):(1ULL<<31);unsigned carry=sub?a>=b:(sf?result<a:a+b>mask);unsigned overflow=((sub?(a^b):~(a^b))&(a^result)&sign)!=0;
    s->nz=(result&sign?8:0)|(!result?4:0)|(carry?2:0)|overflow;}
   continue;}
  return 0;
 }
 return 1;
}
static int writes_af(u32 w){return (logical_imm(w)||logical_reg(w)||move_wide(w))&&(w&31)==R_AF;}
static Code emit(const Insn *in,int live,int candidate){Code c={0};current_words=c.w;tr_cc=c.w;if(candidate)candidate_emit_int(in,live);else baseline_emit_int(in,live);c.n=(int)(tr_cc-c.w);CHECK(c.n>=0&&c.n<=256,"emit array bound");if((unsigned)c.n>maximum_words)maximum_words=(unsigned)c.n;c.af=-1;c.start=0;for(int i=0;i<c.n;i++)if(writes_af(c.w[i])){c.af=i;c.start=i;if(i&&move_wide(c.w[i-1])&&(c.w[i-1]&31)==S1)c.start--;break;}return c;}
static Insn decode(int sz,int alu,int reg,i64 imm,int mem,int lock,int compat,int accumulator){
 tr_compat=compat;input_size=0;if(lock)input[input_size++]=0xf0;if(sz==2)input[input_size++]=0x66;
 if(!compat&&(sz==8||reg>=8))input[input_size++]=(u8)(0x40|(sz==8?8:0)|(reg>=8?1:0));
 if(accumulator){input[input_size++]=(u8)(alu==ALU_ADD?0x05:alu==ALU_SUB?0x2d:0x3d);}
 else{input[input_size++]=sz==1?0x80:0x81;input[input_size++]=(u8)((mem?0x00:0xc0)|((alu==ALU_ADD?0:alu==ALU_SUB?5:7)<<3)|(reg&7));}
 int bytes=sz==1?1:sz==2?2:4;for(int i=0;i<bytes;i++)input[input_size++]=(u8)((u64)imm>>(i*8));
 Insn in;x86_decode(0x400000,&in);CHECK(in.next==0x400000+(u64)input_size,"production decode consumed expected instruction");return in;
}
static void layout_check(const Insn *in,int live,Code b,Code c,int eligible){
 layouts++;if(!eligible||!(live&F_A)){CHECK(b.n==c.n&&!memcmp(b.w,c.w,(size_t)b.n*4),"outside change domain must be byte-identical");return;}
 CHECK(b.af>=0&&c.af>=0,"AF prefix present");CHECK(b.start==c.start,"same preload before AF");CHECK(!memcmp(b.w,c.w,(size_t)b.start*4),"pre-AF load/address bytes unchanged");
 CHECK(c.af==c.start,"candidate AF prefix exactly one word");CHECK(b.af-b.start<=1,"baseline AF prefix one/two words");
 CHECK(b.n-b.af==c.n-c.af&&!memcmp(b.w+b.af+1,c.w+c.af+1,(size_t)(b.n-b.af-1)*4),"non-AF tail exactly unchanged");
 CHECK((c.w[c.af]&31)==R_AF,"candidate writes only AF register");for(int i=c.af+1;i<c.n;i++){u32 w=c.w[i];if(w==0xD500401Fu)continue;CHECK(((w>>5)&31)!=S1,"non-AF tail must not read S1");if(logical_reg(w))CHECK(((w>>16)&31)!=S1,"non-AF MOV tail must not read S1");}
 words_saved+=(u64)(b.n-c.n);(void)in;
}
static int state_ok(const Insn *in,int live,const Code *b,const Code *c,u64 value,int symbolic_memory,int negative){
 State initial={0};for(int r=0;r<31;r++)initial.x[r]=0x9e3779b97f4a7c15ULL*(u64)(r+1)^value;initial.nz=(unsigned)(value>>28)&15;
 int d=symbolic_memory?S0:in->rm;initial.x[d]=value;State x=initial,y=initial;
 if(!eval(&x,b,symbolic_memory?b->start:0)||!eval(&y,c,symbolic_memory?c->start:0))die("unmodelled generated instruction");
 int sf=in->sz==8,sub=in->sub!=ALU_ADD;u64 mask=mask_for(sf),a=value&mask,imm=(u64)in->imm&mask,result=(sub?a-imm:a+imm)&mask,sign=sf?1ULL<<63:1ULL<<31;
 for(int r=0;r<31;r++){if(r==S1)continue;if(r==R_AF){if((x.x[r]^y.x[r])&16)return 0;}else if(x.x[r]!=y.x[r])return 0;}
 if(x.nz!=y.nz)return 0;
 for(int r=0;r<16;r++){u64 expected=initial.x[r];if(in->sub!=ALU_CMP&&r==in->rm)expected=result;if(x.x[r]!=expected||y.x[r]!=expected)return 0;}
 if(live&(F_P|F_A)){if(x.x[R_PF]!=(u32)result||y.x[R_PF]!=(u32)result)return 0;}
 if(live&F_A){unsigned expected=sub?((a&15)<(imm&15)):((a&15)+(imm&15)>15);if((((x.x[R_AF]^x.x[R_PF])>>4)&1)!=expected||(((y.x[R_AF]^y.x[R_PF])>>4)&1)!=expected)return 0;}
 if(live&F_P){unsigned expected=!(__builtin_popcount((unsigned)result&255)&1);if((!(__builtin_popcount((unsigned)y.x[R_PF]&255)&1))!=expected)return 0;}
 if(live&F_N)if(!!(y.nz&8)!=!!(result&sign))return 0;
 if(live&F_Z)if(!!(y.nz&4)!=(result==0))return 0;
 if(live&F_C){unsigned carry=sub?a<imm:(sf?result<a:a+imm>mask);if((!(y.nz&2))!=carry)return 0;}
 if(live&F_V){unsigned overflow=((sub?(a^imm):~(a^imm))&(a^result)&sign)!=0;if((y.nz&1)!=overflow)return 0;}
 if(!negative){states++;raw_af_diffs+=x.x[R_AF]!=y.x[R_AF];scratch_diffs+=x.x[S1]!=y.x[S1];}return 1;
}
static u64 highs(int n){static const u64 h[]={0,0xffffffffffffffe0ULL,0x7fffffffffffffe0ULL,0x8000000000000000ULL,0x000000007fffffe0ULL,0x0000000080000000ULL,0xaaaaaaaaaaaaaaa0ULL,0x5555555555555540ULL};return h[n&7];}
int main(void){
 const int alus[]={ALU_ADD,ALU_SUB,ALU_CMP};
 for(int mode=0;mode<3;mode++){int sz=mode==1?8:4,compat=mode==2;
  for(int k=0;k<3;k++)for(int imm=0;imm<4096;imm++)for(int live=0;live<64;live++){
   int reg=(imm^live)&(compat?7:15);Insn in=decode(sz,alus[k],reg,imm,0,0,compat,0);CHECK(in.op==OP_ALU&&in.sz==sz&&in.sub==alus[k]&&in.rm_reg&&in.has_imm&&!in.to_reg&&in.rm==reg,"decoded main coverage");
   Code b=emit(&in,live,0),c=emit(&in,live,1);layout_check(&in,live,b,c,1);
   for(int low=0;low<32;low++)CHECK(state_ok(&in,live,&b,&c,highs(imm+live+low+k+mode)|(u64)low,0,0),"architectural/x86 truth mismatch");
  }
 }
 const i64 edges[]={0,1,15,16,17,31,32,2047,2048,4095};
 for(int sf=0;sf<2;sf++)for(int k=0;k<3;k++)for(unsigned e=0;e<sizeof(edges)/sizeof(edges[0]);e++)for(int reg=0;reg<16;reg++){
  Insn in=decode(sf?8:4,alus[k],reg,edges[e],0,0,0,0);Code b=emit(&in,F_ARITH,0),c=emit(&in,F_ARITH,1);layout_check(&in,F_ARITH,b,c,1);
  for(int high=0;high<8;high++)for(int low=0;low<32;low++)CHECK(state_ok(&in,F_ARITH,&b,&c,highs(high)|(u64)low,0,0),"edge/high/GPR truth mismatch");
 }
 // Memory CMP is eligible. Never evaluate its memory/address instructions: assert
 // unchanged preload bytes, then model successful loaded S0 values symbolically.
 for(int sf=0;sf<2;sf++)for(int imm=0;imm<4096;imm++){Insn in=decode(sf?8:4,ALU_CMP,7,imm,1,0,0,0);Code b=emit(&in,F_ARITH,0),c=emit(&in,F_ARITH,1);layout_check(&in,F_ARITH,b,c,1);CHECK(c.start>0,"memory CMP load precedes AF");memory_cmp++;for(int low=0;low<32;low++)CHECK(state_ok(&in,F_ARITH,&b,&c,highs(imm+low)|(u64)low,1,0),"symbolic memory CMP truth");}
 // Unaffected forms: outside immediate domain, narrow, memory RMW, LOCK; no execution.
 const i64 other[]={-2147483647LL,-4096,-1,4096,4097,65535,0x12345678};
 for(int sz=1;sz<=8;sz*=2)for(int k=0;k<3;k++)for(int mem=0;mem<2;mem++)for(int lock=0;lock<2;lock++)for(unsigned e=0;e<sizeof(other)/sizeof(other[0]);e++){
  Insn in=decode(sz,alus[k],7,other[e],mem,lock,0,0);Code b=emit(&in,F_ARITH,0),c=emit(&in,F_ARITH,1);int eligible=in.op==OP_ALU&&in.sz>=4&&!in.lock&&(!mem||in.sub==ALU_CMP)&&in.has_imm&&!in.to_reg&&((u64)in.imm<4096);
  if(!eligible){layout_check(&in,F_ARITH,b,c,0);controls++;}
 }
 // Within-domain memory RMW and LOCK stay outside the modified fast path.
 for(int sz=4;sz<=8;sz*=2)for(int k=0;k<2;k++)for(int lock=0;lock<2;lock++)for(int imm=0;imm<4096;imm++){
  Insn in=decode(sz,alus[k==0?0:1],7,imm,1,lock,0,0);Code b=emit(&in,F_ARITH,0),c=emit(&in,F_ARITH,1);layout_check(&in,F_ARITH,b,c,0);controls++;
 }
 for(int sz=1;sz<=2;sz*=2)for(int k=0;k<3;k++)for(int mem=0;mem<2;mem++)for(int lock=0;lock<2;lock++)for(int imm=0;imm<4096;imm++){
  Insn in=decode(sz,alus[k],7,imm,mem,lock,0,0);Code b=emit(&in,F_ARITH,0),c=emit(&in,F_ARITH,1);layout_check(&in,F_ARITH,b,c,0);controls++;
 }
 for(int sz=4;sz<=8;sz*=2)for(int k=0;k<3;k++)for(int imm=0;imm<4096;imm+=17){Insn in=decode(sz,alus[k],0,imm,0,0,0,1);Code b=emit(&in,F_ARITH,0),c=emit(&in,F_ARITH,1);layout_check(&in,F_ARITH,b,c,1);for(int low=0;low<32;low++)CHECK(state_ok(&in,F_ARITH,&b,&c,highs(imm+low)|low,0,0),"accumulator decode path");}
 unsigned negative=0;{Insn in=decode(8,ALU_ADD,5,16,0,0,0,0);Code b=emit(&in,F_ARITH,0),c=emit(&in,F_ARITH,1);u32 enc;CHECK(baseline_logimm(8,1,&enc),"negative encoding");c.w[c.af]=0xD2000000u|enc|5u<<5|R_AF;negative+=!state_ok(&in,F_ARITH,&b,&c,0,0,1);}
 {Insn in=decode(8,ALU_SUB,3,5,0,0,0,0);Code b=emit(&in,F_ARITH,0),c=emit(&in,F_ARITH,1);c.w[c.af]=0xAA000000u|31u<<16|31u<<5|R_AF;negative+=!state_ok(&in,F_ARITH,&b,&c,16,0,1);}
 {Insn in=decode(4,ALU_CMP,2,7,0,0,0,0);Code b=emit(&in,F_ARITH,0),c=emit(&in,F_ARITH,1);CHECK((c.w[c.n-1]&31)==R_PF,"negative PF tail");c.n--;negative+=!state_ok(&in,F_ARITH,&b,&c,0x12345670,0,1);}
 CHECK(negative==3,"negative controls must fail");CHECK(raw_af_diffs&&scratch_diffs&&words_saved,"proof must exercise representation/scratch/code-size differences");
 printf("{\"result\":\"PASS_DATA_ONLY\",\"layout_pairs\":%"PRIu64",\"architectural_state_pairs\":%"PRIu64",\"unaffected_controls\":%"PRIu64",\"symbolic_memory_CMP_pairs\":%"PRIu64",\"aggregate_words_saved_across_coverage\":%"PRIu64",\"raw_AF_nonobservable_difference_cases\":%"PRIu64",\"S1_scratch_difference_cases\":%"PRIu64",\"negative_controls_detected\":%u,\"maximum_emitted_words\":%u,\"generated_ARM_executed\":false,\"guest_or_HV_or_codec_executed\":false}\n",layouts,states,controls,memory_cmp,words_saved,raw_af_diffs,scratch_diffs,negative,maximum_words);
 return 0;
}
