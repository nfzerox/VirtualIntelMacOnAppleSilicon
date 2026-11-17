// Finite guest/native comparison for bounded VEX fallback. No system changes.
#include <immintrin.h>
#include <cpuid.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <mach/mach_time.h>

#define AVX __attribute__((target("avx2,bmi2"),noinline))
static sigjmp_buf fault;
static volatile sig_atomic_t faulted;
static void on_fault(int signal) { faulted=signal;siglongjmp(fault,1); }

static AVX void vector_loop(const uint32_t *a,const uint32_t *b,uint32_t *out) {
    __m256i x=_mm256_loadu_si256((const __m256i *)a);
    __m256i y=_mm256_loadu_si256((const __m256i *)b);
    for(unsigned round=0;round<1024;round++) {
        x=_mm256_add_epi32(_mm256_xor_si256(x,_mm256_srli_epi32(y,7)),_mm256_slli_epi32(x,3));
        y=_mm256_add_epi32(_mm256_xor_si256(y,x),_mm256_set1_epi32(round));
    }
    _mm256_storeu_si256((__m256i *)out,x);
    _mm256_storeu_si256((__m256i *)(out+8),y);
    _mm256_storeu_si256((__m256i *)(out+16),_mm256_permute2x128_si256(x,y,0x21));
    _mm256_storeu_si256((__m256i *)(out+24),_mm256_alignr_epi8(y,x,7));
}

static AVX int rotate_flags(uint32_t input) {
    uint64_t before,after;uint32_t result;
    __asm__ volatile("stc; pushfq; popq %0; rorxl $13,%k3,%k2; pushfq; popq %1"
        : "=&r"(before),"=&r"(after),"=&r"(result) : "r"(input) : "cc");
    return before==after&&result==((input>>13)|(input<<19));
}

static AVX void crossing_load(const void *source,void *destination,volatile int *committed) {
    __m256i value=_mm256_loadu_si256(source);
    _mm256_storeu_si256(destination,value);
    *committed=1;
}

static AVX void mixed_sse(const void *source,void *destination) {
    // The jumps end TCG blocks so legacy SSE runs on the engine between
    // VEX fallbacks. It must preserve the upper half of the YMM register.
    __asm__ volatile("vmovdqu (%0),%%ymm0; jmp 1f; 1: pxor %%xmm0,%%xmm0; jmp 2f; 2: vmovdqu %%ymm0,(%1); vzeroupper"
        ::"r"(source),"r"(destination):"xmm0","memory");
}

static AVX void unrolled(const void *a,const void *b,const void *c,void *out) {
    // SHA-like instruction density, without depending on an OS crypto API.
    // The loop deliberately exceeds a small fallback block's instruction
    // budget; no branch lets an optimizer fold these into a scalar formula.
#define STEP "vpsrld $7,%%ymm0,%%ymm3; vpxor %%ymm1,%%ymm3,%%ymm0; vpaddd %%ymm2,%%ymm0,%%ymm0;"
#define EIGHT STEP STEP STEP STEP STEP STEP STEP STEP
#define SIXTYFOUR EIGHT EIGHT EIGHT EIGHT EIGHT EIGHT EIGHT EIGHT
    __asm__ volatile("vmovdqu (%0),%%ymm0; vmovdqu (%1),%%ymm1; vmovdqu (%2),%%ymm2;"
        SIXTYFOUR SIXTYFOUR
        "vmovdqu %%ymm0,(%3); vzeroupper"
        ::"r"(a),"r"(b),"r"(c),"r"(out):"xmm0","xmm1","xmm2","xmm3","memory");
#undef SIXTYFOUR
#undef EIGHT
#undef STEP
}

