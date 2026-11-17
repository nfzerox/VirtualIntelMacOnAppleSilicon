/* Finite cross-modifying-code oracle, independent of graphics and files.
 * The writer does not patch again until every reader has serialized, checked
 * the data bytes, executed the new instruction and acknowledged that round.
 * Compare the same executable on physical Intel before diagnosing an emulator.
 */
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#define READERS 3
#define ROUNDS 2000
static uint8_t *code;
static uint64_t generation, acknowledged[READERS];
static int failed;

static void *reader(void *argument)
{
    unsigned me=(unsigned)(uintptr_t)argument;
    uint64_t last=0;
    uint32_t (*execute)(void)=(void *)(code+3);
    for(;;) {
        uint64_t round=__atomic_load_n(&generation,__ATOMIC_SEQ_CST);
        if(round==UINT64_MAX)break;
        if(!round||round==last){sched_yield();continue;}
        uint32_t bytes=__atomic_load_n((uint32_t *)(code+4),__ATOMIC_SEQ_CST);
        uint32_t a=0,b,c=0,d;
        __asm__ volatile("cpuid" : "+a"(a),"=b"(b),"+c"(c),"=d"(d) : : "memory");
        uint32_t result=execute();
        if(bytes!=round||result!=round) {
            if(!__atomic_exchange_n(&failed,1,__ATOMIC_SEQ_CST))
                fprintf(stderr,"reader=%u generation=%llu data=%u executed=%u\n",
                    me,(unsigned long long)round,bytes,result);
        }
        last=round;
        __atomic_store_n(&acknowledged[me],round,__ATOMIC_SEQ_CST);
    }
    return NULL;
}

int main(void)
{
    /* SIGALRM is a finite test guard, not evidence of stale instructions. */
    alarm(30);
    code=mmap(NULL,4096,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANON,-1,0);
    if(code==MAP_FAILED){perror("mmap");return 2;}
    static const uint8_t initial[]={0x90,0x90,0x90,0xb8,0,0,0,0,0xc3};
    for(unsigned i=0;i<sizeof(initial);i++)code[i]=initial[i];
    pthread_t threads[READERS];unsigned count=0;
    for(;count<READERS;count++) {
        int error=pthread_create(&threads[count],NULL,reader,(void *)(uintptr_t)count);
        if(error){fprintf(stderr,"pthread_create: %d\n",error);failed=1;break;}
    }
    unsigned completed=0;
    if(count==READERS)for(uint32_t round=1;round<=ROUNDS;round++) {
        __atomic_store_n((uint32_t *)(code+4),round,__ATOMIC_SEQ_CST);
        __atomic_store_n(&generation,round,__ATOMIC_SEQ_CST);
        for(unsigned i=0;i<READERS;i++)
            while(__atomic_load_n(&acknowledged[i],__ATOMIC_SEQ_CST)!=round)sched_yield();
        completed=round;
        if(__atomic_load_n(&failed,__ATOMIC_SEQ_CST))break;
    }
    __atomic_store_n(&generation,UINT64_MAX,__ATOMIC_SEQ_CST);
    for(unsigned i=0;i<count;i++)pthread_join(threads[i],NULL);
    munmap(code,4096);alarm(0);
    printf("%s serialized cross-modifying code: %u rounds, %u readers\n",
        failed?"FAIL":"PASS",completed,count);
    return failed?1:0;
}
