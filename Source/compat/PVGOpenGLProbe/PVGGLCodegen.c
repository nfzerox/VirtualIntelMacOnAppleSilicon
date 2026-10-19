/* Mojave native GLEngine -> native GLSL compiler boundary. Rebind only the
 * engine's ShLink import; never replace the system framework on disk. */
#include <dlfcn.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach-o/dyld.h>
#include <mach-o/nlist.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>

static unsigned char (*nativeLink)(void *, void *, unsigned, void *);
static void *(*currentContext)(void);
static const unsigned char *(*getString)(unsigned);
static pthread_once_t resolveOnce=PTHREAD_ONCE_INIT;

static void resolve(void) {
    void *compiler=dlopen("/System/Library/Frameworks/OpenGL.framework/Versions/A/Libraries/libGLProgrammability.dylib",RTLD_NOW|RTLD_LOCAL);
    void *gl=dlopen("/System/Library/Frameworks/OpenGL.framework/Versions/A/OpenGL",RTLD_NOW|RTLD_LOCAL);
    nativeLink=compiler?dlsym(compiler,"ShLink"):NULL;
    currentContext=gl?dlsym(gl,"CGLGetCurrentContext"):NULL;
    getString=gl?dlsym(gl,"glGetString"):NULL;
    if(!nativeLink || !currentContext || !getString) abort();
}

static unsigned char linkProgram(void *program,void *shaders,unsigned count,void *options) {
    pthread_once(&resolveOnce,resolve);
    const char *renderer=currentContext()?(const char *)getString(0x1f01):NULL;
    if(getenv("PVG_NATIVE_CODEGEN_TRACE"))
        fprintf(stderr,"PVG GL: ShLink renderer=%s flags=0x%x\n",renderer?:"(no context)",
                options?*(uint32_t *)((uint8_t *)options+20):0);
    if(!options || !renderer || !strstr(renderer,"Paravirtual"))
        return nativeLink(program,shaders,count,options);
    uint32_t *flags=(void *)((uint8_t *)options+20);
    uint32_t saved=*flags;
    /* Native mode0 selects PP(1), optionally with other compile flags.
     * Add the companion representation and modern AIR bit, retaining all
     * independent flags. The debugger experiments established 1 -> 11. */
    if(!(saved&1)) {
        fprintf(stderr,"PVG GL: unexpected native codegen flags 0x%x\n",saved);
        abort();
    }
    *flags=saved|0xa;
    /* GLEngine tests this SAME stack field after ShLink to retrieve the AIR
     * program. Restoring it would hide the representation we just requested. */
    return nativeLink(program,shaders,count,options);
}

static int fileRange(uint64_t offset,uint64_t bytes,const struct segment_command_64 *s) {
    return s->filesize<=s->vmsize && offset>=s->fileoff && bytes<=s->filesize &&
        offset-s->fileoff<=s->filesize-bytes;
}