static int crossing_code(long page,const uint32_t *input) {
    uint8_t *mapping=mmap(NULL,2*page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    if(mapping==MAP_FAILED)return 0;
    uint8_t *code=mapping+page-24;
    const uint8_t load[]={0xc5,0xfe,0x6f,0x07},add[]={0xc5,0xfd,0xfe,0xc0};
    const uint8_t finish[]={0xc5,0xfe,0x7f,0x06,0xc5,0xf8,0x77,0xc3};
    memcpy(code,load,4);for(unsigned i=0;i<8;i++)memcpy(code+4+i*4,add,4);memcpy(code+36,finish,sizeof(finish));
    uint32_t output[8];int ok=0;
    if(mprotect(mapping,2*page,PROT_READ|PROT_EXEC))goto done;
    void (*run)(const void *,void *)=(void (*)(const void *,void *))(uintptr_t)code;
    run(input,output);
    for(unsigned i=0;i<8;i++)if(output[i]!=input[i]*256u)goto done;
    if(mprotect(mapping,2*page,PROT_READ|PROT_WRITE))goto done;
    // Only the second code page changes: replace its first VPADDD with
    // VPXOR ymm0,ymm0,ymm0. A stale multi-instruction block gives nonzero.
    code[26]=0xef;
    if(mprotect(mapping,2*page,PROT_READ|PROT_EXEC))goto done;
    run(input,output);
    for(unsigned i=0;i<8;i++)if(output[i])goto done;
    ok=1;
done:
    munmap(mapping,2*page);return ok;
}

int main(void) {
    unsigned a,b,c,d;__cpuid_count(1,0,a,b,c,d);
    if((c&((1u<<27)|(1u<<28)))!=((1u<<27)|(1u<<28)))return 77;
    unsigned low,high;__asm__ volatile("xgetbv":"=a"(low),"=d"(high):"c"(0));
    if((low&6)!=6)return 77;
    __cpuid_count(7,0,a,b,c,d);if((b&((1u<<5)|(1u<<8)))!=((1u<<5)|(1u<<8)))return 77;
    uint32_t x[8],y[8],input_x[8],input_y[8],observed[32],expected[32];
    for(unsigned i=0;i<8;i++){x[i]=input_x[i]=0x12345678u+i*0x31415927u;y[i]=input_y[i]=~x[i];}
    for(unsigned round=0;round<1024;round++)for(unsigned i=0;i<8;i++) {
        x[i]=(x[i]^(y[i]>>7))+(x[i]<<3);y[i]=(y[i]^x[i])+round;
    }
    memcpy(expected,x,32);memcpy(expected+8,y,32);
    memcpy(expected+16,x+4,16);memcpy(expected+20,y,16);
    for(unsigned lane=0;lane<2;lane++) {
        uint8_t joined[32];memcpy(joined,(uint8_t *)x+16*lane,16);memcpy(joined+16,(uint8_t *)y+16*lane,16);
        memcpy((uint8_t *)(expected+24)+16*lane,joined+7,16);
    }
    vector_loop(input_x,input_y,observed);
    if(memcmp(observed,expected,sizeof(expected))||!rotate_flags(0xa1345678)) {
        fputs("FAIL vector lanes, permutation, alignment or RORX flags\n",stderr);return 1;
    }
    uint32_t mixed[8];mixed_sse(input_x,mixed);
    for(unsigned i=0;i<8;i++)if(mixed[i]!=(i<4?0:input_x[i])) {
        fputs("FAIL legacy SSE did not preserve YMM upper lanes across hand-offs\n",stderr);return 1;
    }
    long page=sysconf(_SC_PAGESIZE);
    if(page<=0)return 1;
    if(!crossing_code(page,input_x)){fputs("FAIL second-page VEX code edit\n",stderr);return 1;}
    uint8_t *mapping=mmap(NULL,2*page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    if(mapping==MAP_FAILED||mprotect(mapping+page,page,PROT_NONE)){perror("mmap/mprotect");return 1;}
    struct sigaction action={.sa_handler=on_fault},old_segv,old_bus;
    sigemptyset(&action.sa_mask);sigaction(SIGSEGV,&action,&old_segv);sigaction(SIGBUS,&action,&old_bus);
    volatile int committed=0;volatile uint8_t output[32];memset((void *)output,0xa5,sizeof(output));
    if(!sigsetjmp(fault,1))crossing_load(mapping+page-16,(void *)output,&committed);
    sigaction(SIGSEGV,&old_segv,NULL);sigaction(SIGBUS,&old_bus,NULL);munmap(mapping,2*page);
    if(!faulted||committed){fputs("FAIL crossing load did not stop at its fault\n",stderr);return 1;}
    for(unsigned i=0;i<sizeof(output);i++)if(output[i]!=0xa5){fputs("FAIL faulting load committed its destination\n",stderr);return 1;}
    uint32_t dense[8],reference[8];memcpy(reference,input_x,sizeof(reference));
    for(unsigned round=0;round<128;round++)for(unsigned i=0;i<8;i++)
        reference[i]=((reference[i]>>7)^input_y[i])+input_x[i];
    uint64_t start=mach_absolute_time();
    for(unsigned round=0;round<100;round++)unrolled(input_x,input_y,input_x,dense);
    uint64_t elapsed=mach_absolute_time()-start;mach_timebase_info_data_t basis;mach_timebase_info(&basis);
    if(memcmp(dense,reference,sizeof(dense))){fputs("FAIL unrolled vector block\n",stderr);return 1;}
    printf("Unrolled vector workload: %.3f ms (100 x 384 arithmetic instructions)\n",
        (double)elapsed*basis.numer/basis.denom/1e6);
    printf("PASS AVX2 lanes/permutations, mixed SSE/YMM state, RORX flags, second-page code edit and cross-page fault; checksum %08x\n",observed[0]^observed[31]);
    return 0;
}
