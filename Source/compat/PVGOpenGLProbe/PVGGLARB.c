/* Research-only ARB assembly IR companion for native Mojave GLEngine. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>

void *PVGGLLegacyParseOriginal;
static pthread_once_t parserOnce=PTHREAD_ONCE_INIT;
static void *(*parserCreate)(unsigned);
static void (*parserInit)(void *),(*parserFree)(void *);
static void (*parserString)(void *,const char *,unsigned);
static void (*parserAttach)(void *,void **,void **,void **);
static void (*parserLimits)(void *,const unsigned *);
static void (*parserPrefs)(void *,unsigned);
static int (*parserParse)(void *);
static void resolveParser(void) {
    const char *path=getenv("PVG_NATIVE_ARB_PARSER");
    void *h=path?dlopen(path,RTLD_NOW|RTLD_LOCAL):NULL;
    if(!h){fprintf(stderr,"PVG GL: ARB donor parser unavailable\n");abort();}
#define GET(field,name) do { *(void **)(&field)=dlsym(h,name);if(!field)abort(); } while(0)
    GET(parserCreate,"PPParserCreate");GET(parserInit,"PPParserInit");
    GET(parserFree,"PPParserFree");GET(parserString,"PPParserAttachString");
    GET(parserAttach,"PPParserAttachLLVMIR");GET(parserLimits,"PPParserSetLimits");
    GET(parserPrefs,"PPParserSetPrefs");GET(parserParse,"PPParserParse");
#undef GET
}
extern int PVGGLConfigOwnsNative(void *);
void PVGGLLegacyParse(void *context,void *program,unsigned stage) {
    void (*original)(void *,void *,unsigned)=PVGGLLegacyParseOriginal;
    void *config;memcpy(&config,(uint8_t *)context+0xb0b8,8);
    if(!PVGGLConfigOwnsNative(config)){original(context,program,stage);return;}
    if(stage!=0&&stage!=4)abort();
    /* Native destroy frees these same three fields. Native parse predates
     * the second IR pass, so retire previous outputs here on recompile. */
    const unsigned offsets[]={0x1b8,0x210,0x68};
    for(unsigned i=0;i<3;++i) {
        void *old;memcpy(&old,(uint8_t *)program+offsets[i],8);free(old);
        memset((uint8_t *)program+offsets[i],0,8);
    }
    original(context,program,stage);
    int errorPosition;memcpy(&errorPosition,(uint8_t *)context+0x9330,4);
    if(errorPosition!=-1)return;
    pthread_once(&parserOnce,resolveParser);
    /* Catalina glePPParse constants; SetLimits copies exactly 64 bytes. */
    const unsigned limits[2][16]={
        {256,1024,8,262144,65535,1024,32,2,8,128,4,1,6,8,16,0},
        {128,1024,8,262144,65535,1024,64,0,8,128,4,1,6,8,0,8}
    };
    void *parser=parserCreate(stage?2:0),*prefsObject;
    const char *source;unsigned short prefs;
    memcpy(&source,(uint8_t *)program+0x80,8);
    memcpy(&prefsObject,(uint8_t *)context+0x5a30,8);
    memcpy(&prefs,(uint8_t *)prefsObject+0x19a,2);
    if(!parser||!source)abort();
    parserInit(parser);parserPrefs(parser,prefs);parserLimits(parser,limits[stage==4]);
    parserString(parser,source,1);
    void *ir=NULL,*extra=NULL,*reflection=NULL;
    parserAttach(parser,&ir,&extra,&reflection);
    int result=parserParse(parser);
    parserAttach(parser,NULL,NULL,NULL);parserFree(parser);
    if(result||!ir||!reflection) {
        free(ir);free(extra);free(reflection);
        fprintf(stderr,"PVG GL: ARB IR generation failed stage=%u result=%d\n",stage,result);
        abort();
    }
    memcpy((uint8_t *)program+0x1b8,&ir,8);
    memcpy((uint8_t *)program+0x210,&extra,8);
    memcpy((uint8_t *)program+0x68,&reflection,8);
}