static void rebind(const struct mach_header *header,intptr_t slide) {
    if(header->magic!=MH_MAGIC_64 || header->cputype!=CPU_TYPE_X86_64) return;
    Dl_info image;
    if(!dladdr(header,&image) || !image.dli_fname || strcmp(image.dli_fname,
        "/System/Library/Frameworks/OpenGL.framework/Versions/A/Resources/GLEngine.bundle/GLEngine")) return;
    const struct mach_header_64 *h=(const void *)header;
    const uint8_t *start=(const void *)(h+1),*end=start+h->sizeofcmds,*p=start;
    const struct segment_command_64 *le=NULL;
    const struct symtab_command *st=NULL;
    const struct dysymtab_command *dt=NULL;
    for(uint32_t i=0;i<h->ncmds;++i) {
        if((size_t)(end-p)<sizeof(struct load_command)) abort();
        const struct load_command *c=(const void *)p;
        if(c->cmdsize<sizeof(*c) || c->cmdsize>(size_t)(end-p)) abort();
        if(c->cmd==LC_SEGMENT_64) {
            if(c->cmdsize<sizeof(struct segment_command_64)) abort();
            const struct segment_command_64 *s=(const void *)c;
            if(!strncmp(s->segname,SEG_LINKEDIT,16)) le=s;
            if(s->nsects>(c->cmdsize-sizeof(*s))/sizeof(struct section_64)) abort();
        } else if(c->cmd==LC_SYMTAB) {
            if(c->cmdsize<sizeof(*st)) abort(); st=(const void *)c;
        } else if(c->cmd==LC_DYSYMTAB) {
            if(c->cmdsize<sizeof(*dt)) abort(); dt=(const void *)c;
        }
        p+=c->cmdsize;
    }
    if(!le || !st || !dt ||
       !fileRange(st->symoff,(uint64_t)st->nsyms*sizeof(struct nlist_64),le) ||
       !fileRange(st->stroff,st->strsize,le) ||
       !fileRange(dt->indirectsymoff,(uint64_t)dt->nindirectsyms*4,le)) abort();
    uintptr_t base=(uintptr_t)slide+le->vmaddr-le->fileoff;
    const struct nlist_64 *symbols=(const void *)(base+st->symoff);
    const char *strings=(const void *)(base+st->stroff);
    const uint32_t *indices=(const void *)(base+dt->indirectsymoff);
    unsigned patched=0;
    for(uint32_t i=0;i<h->ncmds;++i) {
        const struct load_command *c=(const void *)start;
        if(c->cmd==LC_SEGMENT_64) {
            const struct segment_command_64 *s=(const void *)c;
            const struct section_64 *sections=(const void *)(s+1);
            for(uint32_t j=0;j<s->nsects;++j) {
                const struct section_64 *section=sections+j;
                unsigned type=section->flags&SECTION_TYPE;
                if(type!=S_LAZY_SYMBOL_POINTERS && type!=S_NON_LAZY_SYMBOL_POINTERS) continue;
                uint64_t n=section->size/sizeof(void *);
                if(section->size%8 || section->addr<s->vmaddr || section->size>s->vmsize ||
                   section->addr-s->vmaddr>s->vmsize-section->size ||
                   section->reserved1>dt->nindirectsyms || n>dt->nindirectsyms-section->reserved1) abort();
                for(uint64_t k=0;k<n;++k) {
                    uint32_t index=indices[section->reserved1+k];
                    if(index&(INDIRECT_SYMBOL_LOCAL|INDIRECT_SYMBOL_ABS)) continue;
                    if(index>=st->nsyms) abort();
                    uint32_t str=symbols[index].n_un.n_strx;
                    if(str>=st->strsize || !memchr(strings+str,0,st->strsize-str)) abort();
                    if(strcmp(strings+str,"_ShLink")) continue;
                    uintptr_t address=(uintptr_t)slide+section->addr+k*8;
                    if(address%8) abort();
                    mach_vm_address_t page=address&~((uintptr_t)vm_page_size-1);
                    if(mach_vm_protect(mach_task_self(),page,vm_page_size,FALSE,
                        VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY)!=KERN_SUCCESS) abort();
                    __atomic_store_n((void **)address,(void *)linkProgram,__ATOMIC_RELEASE);
                    if(mach_vm_protect(mach_task_self(),page,vm_page_size,FALSE,s->initprot)!=KERN_SUCCESS) abort();
                    ++patched;
                }
            }
        }
        start+=c->cmdsize;
    }
    if(!patched) abort();
    fprintf(stderr,"PVG GL: native ShLink import adapted (%u slots)\n",patched);
}

__attribute__((constructor)) static void activate(void) {
    if(!getenv("PVG_NATIVE_CODEGEN_AUTO")) return;
    char release[64];size_t size=sizeof(release);
    if(sysctlbyname("kern.osrelease",release,&size,NULL,0) || strncmp(release,"18.",3)) return;
    _dyld_register_func_for_add_image(rebind);
}
