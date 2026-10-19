/* Opt-in research hooks. No on-disk system-framework patch or injected library.
 * Opt-in normal-dependency activation runs before creating contexts.
 * Both relocated prologues contain ONLY position-independent instructions. */
#include "PVGGLConfig.h"
#include <dlfcn.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach/i386/thread_status.h>
#include <mach-o/dyld.h>
#include <mach-o/nlist.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <unistd.h>

typedef struct Entry {
    void *context;
    void *native;
    PVGGLConfig *config;
    struct Entry *next;
} Entry;
static Entry *entries;
static pthread_mutex_t entryLock = PTHREAD_MUTEX_INITIALIZER;
static void (*populateOriginal)(void *, void *);
static void (*deallocOriginal)(void *);
static void (*metadataOriginal)(void *);
static unsigned char (*entryOriginal)(void *);
extern void *PVGGLProgramCreateOriginal,*PVGGLProgramDispatchOriginal;
extern int PVGGLProgramCreate(void *,void **,void *,void *);
extern uintptr_t PVGGLProgramDispatch(void *,void *,void *);
extern int PVGGLMetadataBeforeLink(void *);
extern int PVGGLMetadataEntry(void *);
static void metadata(void *shader) {
    void *module;memcpy(&module,(uint8_t *)shader+0x108,sizeof(module));
    if(!PVGGLMetadataBeforeLink(module))abort();
    metadataOriginal(shader);
}
static unsigned char entry(void *shader) {
    void *module;memcpy(&module,(uint8_t *)shader+0x108,sizeof(module));
    if(!PVGGLMetadataEntry(module))abort();
    return entryOriginal(shader);
}
static unsigned created, destroyed;

static void populate(void *context, void *native) {
    Entry *entry = calloc(1, sizeof(*entry));
    if (!entry) abort();
    entry->config = PVGGLConfigCreate(native, PVGGLNativeConfigSize);
    if (!entry->config) abort();
    entry->context = context;
    entry->native = native;
    pthread_mutex_lock(&entryLock);
    /* Re-population needs a separate synchronization contract. Fail closed
     * rather than treating our donor-sized allocation as native storage. */
    for (Entry *e = entries; e; e = e->next)
        if (e->context == context) abort();
    entry->next = entries; entries = entry; ++created;
    pthread_mutex_unlock(&entryLock);
    void *donor = PVGGLConfigDonor(entry->config);
    memcpy((uint8_t *)context + 0xa0, &donor, sizeof(donor));
    populateOriginal(context, donor);
    PVGGLConfigPublish(entry->config);
}

static void dealloc(void *context) {
    pthread_mutex_lock(&entryLock);
    Entry **link = &entries;
    while (*link && (*link)->context != context) link = &(*link)->next;
    Entry *entry = *link;
    if (entry) *link = entry->next;
    pthread_mutex_unlock(&entryLock);
    /* Failed initialization can deallocate before populate was reached. */
    deallocOriginal(context);
    if (entry) {
        PVGGLConfigDestroy(entry->config);
        free(entry);
        pthread_mutex_lock(&entryLock); ++destroyed;
        pthread_mutex_unlock(&entryLock);
    }
}

unsigned PVGGLConfigHooksOutstanding(void) {
    pthread_mutex_lock(&entryLock);
    unsigned result = created - destroyed;
    pthread_mutex_unlock(&entryLock);
    return result;
}

int PVGGLConfigOwnsNative(void *native) {
    int found=0;
    pthread_mutex_lock(&entryLock);
    for(Entry *e=entries;e;e=e->next)if(e->native==native){found=1;break;}
    pthread_mutex_unlock(&entryLock);
    return found;
}

__attribute__((destructor)) static void report(void) {
    if (getenv("PVG_NATIVE_CONFIG_HOOKS") || getenv("PVG_NATIVE_CONFIG_AUTO"))
        fprintf(stderr, "compiled config hooks: created=%u destroyed=%u outstanding=%u\n",
                created, destroyed, PVGGLConfigHooksOutstanding());
}

