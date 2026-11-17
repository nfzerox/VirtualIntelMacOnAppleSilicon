// Finite guest-side code-invalidation check. No system files or services.
// A writable alias changes code executed by another thread through an RX alias.
#include <pthread.h>
#include <cpuid.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    int pending,stop,expected,failed,generation;
    int (*execute)(void);
} Check;

static sigjmp_buf fetch_fault;
static volatile sig_atomic_t fetch_faulted;
static void FetchFault(int signal) { fetch_faulted=signal;siglongjmp(fetch_fault,1); }

static void *Execute(void *opaque) {
    Check *check=opaque;
    int seen=0;
    pthread_mutex_lock(&check->mutex);
    for(;;) {
        while(check->generation==seen&&!check->stop)pthread_cond_wait(&check->changed,&check->mutex);
        if(check->stop)break;
        int expected=check->expected;seen=check->generation;
        pthread_mutex_unlock(&check->mutex);
        int observed=check->execute();
        pthread_mutex_lock(&check->mutex);
        if(observed!=expected) {
            fprintf(stderr,"Stale executable alias: expected %d, got %d\n",expected,observed);
            check->failed=1;
        }
        check->pending--;pthread_cond_broadcast(&check->changed);
    }
    pthread_mutex_unlock(&check->mutex);return NULL;
}

static int AtomicCase(uint8_t *write,void *code,long page) {
    uint8_t *memory=mmap(NULL,2*page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    if(memory==MAP_FAILED)return 0;
    uint8_t *crossing=memory+page-3;
    // MOV eax,1; LOCK XADD [rdi+disp8],eax; RET. Both displacements
    // cross a guest page and force TCG's serial atomic path. Change only
    // that cached instruction, not its address or executable mapping.
    const uint8_t bytes[]={0xb8,1,0,0,0,0xf0,0x0f,0xc1,0x47,1,0xc3};
    memcpy(write,bytes,sizeof(bytes));
    int (*run)(void *)=(int (*)(void *))(uintptr_t)code;int ok=1;
    for(unsigned i=0;i<100&&ok;i++) {
        unsigned displacement=1+(i&1);write[9]=(uint8_t)displacement;
        memset(crossing,0,6);uint8_t expected[6]={0};expected[displacement]=1;
        if(run(crossing)||memcmp(crossing,expected,sizeof(expected)))ok=0;
    }
    munmap(memory,2*page);return ok;
}

int main(int argc,char **argv) {
    int vex=argc==2&&!strcmp(argv[1],"--vex");
    if(argc!=1&&!vex)return 64;
    if(vex) {
        unsigned a,b,c,d;__cpuid_count(1,0,a,b,c,d);
        if((c&((1u<<27)|(1u<<28)))!=((1u<<27)|(1u<<28)))return 77;
        unsigned low,high;__asm__ volatile("xgetbv":"=a"(low),"=d"(high):"c"(0));
        if((low&6)!=6)return 77;
        __cpuid_count(7,0,a,b,c,d);if(!(b&(1u<<5)))return 77;
    }
    char name[]="/tmp/vim-code-alias.XXXXXX";
    int fd=mkstemp(name);if(fd<0){perror("mkstemp");return 1;}
    unlink(name);
    long page=sysconf(_SC_PAGESIZE);
    if(page<=0||ftruncate(fd,2*page)){perror("ftruncate");close(fd);return 1;}
    uint8_t *writable=mmap(NULL,2*page,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    uint8_t *executable=mmap(NULL,2*page,PROT_READ|PROT_EXEC,MAP_SHARED,fd,0);close(fd);
    if(writable==MAP_FAILED||executable==MAP_FAILED){perror("mmap");return 1;}
    uint8_t *write=writable+(vex?page-2:0),*code=executable+(vex?page-2:0);
    // x86 mov imm32,%eax; ret. The mutex orders each complete edit before
    // execution; the test never races an instruction against a partial edit.
    if(vex) {
        // VPXOR/VPCMPEQD ymm0,ymm0,ymm0; VMOVD xmm0,eax;
        // VZEROUPPER; RET. Only the second code page's opcode changes.
        const uint8_t bytes[]={0xc5,0xfd,0xef,0xc0,0xc5,0xf9,0x7e,0xc0,0xc5,0xf8,0x77,0xc3};
        memcpy(write,bytes,sizeof(bytes));
    } else {write[0]=0xb8;write[5]=0xc3;}
    Check check={.mutex=PTHREAD_MUTEX_INITIALIZER,.changed=PTHREAD_COND_INITIALIZER,
        .execute=(int (*)(void))(uintptr_t)code};
    pthread_t threads[2];int readers=vex?2:1;
    for(int i=0;i<readers;i++)if(pthread_create(&threads[i],NULL,Execute,&check)){perror("pthread_create");return 1;}
    pthread_mutex_lock(&check.mutex);
    for(int i=1;i<=1000&&!check.failed;i++) {
        if(vex) {write[2]=(i&1)?0x76:0xef;check.expected=(i&1)?-1:0;}
        else {memcpy(write+1,&i,sizeof(i));check.expected=i;}
        check.pending=readers;check.generation++;
        pthread_cond_broadcast(&check.changed);
        while(check.pending)pthread_cond_wait(&check.changed,&check.mutex);
    }
    check.stop=1;pthread_cond_broadcast(&check.changed);pthread_mutex_unlock(&check.mutex);
    for(int i=0;i<readers;i++)pthread_join(threads[i],NULL);
    if(vex&&!check.failed) {
        struct sigaction action={.sa_handler=FetchFault},old_segv,old_bus;
        sigemptyset(&action.sa_mask);sigaction(SIGSEGV,&action,&old_segv);sigaction(SIGBUS,&action,&old_bus);
        if(mprotect(executable+page,page,PROT_NONE))check.failed=1;
        else if(!sigsetjmp(fetch_fault,1)) {
            check.execute();check.failed=1;
        }
        if(mprotect(executable+page,page,PROT_READ|PROT_EXEC))check.failed=1;
        sigaction(SIGSEGV,&old_segv,NULL);sigaction(SIGBUS,&old_bus,NULL);
        if(!fetch_faulted||check.execute()!=check.expected)check.failed=1;
    }
    if(!check.failed) {
        pid_t child=fork();int status=0;
        if(child==0)_exit(check.execute()==check.expected?0:1);
        if(child<0||waitpid(child,&status,0)!=child||!WIFEXITED(status)||WEXITSTATUS(status))check.failed=1;
    }
    if(vex&&!check.failed&&!AtomicCase(write,code,page)) {
        fputs("FAIL rewritten cross-page atomic instruction\n",stderr);check.failed=1;
    }
    munmap(writable,2*page);munmap(executable,2*page);
    if(check.failed)return 1;
    puts(vex?"PASS 1000 two-reader VEX alias edits, executable-page fault, inherited mapping and 100 rewritten cross-page atomics":
        "PASS 1000 cross-thread writable/executable alias edits and inherited executable mapping");return 0;
}
