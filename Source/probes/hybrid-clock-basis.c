// Host clock-origin check / x86 guest cross-thread TSC ordering. No policy changes.
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#if defined(__aarch64__)
#include <mach/mach_time.h>
int main(void) {
    mach_timebase_info_data_t tb;mach_timebase_info(&tb);
    if(tb.numer!=125||tb.denom!=3){fputs("Host counter conversion needs qualification\n",stderr);return 1;}
    for(unsigned i=0;i<100000;i++) {
        uint64_t first=mach_absolute_time()*125/3;
        uint64_t qemu=clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
        uint64_t last=mach_absolute_time()*125/3;
        if(qemu<first||qemu>last){fprintf(stderr,"Host clock origins differ: %"PRIu64" <= %"PRIu64" <= %"PRIu64"\n",first,qemu,last);return 1;}
    }
    unsigned old_rounding_errors=0;
    for(uint64_t i=1;i<100000;i++) {
        uint64_t counter=i*7919,virtual_offset=i*1222,vm_offset=(uint64_t)-123456;
        uint64_t qemu=counter*125/3+vm_offset;
        uint64_t corrected=((counter-virtual_offset)+virtual_offset)*125/3+vm_offset;
        uint64_t old=(counter-virtual_offset)*125/3+virtual_offset*125/3+vm_offset;
        if(qemu!=corrected)return 1;
        old_rounding_errors+=old!=qemu;
    }
    if(!old_rounding_errors)return 1;
    printf("PASS 100000 shared-origin host clock brackets and exact counter-phase conversion; old split rounding differed %u times\n",old_rounding_errors);
    return 0;
}
#elif defined(__x86_64__)
#include <cpuid.h>
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static uint64_t previous,worst_backwards;
static unsigned backwards;
static int use_vex;
__attribute__((target("avx2"),noinline)) static void fallback(void) {
    __asm__ volatile("vpxor %%ymm0,%%ymm0,%%ymm0; vzeroupper":::"xmm0");
}
__attribute__((noinline)) static void x87_fallback(void) {
    // FSIN remains on TCG even on older Penryn guests. Zero is exact and
    // the balanced x87 stack leaves the caller's floating-point state alone.
    __asm__ volatile("fldz; fsin; fstp %%st(0)":::"st");
}
static uint64_t ordered_tsc(void) {
    unsigned lo,hi;
    __asm__ volatile("lfence; rdtsc; lfence":"=a"(lo),"=d"(hi)::"memory");
    return ((uint64_t)hi<<32)|lo;
}
static void *check(void *unused) {
    (void)unused;
    for(unsigned i=0;i<10000;i++) {
        pthread_mutex_lock(&mutex);
        uint64_t now=ordered_tsc();
        if(now<previous){backwards++;if(previous-now>worst_backwards)worst_backwards=previous-now;}
        previous=now;
        pthread_mutex_unlock(&mutex);
        if(use_vex)fallback();else x87_fallback();
    }
    return NULL;
}
int main(void) {
    unsigned a,b,c,d;__cpuid_count(1,0,a,b,c,d);
    if((c&((1u<<27)|(1u<<28)))==((1u<<27)|(1u<<28))) {
        unsigned lo,hi;__asm__ volatile("xgetbv":"=a"(lo),"=d"(hi):"c"(0));
        __cpuid_count(7,0,a,b,c,d);use_vex=(lo&6)==6&&(b&(1u<<5));
    }
    pthread_t threads[4];
    for(unsigned i=0;i<4;i++)if(pthread_create(&threads[i],NULL,check,NULL))return 1;
    for(unsigned i=0;i<4;i++)pthread_join(threads[i],NULL);
    if(backwards){fprintf(stderr,"FAIL %u backwards ordered reads, worst %"PRIu64" ticks (%s hand-offs)\n",backwards,worst_backwards,use_vex?"VEX":"x87");return 1;}
    printf("PASS 40000 ordered cross-thread TSC reads (%s hand-offs)\n",use_vex?"VEX":"x87");return 0;
}
#endif