static void jump(uint8_t *code, const void *target) {
    const uint8_t op[] = {0xff, 0x25, 0, 0, 0, 0};
    memcpy(code, op, sizeof(op)); memcpy(code + 6, &target, 8);
}

/* Targets come from validated image symbols, not fixed guest addresses.
 * Return 1 only after all requested hooks are live; 0 means no installed change. */
static int installHooks(uint8_t **targets,void **replacements,
                       const uint8_t prefixes[2][20],const size_t *lengths,
                       void ***originals,const char *imageSuffix,unsigned hookCount) {
    if(!hookCount || hookCount>2)return 0;
    for(unsigned i=0;i<hookCount;++i)if(*originals[i] || !targets[i])return 0;
    char release[64]; size_t releaseSize = sizeof(release);
    if (sysctlbyname("kern.osrelease", release, &releaseSize, NULL, 0) ||
        strncmp(release, "18.", 3)) return 0;
    Dl_info a;
    if (!dladdr(targets[0], &a) || !a.dli_fname ||
        !strstr(a.dli_fname, imageSuffix)) return 0;
    for(unsigned i=1;i<hookCount;++i) {
        Dl_info b;
        if(!dladdr(targets[i],&b)||a.dli_fbase!=b.dli_fbase)return 0;
    }
    for (unsigned i = 0; i < hookCount; ++i)
        if (lengths[i]<14 || lengths[i]>20 || memcmp(targets[i], prefixes[i], lengths[i])) return 0;
    uint8_t *tramp = mmap(NULL, vm_page_size, PROT_READ|PROT_WRITE,
                          MAP_PRIVATE|MAP_ANON, -1, 0);
    if (tramp == MAP_FAILED) return 0;
    for (unsigned i = 0; i < hookCount; ++i) {
        memcpy(tramp + i*64, prefixes[i], lengths[i]);
        jump(tramp + i*64 + lengths[i], targets[i] + lengths[i]);
    }
    if (mprotect(tramp, vm_page_size, PROT_READ|PROT_EXEC)) {
        munmap(tramp, vm_page_size); return 0;
    }
    thread_act_array_t threads; mach_msg_type_number_t count;
    if (task_threads(mach_task_self(), &threads, &count) != KERN_SUCCESS) {
        munmap(tramp, vm_page_size); return 0;
    }
    thread_t self = mach_thread_self();
    unsigned suspendedThrough = 0, writable = 0; int ok = 1;
    mach_vm_address_t pages[2]; mach_vm_size_t sizes[2];
    for (unsigned i = 0; i < hookCount; ++i) {
        pages[i] = (uintptr_t)targets[i] & ~((uintptr_t)vm_page_size-1);
        sizes[i] = (((uintptr_t)targets[i]+lengths[i]+vm_page_size-1) &
                    ~((uintptr_t)vm_page_size-1)) - pages[i];
    }
    for (unsigned i = 0; i < count && ok; ++i) {
        if (threads[i] == self) continue;
        if (thread_suspend(threads[i]) != KERN_SUCCESS) { ok = 0; break; }
        suspendedThrough = i+1;
        x86_thread_state64_t state; mach_msg_type_number_t n = x86_THREAD_STATE64_COUNT;
        if (thread_get_state(threads[i], x86_THREAD_STATE64,
                            (thread_state_t)&state, &n) != KERN_SUCCESS) { ok = 0; break; }
        for (unsigned j = 0; j < hookCount; ++j)
            if (state.__rip >= (uintptr_t)targets[j] &&
                state.__rip < (uintptr_t)targets[j]+lengths[j]) ok = 0;
    }
    for (unsigned i = 0; i < hookCount && ok; ++i) {
        if (memcmp(targets[i], prefixes[i], lengths[i]) ||
            mach_vm_protect(mach_task_self(), pages[i], sizes[i], FALSE,
                           VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY) != KERN_SUCCESS) ok = 0;
        else ++writable;
    }
    if (ok) {
        for (unsigned i = 0; i < hookCount; ++i) {
            *originals[i]=(void *)(tramp+i*64);
            uint8_t patch[20]; memset(patch, 0x90, sizeof(patch));
            jump(patch, replacements[i]); memcpy(targets[i], patch, lengths[i]);
            __builtin___clear_cache((char *)targets[i], (char *)targets[i]+lengths[i]);
        }
    }
    for (unsigned i = 0; i < writable; ++i)
        if (mach_vm_protect(mach_task_self(), pages[i], sizes[i], FALSE,
                           VM_PROT_READ|VM_PROT_EXECUTE) != KERN_SUCCESS) _exit(70);
    for (unsigned i = 0; i < suspendedThrough; ++i)
        if (threads[i] != self) thread_resume(threads[i]);
    for (unsigned i = 0; i < count; ++i) mach_port_deallocate(mach_task_self(), threads[i]);
    mach_port_deallocate(mach_task_self(), self);
    vm_deallocate(mach_task_self(), (vm_address_t)threads, count*sizeof(thread_t));
    if (!ok) munmap(tramp, vm_page_size);
    return ok;
}

