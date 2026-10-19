/* Mojave shared-program descriptor ownership. Borrow native fields; own only
 * the expanded descriptor and per-object vtable. No global lookup per draw. */
#include <stddef.h>
#include <stdint.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *PVGGLProgramCreateOriginal;
void *PVGGLProgramDispatchOriginal;
/* gleCreateProgramObjectNamed passes allocation+0x1a90 on Mojave and
 * allocation+0x1ab8 on Catalina. The following per-device table begins at
 * +0x1c88 / +0x1cc0 respectively. Borrow this bounded span, not just the
 * first 0x100 bytes exercised by simple shaders. In particular the donor
 * reads +0x182 (native +0x172) for the primitive mode. These are descriptor
 * ABI extents, not addresses to patch in the guest's GLEngine. */
enum { NativeProgramSpan = 0x1f8, DonorProgramSpan = 0x208 };
typedef struct Program {
    uint64_t magic;
    void *native;
    void **originalVtable;
    void *vtable[7]; /* two Itanium ABI header words plus five methods */
    pthread_mutex_t lock;
    unsigned char donor[DonorProgramSpan];
} Program;
static const uint64_t magic=UINT64_C(0x50564750524f4731);
static unsigned created,destroyed;
static int trace,policy;
extern _Thread_local void *PVGGLLegacyListContext;
__attribute__((constructor)) static void configureTrace(void) {
    trace=getenv("PVG_NATIVE_PROGRAM_TRACE")!=NULL;
    policy=getenv("PVG_NATIVE_POLICY_AUTO")!=NULL;
}

static int selectPolicy(uint8_t submit,uint16_t indexType,uint8_t out[2]) {
    /* Native glBegin/Rect select 0; array entry points select 255. Index
     * type is assigned before dispatch (0x14ff is the nonindexed sentinel).
     * These are capability requests, not byte strides. In the donor's
     * repacked group, 0x8f/0x8e accept all these index/array requests. */
    if(submit==0){out[0]=1;out[1]=1;return 1;}
    if(submit!=255)return 0;
    switch(indexType) {
        case 0x14ff:out[0]=1;break;
        case 0x1401:out[0]=2;break;
        case 0x1403:out[0]=4;break;
        case 0x1405:out[0]=8;break;
        default:return 0;
    }
    out[1]=4;return 1;
}

static void updatePolicy(void *context,void *dispatch) {
    uint8_t *state;memcpy(&state,(uint8_t *)context+0x88,sizeof(state));
    uint8_t *native=state-0x810,values[2];uint16_t indexType;
    memcpy(&indexType,native+0xa3e8,sizeof(indexType));
    if(PVGGLLegacyListContext==native) {
        /* ListExec does not update a3e8. Catalina requests 1/4 with a vertex
         * object and 1/1 otherwise, independent of the previous index type. */
        void *vertexObject;memcpy(&vertexObject,native+0x9120,sizeof(vertexObject));
        values[0]=1;values[1]=vertexObject?4:1;
    } else if(!selectPolicy(native[0xc35c],indexType,values)) {
        fprintf(stderr,"PVG GL: unclassified native draw policy %u/0x%x\n",native[0xc35c],indexType);
        abort();
    }
    memcpy(state+12,values,sizeof(values));
    uint32_t dirty;memcpy(&dirty,(uint8_t *)dispatch+4,sizeof(dirty));
    /* The donor can change dispatch groups and their capability tables in
     * this call. Re-select both fields even if the requested masks match the
     * previous call. This runs on state dispatch, not on every cached draw. */
    dirty|=0x06000000;
    memcpy((uint8_t *)dispatch+4,&dirty,sizeof(dirty));
}

