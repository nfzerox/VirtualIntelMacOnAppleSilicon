/* Bounded string-instruction benchmark; validates complete buffers. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec+t.tv_nsec*1e-9; }
int main(void) {
    const size_t size=1024*1024, loops=8;
    unsigned char *a=malloc(size), *b=malloc(size);
    if(!a||!b)return 2;
    for(size_t i=0;i<size;i++)a[i]=(unsigned char)(i*13+(i>>8));
    for(int op=0;op<3;op++) {
        double t=now();
        for(size_t j=0;j<loops;j++) {
            void *dst=b; const void *src=a; size_t n=op==0?size:size/8;
            if(op==0) __asm__ volatile("cld; rep movsb" : "+D"(dst), "+S"(src), "+c"(n) : : "memory", "cc");
            if(op==1) __asm__ volatile("cld; rep movsq" : "+D"(dst), "+S"(src), "+c"(n) : : "memory", "cc");
            if(op==2) __asm__ volatile("cld; rep stosq" : "+D"(dst), "+c"(n) : "a"(0x5a5a5a5a5a5a5a5aULL) : "memory", "cc");
        }
        double seconds=now()-t;
        for(size_t i=0;i<size;i++)if(b[i]!=(op==2?0x5a:a[i]))return 1;
        printf("%s bytes=%zu sec=%.6f PASS\n",op==0?"rep-movsb":op==1?"rep-movsq":"rep-stosq",size*loops,seconds);
    }
    free(a);free(b);return 0;
}
