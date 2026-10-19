/* Isolated native-ARM IR inspection of this host's Rosetta 367.8 library.
 * Does not load guest disks or change Apple files. Executes only a matched
 * load/add prefix and a matched bounded block using its branch-slot exit.
 * Private ABI: fail closed on a different source version/format.
 * Rebase format follows dyld's dyld_chained_ptr_64_rebase (format 6).
 */
#include <mach-o/loader.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <libkern/OSCacheControl.h>
#include <os/lock.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct { void *function; const char *name; } Symbol;
typedef struct { Symbol *symbols; uint64_t count; } Symbols;
static void require(int ok, const char *why)
{ if (!ok) { fprintf(stderr, "reject: %s\n", why); exit(1); } }
static void unmask(void)
{ sigset_t s; sigemptyset(&s); sigprocmask(SIG_SETMASK, &s, NULL); }
static void lock(uint32_t *p, unsigned options)
{ (void)options; os_unfair_lock_lock((os_unfair_lock_t)p); }
static void unlock(uint32_t *p, unsigned options)
{ (void)options; os_unfair_lock_unlock((os_unfair_lock_t)p); }
static void *lookup(Symbols *s, const char *name)
{
    for (uint64_t i=0; i<s->count; i++)
        if (!strcmp(s->symbols[i].name,name)) return s->symbols[i].function;
    require(0,name); return NULL;
}
#define NAME(s) "__ZN7rosetta7runtime7library" s
/* This inspected block clobbers x22-x24 and x30. Preserve the host ABI,
 * returning through our exit rather than Rosetta's private link register. */
