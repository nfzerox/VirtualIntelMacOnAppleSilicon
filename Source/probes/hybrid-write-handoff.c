// Finite real-guest comparison of sparse versus broad VEX memory writes.
#include <immintrin.h>
#include <cpuid.h>
#include <mach/mach_time.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

__attribute__((target("avx2"),noinline))
static void writes(uint32_t *memory, unsigned vectors)
{
    __m256i one = _mm256_set1_epi32(1);
    for (unsigned round = 0; round < 16; round++) {
        for (unsigned i = 0; i < vectors; i++) {
            __m256i value = _mm256_loadu_si256((const __m256i *)(memory + i * 8));
            _mm256_storeu_si256((__m256i *)(memory + i * 8), _mm256_add_epi32(value, one));
        }
    }
}

int main(void)
{
    unsigned a,b,c,d; __cpuid_count(1,0,a,b,c,d);
    if ((c & ((1u<<27)|(1u<<28))) != ((1u<<27)|(1u<<28))) return 77;
    unsigned low,high; __asm__ volatile("xgetbv":"=a"(low),"=d"(high):"c"(0));
    __cpuid_count(7,0,a,b,c,d); if ((low&6)!=6 || !(b&(1u<<5))) return 77;
    mach_timebase_info_data_t basis; mach_timebase_info(&basis);
    const unsigned sizes[] = {128,32768};
    for (unsigned shape=0; shape<2; shape++) {
        unsigned vectors=sizes[shape]; size_t bytes=(size_t)vectors*32;
        uint32_t *memory=malloc(bytes); if(!memory)return 1;
        for (unsigned sample=0; sample<5; sample++) {
            memset(memory,0,bytes);
            uint64_t start=mach_absolute_time(); writes(memory,vectors);
            uint64_t ticks=mach_absolute_time()-start;
            for (unsigned i=0;i<vectors*8;i++)if(memory[i]!=16){free(memory);return 2;}
            printf("PASS bytes=%zu sample=%u milliseconds=%.4f value=16\n",bytes,sample,
                (double)ticks*basis.numer/basis.denom/1e6);
            fflush(stdout);
        }
        free(memory);
    }
    return 0;
}
