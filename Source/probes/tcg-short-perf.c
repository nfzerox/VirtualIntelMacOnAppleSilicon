// Short deterministic CPU/MMU probes; no results upload, no user-file writes.
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <CommonCrypto/CommonDigest.h>
#include <CommonCrypto/CommonCryptor.h>
#include <emmintrin.h>
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec+t.tv_nsec*1e-9; }
#define FN(i) static __attribute__((noinline)) uint64_t f##i(uint64_t x, uint64_t y) { return (x ^ i) * 6364136223846793005ULL + y; }
FN(0) FN(1) FN(2) FN(3) FN(4) FN(5) FN(6) FN(7)
FN(8) FN(9) FN(10) FN(11) FN(12) FN(13) FN(14) FN(15)
static uint64_t (*volatile dispatch[])(uint64_t, uint64_t) = {
    f0,f1,f2,f3,f4,f5,f6,f7,f8,f9,f10,f11,f12,f13,f14,f15
};
int main(int argc,char **argv) {
    unsigned n=argc>1?(unsigned)strtoul(argv[1],0,10):1000000;
    double t,x=1.001; uint64_t v=1; volatile double sink;
    setbuf(stdout,NULL);
    t=now();for(unsigned i=0;i<n;i++){x=x*1.0000001+0.000001;}sink=x;
    printf("scalar-f64 n=%u sec=%.6f check=%.12g\n",n,now()-t,sink);
    __m128d a=_mm_set_pd(1.002,1.001),m=_mm_set1_pd(1.0000001),b=_mm_set1_pd(.000001);
    t=now();for(unsigned i=0;i<n;i++){a=_mm_add_pd(_mm_mul_pd(a,m),b);} double out[2];_mm_storeu_pd(out,a);
    printf("packed-f64 n=%u sec=%.6f check=%.12g,%.12g\n",n,now()-t,out[0],out[1]);
    unsigned saved=_mm_getcsr();x=1.0;double one=1.0;
    t=now();_mm_setcsr(0x1f80);for(unsigned i=0;i<n;i++){__asm__ volatile("mulsd %1,%0" : "+x"(x) : "x"(one));}
    unsigned flags=_mm_getcsr();double elapsed=now()-t;_mm_setcsr(saved);
    printf("exact-f64 n=%u sec=%.6f check=%.12g mxcsr=%x\n",n,elapsed,x,flags);
    t=now();for(unsigned i=0;i<n;i++){v^=v<<13;v^=v>>7;v^=v<<17;}
    printf("integer n=%u sec=%.6f check=%llu\n",n,now()-t,(unsigned long long)v);
    v=1;t=now();for(unsigned i=0;i<n;i++)v=dispatch[(v>>24)&15](v,i);
    printf("indirect-calls n=%u sec=%.6f check=%llu\n",n,now()-t,(unsigned long long)v);
    int f[2];char c=42;if(pipe(f))return 2;
    t=now();for(unsigned i=0;i<10000;i++){if(write(f[1],&c,1)!=1||read(f[0],&c,1)!=1)return 3;}
    printf("pipe-roundtrip n=10000 sec=%.6f check=%d\n",now()-t,c);close(f[0]);close(f[1]);
    size_t size=1024*1024;unsigned char *data=calloc(1,size),*result=malloc(size+16),digest[32],key[16]={0};
    if(!data||!result)return 4;
    t=now();for(int i=0;i<8;i++)CC_SHA256(data,(CC_LONG)size,digest);
    printf("sha256 bytes=%zu sec=%.6f check=%02x%02x\n",size*8,now()-t,digest[0],digest[1]);
    t=now();size_t written=0;for(int i=0;i<8;i++)if(CCCrypt(kCCEncrypt,kCCAlgorithmAES,kCCOptionECBMode,key,16,NULL,data,size,result,size+16,&written))return 5;
    printf("aes128 bytes=%zu sec=%.6f check=%02x%02x\n",size*8,now()-t,result[0],result[1]);free(data);free(result);
    return 0;
}
