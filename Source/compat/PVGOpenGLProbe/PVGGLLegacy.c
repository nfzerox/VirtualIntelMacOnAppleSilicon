/* Opt-in Mojave legacy generation adapter. Entry points are installed only
 * after native symbol/prologue validation in PVGGLConfigHooks.c. */
#include "PVGGLCompanion.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <execinfo.h>

void *PVGGLLegacyOriginal[6];
void *PVGGLLegacyPP[2];
void (*PVGGLLegacyRelease)(void *);
void *PVGGLLegacyBuffersOriginal;
void *PVGGLLegacyListOriginal;
void *PVGGLLegacyListDrawOriginal;
_Thread_local void *PVGGLLegacyListContext;
void PVGGLLegacyList(void *context,void *list) {
    void *saved=PVGGLLegacyListContext;
    PVGGLLegacyListContext=context;
    ((void (*)(void *,void *))PVGGLLegacyListOriginal)(context,list);
    PVGGLLegacyListContext=saved;
}
void PVGGLLegacyListDraw(void *context,void *list) {
    void *saved=PVGGLLegacyListContext;
    PVGGLLegacyListContext=context;
    ((void (*)(void *,void *))PVGGLLegacyListDrawOriginal)(context,list);
    PVGGLLegacyListContext=saved;
}
extern int PVGGLConfigOwnsNative(void *);
static _Thread_local unsigned active;

void PVGGLLegacyBuffers(void *context) {
    void *config;
    memcpy(&config,(uint8_t *)context+0xb0b8,sizeof(config));
    void (*original)(void *)=PVGGLLegacyBuffersOriginal;
    if(!PVGGLConfigOwnsNative(config)) { original(context);return; }
    /* Native reflection still uses Mojave's parameter format even when the
     * generated AIR is modern. Keep native packing, dirty-bit selection,
     * allocation and cleanup; enable only its mode-gated upload here. */
    uint8_t *mode=(uint8_t *)config+0x19,saved=*mode;
    *mode=(saved&0x80)|2;
    original(context);
    *mode=saved;
}

static void update(void *context,unsigned stage) {
    void *config;
    memcpy(&config,(uint8_t *)context+0xb0b8,sizeof(config));
    void (*original)(void *)=PVGGLLegacyOriginal[stage==4];
    if(!PVGGLConfigOwnsNative(config)) { original(context);return; }
    PVGGLCompanionScope scope;
    PVGGLCompanionEnter(&scope,context,stage,PVGGLLegacyRelease);
    uint8_t *mode=(uint8_t *)config+0x19, saved=*mode;
    *mode=(saved&0x80)|2;
    ++active;
    original(context);
    --active;
    *mode=saved;
    PVGGLCompanionLeave(&scope);
}
void PVGGLLegacyVertexUpdate(void *context) { update(context,0); }
void PVGGLLegacyFragmentUpdate(void *context) { update(context,4); }

static void *generatePP(void *key,unsigned stage) {
    uint64_t a=0,b=0;unsigned count=0;
    void *(*generate)(void *,uint64_t *,uint64_t *,unsigned *)=PVGGLLegacyPP[stage==4];
    void *pp=generate(key,&a,&b,&count);
    if(!pp)abort();
    return pp;
}
static void remember(void *ir,void *pp) {
    if(!ir) { PVGGLLegacyRelease(pp);return; }
    if(!PVGGLCompanionPut(ir,pp)) { PVGGLLegacyRelease(pp);abort(); }
}
void *PVGGLLegacyVertexIR(void *key,void *reflection,void *extra,
                         void *a,void *b,void *count) {
    void *(*original)(void *,void *,void *,void *,void *,void *)=PVGGLLegacyOriginal[2];
    void *pp=active?generatePP(key,0):NULL;
    void *ir=original(key,reflection,extra,a,b,count);
    if(pp)remember(ir,pp);
    return ir;
}
void *PVGGLLegacyFragmentIR(void *key,void *reflection,void *a,void *b,void *count) {
    void *(*original)(void *,void *,void *,void *,void *)=PVGGLLegacyOriginal[3];
    void *pp=active?generatePP(key,4):NULL;
    void *ir=original(key,reflection,a,b,count);
    if(pp)remember(ir,pp);
    return ir;
}
void *PVGGLLegacyCreate(void *context,unsigned stage,void *pp,void *ir,
                       uint64_t a,uint64_t b,unsigned count,uint64_t extra) {
    void *(*original)(void *,unsigned,void *,void *,uint64_t,uint64_t,unsigned,uint64_t)=PVGGLLegacyOriginal[4];
    void *companion=active?PVGGLCompanionTake(context,stage,ir):NULL;
    if(companion) { if(pp)abort();pp=companion; }
    void *program=original(context,stage,pp,ir,a,b,count,extra);
    if(!program && companion)PVGGLLegacyRelease(companion);
    return program;
}
void PVGGLLegacyRemap(void *context,void *program) {
    if(getenv("PVG_NATIVE_ARB_TRACE")) {
        void *ir,*pp,*source;
        memcpy(&ir,(uint8_t *)program+0x1b8,8);
        memcpy(&pp,(uint8_t *)program+0x1b0,8);
        memcpy(&source,(uint8_t *)program+0x80,8);
        fprintf(stderr,"PVG GL: remap stage=%u active=%u pp=%p ir=%p source=%p\n",
                ((uint8_t *)program)[0x1aa],active,pp,ir,source);
        if(pp&&!ir&&!active){void *frames[12];int n=backtrace(frames,12);backtrace_symbols_fd(frames,n,2);}
    }
    if(active) {
        uint8_t *p=program;void *ir,*old;
        memcpy(&ir,p+0x1b8,sizeof(ir));memcpy(&old,p+0x1b0,sizeof(old));
        void *pp=PVGGLCompanionTake(context,p[0x1aa],ir);
        if(pp) {
            if(old)abort();
            memcpy(p+0x1b0,&pp,sizeof(pp));
        }
    }
    ((void (*)(void *,void *))PVGGLLegacyOriginal[5])(context,program);
}
