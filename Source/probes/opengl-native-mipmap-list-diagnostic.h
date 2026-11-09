/* Historical owned-process diagnostic only; never include in the executor.
 * Execution-bound mipmap scopes in native display lists. No Apple image,
 * dispatch table, import slot or executable page is changed. GLEngine owns
 * compilation, command storage, optimization and sharing. Our share-group
 * list namespace owns auxiliary metadata, including across native optimizer
 * copies. The allocator ABI and callback records are private, studied native
 * interfaces; this is not presented as a public OpenGL extension/API. */
#ifndef VIM_NATIVE_MIPMAP_LIST_H
#define VIM_NATIVE_MIPMAP_LIST_H
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <pthread.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#ifndef DG_NATIVE_MIPMAP_CREATED
#define DG_NATIVE_MIPMAP_CREATED() ((void)0)
#endif
#ifndef DG_NATIVE_MIPMAP_DESTROYED
#define DG_NATIVE_MIPMAP_DESTROYED() ((void)0)
#endif

#define DG_NATIVE_MIPMAP_OPERATIONS(X) \
    X(IMAGE1,glTexImage1D) X(IMAGE2,glTexImage2D) X(IMAGE3,glTexImage3D) \
    X(SUB1,glTexSubImage1D) X(SUB2,glTexSubImage2D) X(SUB3,glTexSubImage3D) \
    X(COMPIMAGE2,glCompressedTexImage2D) X(COMPSUB2,glCompressedTexSubImage2D) \
    X(COPY1,glCopyTexImage1D) X(COPY2,glCopyTexImage2D) \
    X(COPYSUB1,glCopyTexSubImage1D) X(COPYSUB2,glCopyTexSubImage2D) X(COPYSUB3,glCopyTexSubImage3D)
