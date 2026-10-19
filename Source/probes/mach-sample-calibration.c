/* Native control for PC sampling: one BTI, sixteen adds, counter/branch.
 * Runs at most ~30 seconds; generated memory is RX, never RWX.
 */
#include <sys/mman.h>
#include <libkern/OSCacheControl.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
int main(void)
{
    size_t size=getpagesize();
    uint32_t *code=mmap(NULL,size,PROT_READ|PROT_WRITE,MAP_ANON|MAP_PRIVATE,-1,0);
    if(code==MAP_FAILED) return 1;
    code[0]=0xd503249f; /* bti j */
    for(int i=1;i<=16;i++) code[i]=0x91000400; /* add x0,x0,#1 */
    code[17]=0xf1000421; /* subs x1,x1,#1 */
    code[18]=0x54000001 | ((uint32_t)(-18 & 0x7ffff)<<5); /* b.ne loop */
    code[19]=0xd65f03c0;
    if(mprotect(code,size,PROT_READ|PROT_EXEC)) return 2;
    sys_icache_invalidate(code,80);
    printf("pid=%d start=0x%llx end=0x%llx\n",getpid(),
           (unsigned long long)(uintptr_t)code,
           (unsigned long long)((uintptr_t)code+size));
    fflush(stdout);
    uint64_t (*loop)(uint64_t,uint64_t)=(void *)code;
    time_t end=time(NULL)+30;
    unsigned rounds=0;
    while(time(NULL)<end) {
        if(loop(0,10000000)!=160000000) return 3;
        rounds++;
    }
    printf("PASS rounds=%u\n",rounds);
    munmap(code,size);
}