static int installPair(uint8_t **targets,void **replacements,
                       const uint8_t prefixes[2][20],const size_t *lengths,
                       void ***originals,const char *imageSuffix) {
    return installHooks(targets,replacements,prefixes,lengths,originals,imageSuffix,2);
}

int PVGGLConfigHooksInstall(void *populateTarget,void *deallocTarget) {
    const uint8_t prefixes[2][20]={
        {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x54,0x53,0x48,0x83,0xec,0x10},
        {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x54,0x53,0x48,0x83,0xec,0x30}};
    size_t lengths[]={15,15};uint8_t *targets[]={populateTarget,deallocTarget};
    void *replacements[]={(void *)populate,(void *)dealloc};
    void **originals[]={(void **)&populateOriginal,(void **)&deallocOriginal};
    return installPair(targets,replacements,prefixes,lengths,originals,
        "/AppleMetalOpenGLRenderer.bundle/Contents/MacOS/AppleMetalOpenGLRenderer");
}

static int installMetadata(void *queryTarget,void *entryTarget) {
    const uint8_t prefixes[2][20]={
        {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x54,0x53,0x48,0x83,0xec,0x20},
        {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x81,0xec,0x58,0x01,0,0}};
    size_t lengths[]={15,20};uint8_t *targets[]={queryTarget,entryTarget};
    void *replacements[]={(void *)metadata,(void *)entry};
    void **originals[]={(void **)&metadataOriginal,(void **)&entryOriginal};
    return installPair(targets,replacements,prefixes,lengths,originals,
        "/AppleMetalOpenGLRenderer.bundle/Contents/PlugIns/libGLDPlugin3902.dylib");
}

static int installPrograms(void *createTarget,void *dispatchTarget) {
    const uint8_t prefixes[2][20]={
        {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x50},
        {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x81,0xec,0xb8,0,0,0}};
    size_t lengths[]={14,20};uint8_t *targets[]={createTarget,dispatchTarget};
    void *replacements[]={(void *)PVGGLProgramCreate,(void *)PVGGLProgramDispatch};
    void **originals[]={&PVGGLProgramCreateOriginal,&PVGGLProgramDispatchOriginal};
    return installPair(targets,replacements,prefixes,lengths,originals,
        "/AppleMetalOpenGLRenderer.bundle/Contents/MacOS/AppleMetalOpenGLRenderer");
}