__attribute__((naked,noinline)) static uint64_t enter_block(uint64_t value, void *code)
{
    __asm__("stp x22, x23, [sp, #-32]!\n"
            "stp x24, x30, [sp, #16]\n"
            "br x1\n");
}
__attribute__((naked,noinline)) static void exit_block(void)
{
    __asm__("ldp x24, x30, [sp, #16]\n"
            "ldp x22, x23, [sp], #32\n"
            "ret\n");
}
int main(void)
{
    const char *path="/Library/Apple/usr/libexec/oah/libRosettaRuntime";
    int fd=open(path,O_RDONLY); struct stat st;
    require(fd>=0 && !fstat(fd,&st),"open Rosetta library");
    size_t size=(size_t)st.st_size;
    uint8_t *file=mmap(NULL,size,PROT_READ,MAP_PRIVATE,fd,0);
    require(file!=MAP_FAILED && size>=sizeof(struct mach_header_64),"file mapping");
    struct mach_header_64 *h=(void *)file;
    require(h->magic==MH_MAGIC_64 && h->cputype==CPU_TYPE_ARM64 &&
            sizeof(*h)+h->sizeofcmds<=size,"Mach-O header");
    uint64_t extent=0,chain=0,exports=0,textsize=0,version=0;
    struct load_command *lc=(void *)(h+1);
    for (unsigned i=0;i<h->ncmds;i++) {
        require((uint8_t *)lc+sizeof(*lc)<=file+size && lc->cmdsize>=8 &&
                (uint8_t *)lc+lc->cmdsize<=file+size,"load command");
        if (lc->cmd==LC_SOURCE_VERSION) version=((struct source_version_command *)lc)->version;
        if (lc->cmd==LC_SEGMENT_64) {
            struct segment_command_64 *s=(void *)lc;
            require(s->fileoff+s->filesize<=size && s->vmsize>=s->filesize &&
                    s->vmaddr+s->vmsize<0x1000000,"segment bounds");
            if (s->vmaddr+s->vmsize>extent) extent=s->vmaddr+s->vmsize;
            if (!strcmp(s->segname,"__TEXT")) { require(s->vmaddr==0,"text base"); textsize=s->vmsize; }
            struct section_64 *sec=(void *)(s+1);
            require(sizeof(*s)+(uint64_t)s->nsects*sizeof(*sec)<=s->cmdsize,"section bounds");
            for(unsigned j=0;j<s->nsects;j++) {
                if (!strcmp(sec[j].sectname,"__chain_starts")) chain=sec[j].addr;
                if (!strcmp(sec[j].sectname,"exports")) exports=sec[j].addr;
            }
        }
        lc=(void *)((uint8_t *)lc+lc->cmdsize);
    }
    require(version==((367ULL<<40)|(8ULL<<30)) && chain && exports && textsize,
            "requires inspected Rosetta 367.8 format");
    size_t pagesize=(size_t)getpagesize();
    extent=(extent+pagesize-1)&~(pagesize-1);
    uint8_t *base=mmap(NULL,extent,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    require(base!=MAP_FAILED,"image allocation");
    lc=(void *)(h+1);
    for(unsigned i=0;i<h->ncmds;i++) {
        if(lc->cmd==LC_SEGMENT_64) {
            struct segment_command_64 *s=(void *)lc;
            memcpy(base+s->vmaddr,file+s->fileoff,s->filesize);
        }
        lc=(void *)((uint8_t *)lc+lc->cmdsize);
    }
    require(chain+8<extent && exports+24<extent,"metadata bounds");
    uint32_t *starts=(void *)(base+chain);
    require(starts[0]==6 && starts[1]<10000 &&
            chain+8+4ULL*starts[1]<=extent,"chain format");
    for(unsigned i=0;i<starts[1];i++) {
        uint64_t off=starts[2+i];
        for(unsigned steps=0;;steps++) {
            require(off+8<=extent && steps<100000,"chain bounds");
            uint64_t raw; memcpy(&raw,base+off,8);
            uint64_t next=(raw>>51)&0xfff, target=raw&0xfffffffffULL;
            require(!(raw>>63) && !((raw>>36)&0x7fff) && target<extent,"local rebase only");
            uint64_t pointer=(uintptr_t)(base+target); memcpy(base+off,&pointer,8);
            if(!next) break;
            off+=next*4;
        }
    }
    require(!mprotect(base,textsize,PROT_READ|PROT_EXEC),"RX text");
    sys_icache_invalidate(base,textsize);
    Symbols *api=(void *)(base+exports+8);
    require(api->count==108,"export count");
    Symbol imports[]={{unmask,"__Z30unmask_signals_for_fatal_errorv"},
                      {lock,"__Z16unfair_lock_lockPjj"},
                      {unlock,"__Z18unfair_lock_unlockPjj"}};
    Symbols in={imports,3}; uint64_t offsets[256]={0};
    void (*init)(Symbols *,uint64_t,void *)=lookup(api,NAME("12init_libraryEPKNS1_10SymbolListEyPKNS_20ThreadContextOffsetsE"));
    void (*reset)(void)=lookup(api,NAME("16translator_resetEv"));
    void *(*create)(uint64_t,unsigned,unsigned,unsigned,unsigned)=lookup(api,NAME("9ir_createEyjj15TranslationMode13ExecutionMode"));
    void (*print)(void *,int)=lookup(api,NAME("12module_printEPKNS1_12ModuleResultEi"));
    void *(*translate)(void *,unsigned)=lookup(api,NAME("20translator_translateEPKNS1_12ModuleResultE15TranslationMode"));
    const uint32_t *(*data)(void *)=lookup(api,NAME("19translator_get_dataEPKNS1_17TranslationResultE"));
    uint64_t (*bytes)(void *)=lookup(api,NAME("19translator_get_sizeEPKNS1_17TranslationResultE"));
    unsigned (*slots_offset)(void *)=lookup(api,NAME("34translator_get_branch_slots_offsetEPKNS1_17TranslationResultE"));
    unsigned (*slots_count)(void *)=lookup(api,NAME("33translator_get_branch_slots_countEPKNS1_17TranslationResultE"));
    init(&in,0x12345678,offsets);
    unsigned char code[][8]={
        {0x48,0x8b,0x07,0x48,0x83,0xc0,1,0xc3},
        {0x0f,0x20,0xd8,0xc3},
        {0xfa,0xc3}, {0xf4,0xc3}, {0x0f,0x05,0xc3}
    };
    const unsigned lengths[]={8,4,2,2,3};
    const char *names[]={"load/add/ret","read CR3","CLI","HLT","SYSCALL"};
    /* Mode zero recognizes REX.W; mode one decodes 0x48 as DEC EAX (32 bit).
     * Zero runtime offsets are NOT execution-ready. */
    for(unsigned i=0;i<5;i++) {
        printf("CASE %s\n",names[i]); fflush(stdout);
        reset();
        void *module=create((uintptr_t)code[i],lengths[i],lengths[i],0,0);
        require(module!=NULL,"IR module");
        print(module,1);
        if(i==0) {
            void *result=translate(module,0);
            require(result!=NULL,"translation result");
            uint64_t n=bytes(result);
            require(n<=4096 && !(n&3),"bounded ARM output");
            printf("ARM output (%llu bytes; not relocated/executed):\n",(unsigned long long)n);
            for(uint64_t j=0;j<n/4;j++) printf("%04llx: %08x\n",(unsigned long long)(4*j),data(result)[j]);
            /* Explicit body-only experiment, NOT a general return rewriter.
             * Match this exact known sequence before omitting Rosetta's RET
             * machinery. Guest RDI is x7 and RAX is x0 in these emitted bytes.
             * No guest addresses, QEMU state or generated helper calls run. */
            require(n>=12 && data(result)[0]==0xf94000e0 &&
                    data(result)[1]==0xb1000400 && data(result)[2]==0xd500401f,
                    "expected LDR/ADDS/CFINV prefix");
            uint32_t *body=mmap(NULL,pagesize,PROT_READ|PROT_WRITE,
                                MAP_ANON|MAP_PRIVATE,-1,0);
            require(body!=MAP_FAILED,"body mapping");
            memcpy(body,data(result),12); body[3]=0xd65f03c0; /* native RET */
            require(!mprotect(body,pagesize,PROT_READ|PROT_EXEC),"body RX");
            sys_icache_invalidate(body,16);
            typedef uint64_t (*Body)(uint64_t,uint64_t,uint64_t,uint64_t,
                                    uint64_t,uint64_t,uint64_t,uint64_t *);
            uint64_t input=UINT64_MAX;
            for(unsigned trial=0;trial<10000;trial++) {
                require(((Body)body)(0,0,0,0,0,0,0,&input)==input+1,"body value");
                input=input*6364136223846793005ULL+1;
            }
            munmap(body,pagesize);
            puts("PASS: 10000 body-only load/add results; private RET not executed.");
        }
    }
    /* Inspect bounded fallthrough without executing private exits. */
    for (unsigned mode=0; mode<=2; mode++) {
        unsigned char fallthrough[]={0x48,0x83,0xc0,1};
        printf("FALLTHROUGH mode %u\n",mode); fflush(stdout);
        reset();
        void *module=create((uintptr_t)fallthrough,sizeof(fallthrough),sizeof(fallthrough),mode,0);
        require(module!=NULL,"fallthrough module");
        print(module,1);
        void *result=translate(module,mode);
        require(result!=NULL,"fallthrough translation");
        uint64_t n=bytes(result);
        require(n<=4096 && !(n&3),"bounded fallthrough output");
        printf("ARM bytes %llu branch slots offset %u count %u\n",
               (unsigned long long)n,slots_offset(result),slots_count(result));
        for(uint64_t j=0;j<n/4;j++) printf("%04llx: %08x\n",(unsigned long long)(4*j),data(result)[j]);
        if (mode==0) {
            const uint32_t *words=data(result);
            /* Version-pinned ABI experiment, not a general block validator.
             * Verify every emitted instruction, allowing only target-immediate
             * fields in MOVZ/MOVK to vary with the x86 buffer address. */
            require(n==64 && slots_offset(result)==40 && slots_count(result)==1 &&
                    words[0]==0xb1000400 && words[1]==0xd500401f &&
                    words[2]==0x58000118 &&
                    (words[3]&0xffe0001f)==0xd2c00016 &&
                    (words[4]&0xffe0001f)==0xf2a00016 &&
                    (words[5]&0xffe0001f)==0xf2800016 &&
                    words[6]==0x10000097 && words[7]==0x1000005e &&
                    words[8]==0xd61f0300 && words[9]==0xd503201f,
                    "inspected branch-slot block");
            uint8_t *block=mmap(NULL,pagesize,PROT_READ|PROT_WRITE,
                               MAP_ANON|MAP_PRIVATE,-1,0);
            require(block!=MAP_FAILED,"block mapping");
            memcpy(block,words,n);
            uintptr_t target=(uintptr_t)exit_block;
            memcpy(block+slots_offset(result),&target,sizeof(target));
            require(!mprotect(block,pagesize,PROT_READ|PROT_EXEC),"block RX");
            sys_icache_invalidate(block,n);
            uint64_t input=UINT64_MAX;
            for(unsigned trial=0;trial<10000;trial++) {
                require(enter_block(input,block)==input+1,"branch-slot result");
                input=input*6364136223846793005ULL+1;
            }
            munmap(block,pagesize);
            puts("PASS: 10000 complete bounded blocks, generated instructions unchanged; branch-slot exit.");
        }
    }
    puts("Inspection complete; matched body and bounded branch-slot block executed; no guest execution.");
    return 0;
}