static Program *owner(void *object) {
    void *descriptor;memcpy(&descriptor,(char *)object+0x18,sizeof(descriptor));
    Program *p=(void *)((char *)descriptor - offsetof(Program,donor));
    if(p->magic!=magic)abort();
    return p;
}
static void refresh(Program *p) {
    pthread_mutex_lock(&p->lock);
    memcpy(p->donor,p->native,0x10);
    memcpy(p->donor+0x20,(char *)p->native+0x10,NativeProgramSpan-0x10);
    pthread_mutex_unlock(&p->lock);
}
static void finalDealloc(void *object) {
    Program *p=owner(object);
    void (*original)(void *)=p->originalVtable[4];
    /* Original dealloc may virtually destroy/delete the object. Restore the
     * actual donor vtable first and retain our storage until it returns. */
    memcpy(object,&p->originalVtable,sizeof(p->originalVtable));
    original(object);
    p->magic=0;pthread_mutex_destroy(&p->lock);free(p);
    __atomic_add_fetch(&destroyed,1,__ATOMIC_RELAXED);
}
int PVGGLProgramCreate(void *share,void **out,void *native,void *pipelines) {
    int (*original)(void *,void **,void *,void *)=PVGGLProgramCreateOriginal;
    int result=original(share,out,native,pipelines);
    if(trace)fprintf(stderr,"program create result=%d object=%p native=%p\n",result,out?*out:NULL,native);
    if(result || !out || !*out)return result;
    void *actual;memcpy(&actual,(char *)*out+0x18,sizeof(actual));
    if(actual!=native || !native)abort();
    Program *p=calloc(1,sizeof(*p));if(!p)abort();
    p->magic=magic;p->native=native;
    if(pthread_mutex_init(&p->lock,NULL))abort();
    memcpy(&p->originalVtable,*out,sizeof(p->originalVtable));
    /* The matched donor class has a ten-byte tail-jump dealloc thunk. Never
     * overwrite that text with a fourteen-byte jump. */
    const unsigned char expected[]={0x55,0x48,0x89,0xe5,0x5d,0xe9};
    if(memcmp(p->originalVtable[4],expected,sizeof(expected)))abort();
    memcpy(p->vtable,p->originalVtable-2,sizeof(p->vtable));
    p->vtable[6]=(void *)finalDealloc;
    refresh(p);
    void *descriptor=p->donor,*vtable=p->vtable+2;
    memcpy((char *)*out+0x18,&descriptor,sizeof(descriptor));
    memcpy(*out,&vtable,sizeof(vtable));
    __atomic_add_fetch(&created,1,__ATOMIC_RELAXED);
    return result;
}
uintptr_t PVGGLProgramDispatch(void *context,void *unused,void *dispatch) {
    if(policy)updatePolicy(context,dispatch);
    void *objects;memcpy(&objects,(char *)context+0x90,sizeof(objects));
    /* gldUpdateDispatch's own loop (5c074..5c0a4) has FIVE graphics
     * stages. The following pointer is not another GLDProgramRec. */
    void *seen[5];unsigned seenCount=0;
    for(unsigned stage=0;stage<5;++stage) {
        void *object;memcpy(&object,(char *)objects+0x448+stage*8,sizeof(object));
        if(!object)continue;
        if(trace) {
            void *descriptor;memcpy(&descriptor,(char *)object+0x18,sizeof(descriptor));
            fprintf(stderr,"program dispatch stage=%u object=%p descriptor=%p\n",stage,object,descriptor);
        }
        unsigned i=0;while(i<seenCount&&seen[i]!=object)++i;
        if(i<seenCount)continue;
        seen[seenCount++]=object;refresh(owner(object));
    }
    uintptr_t (*original)(void *,void *,void *)=PVGGLProgramDispatchOriginal;
    return original(context,unused,dispatch);
}
__attribute__((destructor)) static void report(void) {
    if(getenv("PVG_NATIVE_PROGRAM_AUTO"))
        fprintf(stderr,"compiled program descriptors: created=%u destroyed=%u outstanding=%u\n",
            created,destroyed,created-destroyed);
}