enum DGNativeMipmapOperation {
#define DG_NATIVE_MIPMAP_ENUM(kind,name) DG_NATIVE_MIPMAP_##kind,
    DG_NATIVE_MIPMAP_OPERATIONS(DG_NATIVE_MIPMAP_ENUM)
#undef DG_NATIVE_MIPMAP_ENUM
    DG_NATIVE_MIPMAP_COUNT
};
struct DGNativeMipmapAPI {uintptr_t add,executors[DG_NATIVE_MIPMAP_COUNT];int ready;};
static struct DGNativeMipmapAPI dg_native_mipmap_api;
static pthread_once_t dg_native_mipmap_once=PTHREAD_ONCE_INIT;
static uintptr_t DGNativeMipmapCode(uintptr_t address)
{
#if defined(__arm64__)
    uintptr_t zero=0;__asm__ volatile("pacia %0, %1":"+r"(address):"r"(zero));
#endif
    return address;
}
static void DGNativeMipmapFindAPI(void)
{
    static const char *const names[]={
#define DG_NATIVE_MIPMAP_NAME(kind,name) "_" #name "_ListExec",
        DG_NATIVE_MIPMAP_OPERATIONS(DG_NATIVE_MIPMAP_NAME)
#undef DG_NATIVE_MIPMAP_NAME
    };
    for(unsigned image=0;image<_dyld_image_count();++image){
        const char *path=_dyld_get_image_name(image);
        if(!path||!strstr(path,"/GLEngine.bundle/GLEngine"))continue;
        const struct mach_header_64 *header=(const void *)_dyld_get_image_header(image);
        if(header->magic!=MH_MAGIC_64)continue;
        const struct segment_command_64 *link=NULL,*text=NULL;
        const struct symtab_command *symbols=NULL;
        const struct load_command *command=(const void *)(header+1);
        for(unsigned i=0;i<header->ncmds;++i){
            if(command->cmd==LC_SEGMENT_64){
                const struct segment_command_64 *segment=(const void *)command;
                if(!strcmp(segment->segname,SEG_LINKEDIT))link=segment;
                if(!strcmp(segment->segname,SEG_TEXT))text=segment;
            }else if(command->cmd==LC_SYMTAB)symbols=(const void *)command;
            command=(const void *)((const char *)command+command->cmdsize);
        }
        if(!link||!text||!symbols)return;
        intptr_t slide=_dyld_get_image_vmaddr_slide(image);
        uintptr_t base=slide+link->vmaddr-link->fileoff;
        const struct nlist_64 *table=(const void *)(base+symbols->symoff);
        const char *strings=(const void *)(base+symbols->stroff);
        for(unsigned i=0;i<symbols->nsyms;++i){
            uint32_t index=table[i].n_un.n_strx;
            if((table[i].n_type&N_TYPE)!=N_SECT||index>=symbols->strsize||
               !memchr(strings+index,0,symbols->strsize-index)||table[i].n_value<text->vmaddr||
               table[i].n_value-text->vmaddr>=text->vmsize)continue;
            const char *name=strings+index;uintptr_t address=slide+table[i].n_value;
            if(!strcmp(name,"_gleAddCommand"))dg_native_mipmap_api.add=address;
            else for(unsigned n=0;n<DG_NATIVE_MIPMAP_COUNT;++n)
                if(!strcmp(name,names[n]))dg_native_mipmap_api.executors[n]=DGNativeMipmapCode(address);
        }
        dg_native_mipmap_api.ready=!!dg_native_mipmap_api.add;
        for(unsigned n=0;n<DG_NATIVE_MIPMAP_COUNT;++n)
            if(!dg_native_mipmap_api.executors[n])dg_native_mipmap_api.ready=0;
        return;
    }
}
struct DGNativeMipmapPair {
    GLenum target;GLint level;int copy,complete,recognized;uintptr_t expected;
    struct DGNativeMipmapPair *next;
};
struct DGNativeMipmapList {GLuint name;struct DGNativeMipmapPair *pairs;struct DGNativeMipmapList *next;};
struct DGNativeMipmapRecord {uint32_t bytes,padding;struct DGNativeMipmapPair *pair;};
#if defined(__LP64__)
_Static_assert(sizeof(struct DGNativeMipmapRecord)==16,"native 64-bit command payload");
#endif
struct DGNativeMipmapPlayback {
    CGLContextObj context;const struct DGMipmapExecution *execution;GLenum *error;
    int inside_begin,nonzero_possible;
};
static __thread struct DGNativeMipmapPlayback *dg_native_mipmap_playback;
static __thread struct {
    struct {struct DGNativeMipmapPair *pair;struct DGNativeMipmapPlayback *playback;
        struct DGAutomaticMipmapScope scope;} frames[64];
    unsigned count,overflow;
} dg_native_mipmap_stack;
static struct DGNativeMipmapRecord DGNativeMipmapReadRecord(const void *data)
{
    /* Native texture records have four-byte, not pointer-size alignment. */
    struct DGNativeMipmapRecord record;memcpy(&record,data,sizeof(record));return record;
}
static void DGNativeMipmapFreePairs(struct DGNativeMipmapList *list)
{
    while(list->pairs){struct DGNativeMipmapPair *pair=list->pairs;list->pairs=pair->next;
        DG_NATIVE_MIPMAP_DESTROYED();free(pair);}
}
static unsigned DGNativeMipmapBefore(GLIContext context,const void *data)
{
    struct DGNativeMipmapRecord record=DGNativeMipmapReadRecord(data);struct DGNativeMipmapPair *pair=record.pair;
    if(!__atomic_load_n(&pair->complete,__ATOMIC_ACQUIRE))return record.bytes;
    int recognized=__atomic_load_n(&pair->recognized,__ATOMIC_ACQUIRE);
    if(!recognized){
        /* Native compile-time validation can emit an error command instead
         * of an upload. Inspect the actual immutable command, not the compile/execute error
         * flag (which can instead describe an initial execution-only error). */
        uintptr_t next=0;memcpy(&next,(const char *)data+record.bytes,sizeof(next));
        recognized=next==pair->expected?1:2;
        __atomic_store_n(&pair->recognized,recognized,__ATOMIC_RELEASE);
    }
    if(recognized!=1)return record.bytes;
    if(dg_native_mipmap_stack.overflow||dg_native_mipmap_stack.count==64){
        ++dg_native_mipmap_stack.overflow;return record.bytes;
    }
    unsigned slot=dg_native_mipmap_stack.count++;
    dg_native_mipmap_stack.frames[slot].pair=pair;
    dg_native_mipmap_stack.frames[slot].playback=dg_native_mipmap_playback;
    struct DGNativeMipmapPlayback *playback=dg_native_mipmap_playback;
    struct DGAutomaticMipmapScope scope={0};
    if(playback&&playback->context==CGLGetCurrentContext()&&playback->context->rend==context)
        scope=DGBeginAutomaticMipmapExecute(pair->target,pair->level,1,playback->inside_begin,0,
            playback->error,pair->copy,playback->nonzero_possible,playback->execution);
    dg_native_mipmap_stack.frames[slot].scope=scope;
    return record.bytes;
}
static unsigned DGNativeMipmapAfter(GLIContext context,const void *data)
{
    (void)context;struct DGNativeMipmapRecord record=DGNativeMipmapReadRecord(data);
    if(!__atomic_load_n(&record.pair->complete,__ATOMIC_ACQUIRE)||
       __atomic_load_n(&record.pair->recognized,__ATOMIC_ACQUIRE)!=1)return record.bytes;
    if(dg_native_mipmap_stack.overflow){--dg_native_mipmap_stack.overflow;return record.bytes;}
    if(!dg_native_mipmap_stack.count)return record.bytes;
    unsigned slot=dg_native_mipmap_stack.count-1;
    if(dg_native_mipmap_stack.frames[slot].pair!=record.pair)return record.bytes;
    struct DGNativeMipmapPlayback *playback=dg_native_mipmap_stack.frames[slot].playback;
    if(playback)DGEndAutomaticMipmap(&dg_native_mipmap_stack.frames[slot].scope,
        playback->execution,playback->error);
    --dg_native_mipmap_stack.count;return record.bytes;
}
static struct DGNativeMipmapPair *DGNativeMipmapBegin(enum DGNativeMipmapOperation operation,
    GLenum target,GLint level,int legacy,int inside_begin,GLenum list_mode,struct DGNativeMipmapList *owner)
{
#if !defined(__arm64__)
    (void)operation;(void)target;(void)level;(void)legacy;(void)inside_begin;(void)list_mode;(void)owner;
    return NULL;
#else
    if(!owner||!legacy||inside_begin||level<0||(unsigned)operation>=DG_NATIVE_MIPMAP_COUNT||
       (list_mode!=GL_COMPILE&&list_mode!=GL_COMPILE_AND_EXECUTE)||
       (target!=GL_TEXTURE_1D&&target!=GL_TEXTURE_2D&&target!=GL_TEXTURE_3D&&
        DGAutomaticMipmapTarget(target)!=GL_TEXTURE_CUBE_MAP))return NULL;
    const char *version=(const char *)glGetString(GL_VERSION);
    if(!version||!strstr(version,"Metal"))return NULL;
    pthread_once(&dg_native_mipmap_once,DGNativeMipmapFindAPI);
    if(!dg_native_mipmap_api.ready)return NULL;
    struct DGNativeMipmapPair *pair=calloc(1,sizeof(*pair));if(!pair)return NULL;
    pair->target=target;pair->level=level;pair->copy=operation>=DG_NATIVE_MIPMAP_COPY1;
    pair->expected=dg_native_mipmap_api.executors[operation];
    typedef void *(*Add)(GLIContext,uintptr_t,size_t);
    struct DGNativeMipmapRecord record={sizeof(record),0,pair};
    void *data=((Add)dg_native_mipmap_api.add)(CGLGetCurrentContext()->rend,
        DGNativeMipmapCode((uintptr_t)&DGNativeMipmapBefore),sizeof(record));
    if(!data){free(pair);return NULL;}
    memcpy(data,&record,sizeof(record));pair->next=owner->pairs;owner->pairs=pair;
    DG_NATIVE_MIPMAP_CREATED();return pair;
#endif
}
static void DGNativeMipmapEnd(struct DGNativeMipmapPair *pair)
{
    if(!pair)return;
    typedef void *(*Add)(GLIContext,uintptr_t,size_t);
    struct DGNativeMipmapRecord record={sizeof(record),0,pair};
    void *data=((Add)dg_native_mipmap_api.add)(CGLGetCurrentContext()->rend,
        DGNativeMipmapCode((uintptr_t)&DGNativeMipmapAfter),sizeof(record));
    if(!data)return;
    memcpy(data,&record,sizeof(record));
    __atomic_store_n(&pair->complete,1,__ATOMIC_RELEASE);
}
#endif
