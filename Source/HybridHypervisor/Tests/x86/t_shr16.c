// Native immediate SHR16: full register and defined flags vs x86 reference;
// helper=1 additionally checks the engine's deterministic undefined OF/AF/CF.
#include "flatlibc.h"
typedef struct { uint64_t value, flags; } Result;
typedef Result (*Shift)(uint64_t,uint64_t);
#define SHIFT(N, REG, NAME) \
    static Result NAME##N(uint64_t input,uint64_t flags) { \
        Result r; \
        __asm__ volatile("pushq %[flags]\n popfq\n movq %[input], %%" REG "\n shrw $" #N \
            ", %%" #NAME "\n pushfq\n popq %[after]\n movq %%" REG ", %[value]" \
            : [value] "=&r"(r.value), [after] "=&r"(r.flags) \
            : [input] "r"(input), [flags] "r"(flags) : REG,"cc","memory");return r; }
#define CASE(N) SHIFT(N,"rdx",dx) SHIFT(N,"r8",r8w)
CASE(0) CASE(1) CASE(2) CASE(3) CASE(4) CASE(5) CASE(6) CASE(7)
CASE(8) CASE(9) CASE(10) CASE(11) CASE(12) CASE(13) CASE(14) CASE(15)
CASE(16) CASE(17) CASE(18) CASE(19) CASE(20) CASE(21) CASE(22) CASE(23)
CASE(24) CASE(25) CASE(26) CASE(27) CASE(28) CASE(29) CASE(30) CASE(31)
CASE(32) CASE(33) CASE(255)
#define FUNCTIONS(R) R##0,R##1,R##2,R##3,R##4,R##5,R##6,R##7,R##8,R##9,R##10,R##11,R##12,R##13,R##14,R##15, \
 R##16,R##17,R##18,R##19,R##20,R##21,R##22,R##23,R##24,R##25,R##26,R##27,R##28,R##29,R##30,R##31,R##32,R##33,R##255
static Shift shifts[2][35]={{FUNCTIONS(dx)},{FUNCTIONS(r8w)}};
static const unsigned counts[]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,255};
static uint64_t checks;
static int check(Shift shift,unsigned count,uint64_t input,uint64_t before,int helper)
{
    unsigned n=count&31,v=input&0xffff,result=n?(n>=16?0:v>>n):v;
    uint64_t expected=(input&~0xffffULL)|result,flags=before&0x8d5,mask=0x8d5;
    if(n){
        unsigned parity=0;for(unsigned i=0;i<8;++i)parity^=(result>>i)&1;
        unsigned cf=n<=16?(v>>(n-1))&1:0;
        flags=cf|(!parity?4:0)|(!result?0x40:0)|((v&0x8000)?0x800:0);
        if(!helper){mask&=~(0x10|(n!=1?0x800:0));if(n>=16)mask&=~1ULL;}
    }
    Result r=shift(input,before);++checks;
    if(r.value!=expected || (r.flags&mask)!=(flags&mask)){
        printf("FAIL shr16 count=%u input=%llx value=%llx flags=%llx expected=%llx/%llx mask=%llx\n",count,input,r.value,r.flags,expected,flags,mask);return 1;
    }
    return 0;
}
int flat_main(unsigned long helper,unsigned long unused)
{
    (void)unused;const unsigned values[]={0,1,0x7fff,0x8000,0xffff,0x4001,0xaaaa,0x5555};
    const uint64_t upper[]={0x123456789abc0000ULL,0xfedcba9876540000ULL};
    const unsigned bits[]={1,4,0x10,0x40,0x80,0x800};
    for(unsigned reg=0;reg<2;++reg)for(unsigned c=0;c<35;++c)for(unsigned v=0;v<8;++v)
        for(unsigned u=0;u<2;++u)for(unsigned f=0;f<64;++f){
            uint64_t before=0x202;for(unsigned i=0;i<6;++i)if(f&(1u<<i))before|=bits[i];
            if(check(shifts[reg][c],counts[c],upper[u]|values[v],before,helper!=0))return 1;
        }
    for(unsigned reg=0;reg<2;++reg)for(unsigned v=0;v<65536;++v)
        if(check(shifts[reg][9],9,upper[reg]|v,0xad7,helper!=0))return 1;
    printf("PASS shr16 %llu checks\n",checks);return 0;
}