extern void *PVGGLLegacyOriginal[6],*PVGGLLegacyPP[2];
extern void (*PVGGLLegacyRelease)(void *);
extern void *PVGGLLegacyBuffersOriginal;
extern void PVGGLLegacyBuffers(void *);
extern void *PVGGLLegacyListOriginal;
extern void PVGGLLegacyList(void *,void *);
extern void *PVGGLLegacyListDrawOriginal;
extern void PVGGLLegacyListDraw(void *,void *);
extern void PVGGLLegacyVertexUpdate(void *),PVGGLLegacyFragmentUpdate(void *);
extern void *PVGGLLegacyVertexIR(void *,void *,void *,void *,void *,void *);
extern void *PVGGLLegacyFragmentIR(void *,void *,void *,void *,void *);
extern void *PVGGLLegacyCreate(void *,unsigned,void *,void *,uint64_t,uint64_t,unsigned,uint64_t);
extern void PVGGLLegacyRemap(void *,void *);
extern void *PVGGLLegacyParseOriginal;
extern void PVGGLLegacyParse(void *,void *,unsigned);
static int installLegacy(void **targets,const char *path) {
    static const uint8_t prefixes[6][20]={
        {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x81,0xec,0x28,0x01,0,0},
        {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x81,0xec,0xf8,0,0,0},
        {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x81,0xec,0xe8,0x07,0,0},
        {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x81,0xec,0x38,0x01,0,0},
        {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x28},
        {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x18}};
    size_t lengths[]={20,20,20,20,17,17};
    void *replacements[]={PVGGLLegacyVertexUpdate,PVGGLLegacyFragmentUpdate,
        PVGGLLegacyVertexIR,PVGGLLegacyFragmentIR,PVGGLLegacyCreate,PVGGLLegacyRemap};
    static const uint8_t bufferPrefix[2][20]={{0x55,0x48,0x89,0xe5,0x41,0x56,0x53,
        0x8b,0x87,0x2c,0x05,0,0,0xa9,0,0,0xfc,0x0f}};
    for(unsigned i=0;i<11;++i)if(!targets[i])return 0;
    for(unsigned i=0;i<6;++i)if(memcmp(targets[i],prefixes[i],lengths[i]))return 0;
    if(memcmp(targets[8],bufferPrefix[0],18))return 0;
    static const uint8_t listPrefix[2][20]={
        {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x50},
        {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x18}};
    if(memcmp(targets[9],listPrefix[0],14))return 0;
    if(memcmp(targets[10],listPrefix[1],17))return 0;
    static const uint8_t parsePrefix[1][20]={{0x55,0x48,0x89,0xe5,0x41,0x57,
        0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x81,0xec,0x88,0,0,0}};
    if(getenv("PVG_NATIVE_ARB_AUTO")) {
        if(!targets[11]||memcmp(targets[11],parsePrefix[0],20))return 0;
        void *replacement[]={PVGGLLegacyParse};
        void **original[]={&PVGGLLegacyParseOriginal};size_t length[]={20};
        if(!installHooks((uint8_t **)(targets+11),replacement,parsePrefix,
                         length,original,path,1))return 0;
    }
    void *handle=dlopen(path,RTLD_LAZY|RTLD_NOLOAD);
    if(!handle)return 0;
    PVGGLLegacyRelease=dlsym(handle,"PPStreamTokenFree");
    dlclose(handle);
    if(!PVGGLLegacyRelease)return 0;
    PVGGLLegacyPP[0]=targets[6];PVGGLLegacyPP[1]=targets[7];
    void *bufferReplacement[]={PVGGLLegacyBuffers};
    void **bufferOriginal[]={&PVGGLLegacyBuffersOriginal};
    size_t bufferLength[]={18};
    if(!installHooks((uint8_t **)(targets+8),bufferReplacement,bufferPrefix,
                     bufferLength,bufferOriginal,path,1))return 0;
    void *listReplacement[]={PVGGLLegacyList,PVGGLLegacyListDraw};
    void **listOriginal[]={&PVGGLLegacyListOriginal,&PVGGLLegacyListDrawOriginal};
    size_t listLength[]={14,17};
    if(!installHooks((uint8_t **)(targets+9),listReplacement,listPrefix,
                    listLength,listOriginal,path,2))return 0;
    /* Enable the scoped update wrappers last, after all transfer hooks exist.
     * A validation/protection failure is fatal in this opt-in process. */
    const unsigned order[]={2,4,0};
    for(unsigned j=0;j<3;++j) {
        unsigned i=order[j];
        void **originals[]={&PVGGLLegacyOriginal[i],&PVGGLLegacyOriginal[i+1]};
        if(!installPair((uint8_t **)(targets+i),replacements+i,prefixes+i,
                         lengths+i,originals,path))return 0;
    }
    return 1;
}

