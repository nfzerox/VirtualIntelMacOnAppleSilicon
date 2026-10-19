/* Isolated ARM branch/code-footprint experiment; not a VM benchmark.
 * Same randomized block cycle and work for each layout. All generated code RX.
 */
#include <sys/mman.h>
#include <libkern/OSCacheControl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
static double now(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
    return t.tv_sec+t.tv_nsec*1e-9;
}
static void run(unsigned N, unsigned stride, int indirect) {
    unsigned order[N]; uint32_t seed=1;
    for(unsigned i=0;i<N;i++) order[i]=i;
    for(unsigned i=N-1;i;i--) {
        seed=1664525*seed+1013904223;
        unsigned j=seed%(i+1), v=order[i]; order[i]=order[j]; order[j]=v;
    }
    size_t size=(N*stride+getpagesize())&~(getpagesize()-1);
    uint8_t *base=mmap(NULL,size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    if(base==MAP_FAILED) exit(1);
    *(uint32_t *)(base+N*stride)=0xd65f03c0;
    for(unsigned i=0;i<N;i++) {
        unsigned at=order[i]*stride, next=order[(i+1)%N]*stride;
        uint32_t *p=(void *)(base+at);
        p[0]=0xd503249f; p[1]=0x91000400; p[2]=0xf1000421;
        p[3]=0x54000000|(((N*stride-at-12)/4)<<5); /* b.eq final ret */
        p[4]=indirect ? 0x58000042 : 0x14000000|(((next-at-16)/4)&0x3ffffff);
        p[5]=0xd61f0040; /* br x2 */
        uintptr_t target=(uintptr_t)(base+next); memcpy(p+6,&target,8);
    }
    if(mprotect(base,size,PROT_READ|PROT_EXEC)) exit(2);
    sys_icache_invalidate(base,size);
    uint64_t (*fn)(uint64_t,uint64_t)=(void *)(base+order[0]*stride);
    if(fn(0,100000)!=100000) exit(3);
    for(unsigned trial=0;trial<3;trial++) {
        double t=now(); uint64_t value=fn(0,10000000); t=now()-t;
        if(value!=10000000) exit(4);
        printf("blocks=%u stride=%u branch=%s trial=%u ns/block=%.3f\n",N,stride,
               indirect?"indirect":"direct",trial,t*1e9/10000000);
    }
    munmap(base,size);
}
int main(void) {
    for(unsigned blocks=256;blocks<=4096;blocks*=16)
    for(unsigned repeat=0;repeat<2;repeat++)
        for(unsigned mode=0;mode<2;mode++) {
            run(blocks,repeat?32:128,mode); run(blocks,repeat?128:32,mode);
        }
}
