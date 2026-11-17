// Exact x87 load state, compared independently with Rosetta's x86 execution.
// No host floating-point conversion is used to construct the source bits.
#include "flatlibc.h"

typedef struct {
    uint16_t cw, sw;
    uint8_t tags, reserved;
    uint16_t op;
    uint64_t ip, dp;
    uint32_t mxcsr, mask;
    uint8_t regs[8][16], rest[352];
} __attribute__((aligned(16))) Image;
static uint64_t hash=0xcbf29ce484222325ULL, cases;
static int environment;
static void mix(uint64_t v) { hash=(hash^v)*0x100000001b3ULL; }

static int check(uint64_t bits, int wide, unsigned top, unsigned tags, uint16_t cw) {
    Image before={0}, after={0};
    before.cw=cw; before.sw=(uint16_t)((top<<11)|0x4725);
    before.tags=(uint8_t)tags; before.mxcsr=0x1f80;
    for(unsigned i=0;i<8;i++) {
        uint64_t sig=0x8000000000000000ULL; uint16_t exp=0x3fff;
        memcpy(before.regs[i],&sig,8);memcpy(before.regs[i]+8,&exp,2);
    }
    uint64_t where, flags;
    if(wide) __asm__ volatile("fxrstor64 %[before]\n pushq $0xad7\n popfq\n"
        "leaq 1f(%%rip), %[where]\n 1: fldl (%%rax)\n pushfq\n popq %[flags]\n fxsave64 %[after]\n fninit"
        : [after] "=m"(after), [where] "=&r"(where), [flags] "=&r"(flags)
        : [before] "m"(before), "a"(&bits) : "memory", "cc");
    else __asm__ volatile("fxrstor64 %[before]\n pushq $0xad7\n popfq\n"
        "leaq 1f(%%rip), %[where]\n 1: flds (%%rax)\n pushfq\n popq %[flags]\n fxsave64 %[after]\n fninit"
        : [after] "=m"(after), [where] "=&r"(where), [flags] "=&r"(flags)
        : [before] "m"(before), "a"(&bits) : "memory", "cc");
    uint64_t sig;uint16_t exp;
    memcpy(&sig,after.regs[0],8);memcpy(&exp,after.regs[0]+8,2);
    unsigned eb=wide?11:8, fb=wide?52:23;
    unsigned e=(unsigned)(bits>>fb)&((1u<<eb)-1);
    unsigned next=(top-1)&7;
    if(!(tags>>next&1) && e && e!=((1u<<eb)-1)) {
        uint64_t expected=(bits<<(63-fb))|0x8000000000000000ULL;
        unsigned se=(e+16383-((1u<<(eb-1))-1))|((bits>>(fb+eb)&1)<<15);
        if(sig!=expected || exp!=se || after.cw!=cw ||
           after.sw!=((next<<11)|0x4525) || after.tags!=(tags|(1u<<next)) ||
           (flags&0x8d5)!=0x8d5) {
            printf("FAIL x87 load wide=%d bits=%llx top=%u got=%llx/%x sw=%x tags=%x\n",
                wide,bits,top,sig,exp,after.sw,after.tags);return 1;
        }
    }
    if(environment && (after.ip!=where || after.dp!=(uint64_t)&bits ||
        after.op!=(wide?0x500:0x100))) {
        printf("FAIL x87 environment ip=%llx/%llx dp=%llx/%llx op=%x\n",
            after.ip,where,after.dp,(uint64_t)&bits,after.op);return 1;
    }
    mix(sig);mix(exp);mix(after.cw);mix(after.sw);mix(after.tags);++cases;
    return 0;
}

int flat_main(unsigned long engine_environment,unsigned long unused1) {
    environment=engine_environment!=0;(void)unused1;
    const uint16_t controls[]={0x37f,0x77f,0xb7f,0xf7f,0x27f,0x7f};
    const uint64_t fractions[]={0,1,0x5555555555555ULL,0xaaaaaaaaaaaaaULL,0xfffffffffffffULL};
    for(int wide=0;wide<2;wide++) {
        unsigned fb=wide?52:23,eb=wide?11:8;
        uint64_t mask=(1ULL<<fb)-1;
        for(unsigned e=1;e<((1u<<eb)-1);e++)for(unsigned f=0;f<5;f++)for(unsigned sign=0;sign<2;sign++) {
            uint64_t bits=((uint64_t)sign<<(fb+eb))|((uint64_t)e<<fb)|(fractions[f]&mask);
            if(check(bits,wide,(e+f)&7,0,controls[(e+f)%6]))return 1;
        }
        // Both signs of zero/subnormal/infinity/qNaN/sNaN and full-stack cases
        // remain on the original monitor path; compare their actual x86 state.
        uint64_t special[]={0,1,mask,((1ULL<<eb)-1)<<fb,
            (((1ULL<<eb)-1)<<fb)|1,(((1ULL<<eb)-1)<<fb)|(1ULL<<(fb-1))};
        for(unsigned top=0;top<8;top++)for(unsigned full=0;full<2;full++)
            for(unsigned s=0;s<6;s++)for(unsigned sign=0;sign<2;sign++)
                if(check(special[s]|((uint64_t)sign<<(fb+eb)),wide,top,full?255:0,controls[top%6]))return 1;
        for(unsigned top=0;top<8;top++)
            for(unsigned tags=0;tags<256;tags++)
                if(check((uint64_t)((1u<<(eb-1))-1)<<fb,wide,top,tags,0x37f))return 1;
    }
    printf("PASS x87 loads %llu cases hash=%016llx\n",cases,hash);return 0;
}