/* Resolve local donor symbols from its mapped Mach-O, not fixed addresses.
 * This parser is restricted to a valid loaded x86_64 Mach-O and validates
 * symbol/string tables against the mapped __LINKEDIT segment. */
static int inFileRange(uint64_t offset, uint64_t bytes,
                      const struct segment_command_64 *segment) {
    return segment->filesize <= segment->vmsize &&
        offset >= segment->fileoff && bytes <= segment->filesize &&
        offset - segment->fileoff <= segment->filesize - bytes;
}

static void addedImage(const struct mach_header *header, intptr_t slide) {
    if (header->magic != MH_MAGIC_64 ||
        header->cputype != CPU_TYPE_X86_64) return;
    Dl_info image;
    if (!dladdr(header, &image) || !image.dli_fname) return;
    int native=!strcmp(image.dli_fname,"/System/Library/Frameworks/OpenGL.framework/Versions/A/Resources/GLEngine.bundle/GLEngine");
    int plugin=!!strstr(image.dli_fname,"/AppleMetalOpenGLRenderer.bundle/Contents/PlugIns/libGLDPlugin3902.dylib");
    if(native) {
        if(PVGGLLegacyOriginal[0] || !getenv("PVG_NATIVE_LEGACY_AUTO"))return;
    } else if(plugin) {
        if(metadataOriginal || !getenv("PVG_NATIVE_METADATA_AUTO"))return;
    } else if((populateOriginal && (!getenv("PVG_NATIVE_PROGRAM_AUTO") || PVGGLProgramCreateOriginal)) ||
        (!getenv("PVG_NATIVE_CONFIG_AUTO")&&!getenv("PVG_NATIVE_PROGRAM_AUTO")) ||
        !strstr(image.dli_fname,"/AppleMetalOpenGLRenderer.bundle/Contents/MacOS/AppleMetalOpenGLRenderer"))return;
    const struct mach_header_64 *h = (const void *)header;
    const uint8_t *cursor = (const void *)(h+1), *end = cursor+h->sizeofcmds;
    const struct segment_command_64 *linkedit = NULL, *text = NULL;
    const struct symtab_command *table = NULL;
    for (uint32_t i=0; i<h->ncmds; ++i) {
        if ((size_t)(end-cursor) < sizeof(struct load_command)) abort();
        const struct load_command *command = (const void *)cursor;
        if (command->cmdsize < sizeof(*command) || command->cmdsize > (size_t)(end-cursor)) abort();
        if (command->cmd == LC_SEGMENT_64) {
            if (command->cmdsize < sizeof(struct segment_command_64)) abort();
            const struct segment_command_64 *segment = (const void *)command;
            if (!strncmp(segment->segname, SEG_LINKEDIT, 16)) linkedit=segment;
            if (!strncmp(segment->segname, SEG_TEXT, 16)) text=segment;
        } else if (command->cmd == LC_SYMTAB) {
            if (command->cmdsize < sizeof(struct symtab_command)) abort();
            table=(const void *)command;
        }
        cursor+=command->cmdsize;
    }
    if (!linkedit || !text || !table ||
        !inFileRange(table->symoff, (uint64_t)table->nsyms*sizeof(struct nlist_64), linkedit) ||
        !inFileRange(table->stroff, table->strsize, linkedit)) abort();
    uintptr_t base = (uintptr_t)slide+linkedit->vmaddr-linkedit->fileoff;
    const struct nlist_64 *symbols = (const void *)(base+table->symoff);
    const char *strings = (const void *)(base+table->stroff);
    const char *names[12] = {"__ZN13GLDContextRec18populateConfigDataEP19GLDContextConfigRec",
                          "__ZN13GLDContextRec7deallocEv","_gldCreateProgram","_gldUpdateDispatch"};
    unsigned targetCount=(!plugin && getenv("PVG_NATIVE_PROGRAM_AUTO"))?4:2;
    if(plugin) {
        names[0]="__ZN9GLDShader23queryMetadataBeforeLinkEv";
        names[1]="__ZN9GLDShader18setupGLDEntryPointEv";
    }
    if(native) {
        const char *legacy[]={"_gleUpdateVertexStateProgram","_gleUpdateFragmentStateProgram",
            "_gleCallVertexStateToLLVMIR","_gleCallFragmentStateToLLVMIR",
            "_gleCreateStateProgram","_glePPUpdateProgram",
            "_gleVertexStateToPPStream","_gleFragmentStateToPPStream",
            "_gleUpdateContextStateBuffers","_gleSetupAndDrawArraysOrElementsOutOfLine_ListExec",
            "_gleDrawArraysOrElementsOutOfLine_ListExec","_glePPParse"};
        memcpy(names,legacy,sizeof(legacy));targetCount=getenv("PVG_NATIVE_ARB_AUTO")?12:11;
    }
    void *targets[12] = {0};
    for (uint32_t i=0; i<table->nsyms; ++i) {
        const struct nlist_64 *s = symbols+i;
        if ((s->n_type & N_STAB) || (s->n_type & N_TYPE) != N_SECT ||
            s->n_un.n_strx >= table->strsize) continue;
        const char *name = strings+s->n_un.n_strx;
        if (!memchr(name, 0, table->strsize-s->n_un.n_strx)) abort();
        for (unsigned j=0; j<targetCount; ++j) if (!strcmp(name,names[j])) {
            if (targets[j] || s->n_value < text->vmaddr ||
                text->vmsize < 20 || s->n_value-text->vmaddr > text->vmsize-20) abort();
            targets[j]=(void *)((uintptr_t)slide+s->n_value);
        }
    }
    if(native) {
        if(!installLegacy(targets,image.dli_fname)) {
            fprintf(stderr,"PVG GL: native legacy hook validation failed\n");abort();
        }
        fprintf(stderr,"PVG GL: scoped native legacy generation hooks installed\n");
        return;
    }
    if (!targets[0] || !targets[1] || !(plugin?installMetadata(targets[0],targets[1]):PVGGLConfigHooksInstall(targets[0],targets[1]))) {
        fprintf(stderr,"PVG GL: donor %s hook validation failed\n",plugin?"metadata":"configuration"); abort();
    }
    fprintf(stderr,"PVG GL: automatic donor %s hooks installed\n",plugin?"metadata":"configuration");
    if(targetCount==4) {
        if(!targets[2]||!targets[3]||!installPrograms(targets[2],targets[3]))abort();
        fprintf(stderr,"PVG GL: automatic program descriptor hooks installed\n");
    }
}

__attribute__((constructor)) static void activate(void) {
    /* Explicit research switch until shader/descriptor adapters are complete.
     * Catalina and 32-bit software GL must not enter the Mojave adapter. */
    if (!getenv("PVG_NATIVE_CONFIG_AUTO") && !getenv("PVG_NATIVE_METADATA_AUTO") && !getenv("PVG_NATIVE_PROGRAM_AUTO") && !getenv("PVG_NATIVE_LEGACY_AUTO")) return;
    char release[64]; size_t size=sizeof(release);
    if (sysctlbyname("kern.osrelease",release,&size,NULL,0) || strncmp(release,"18.",3)) return;
    _dyld_register_func_for_add_image(addedImage);
}
