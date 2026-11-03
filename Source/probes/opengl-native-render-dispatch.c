/* Physical Mavericks Intel, own context only. Resolve the EXACT native
 * GLEngine FlushRender callback identified in matching assembly, never a
 * Float-derived dispatch or another task's memory. Guard the loaded UUID
 * before reading private offsets; do not invoke the resolved callbacks. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/CGLContext.h>
#include <dlfcn.h>
#include <mach-o/loader.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static int native_engine(void *symbol)
{
    static const unsigned char expected[]={0x30,0x64,0x3c,0x97,0x7e,0xbb,0x36,0xee,
        0xbe,0xea,0x13,0x84,0x4e,0xa0,0xf6,0x80};
    Dl_info info={NULL,NULL,NULL,NULL};if(!dladdr(symbol,&info)||!info.dli_fbase)return 0;
    const struct mach_header_64 *header=info.dli_fbase;if(header->magic!=MH_MAGIC_64)return 0;
    const unsigned char *at=(const void *)(header+1),*limit=at+header->sizeofcmds;
    for(unsigned i=0;i<header->ncmds;++i){
        if(at>limit || (size_t)(limit-at)<sizeof(struct load_command))return 0;
        const struct load_command *command=(const void *)at;
        if(command->cmdsize<sizeof(*command) || command->cmdsize>(size_t)(limit-at))return 0;
        if(command->cmd==LC_UUID && command->cmdsize==sizeof(struct uuid_command))
            return !memcmp(((const struct uuid_command *)command)->uuid,expected,sizeof(expected));
        at+=command->cmdsize;
    }
    return 0;
}
static int describe(const char *label,const void *callback)
{
    Dl_info info={NULL,NULL,NULL,NULL};
    if(!callback || !dladdr(callback,&info)||!info.dli_fbase||!info.dli_fname)return 0;
    printf("%s image=%s symbol=%s image_offset=%llx\n",label,info.dli_fname,
        info.dli_sname?info.dli_sname:"<local>",
        (unsigned long long)((uintptr_t)callback-(uintptr_t)info.dli_fbase));
    return 1;
}
int main(void)
{
    CGLPixelFormatAttribute a[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFASupportsAutomaticGraphicsSwitching,kCGLPFAOpenGLProfile,
        (CGLPixelFormatAttribute)kCGLOGLPVersion_Legacy,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint count=0;
    int result=1;
    if(CGLChoosePixelFormat(a,&format,&count)||!format||CGLCreateContext(format,NULL,&context)||
       !context||CGLSetCurrentContext(context))goto done;
    const char *renderer=(const char *)glGetString(GL_RENDERER);
    printf("renderer=%s\n",renderer?renderer:"<none>");
    if(!renderer || !strstr(renderer,"Intel"))goto done;
    void *engine=dlopen("/System/Library/Frameworks/OpenGL.framework/Resources/GLEngine.bundle/GLEngine",RTLD_NOW|RTLD_LOCAL);
    void *create=engine?dlsym(engine,"gliCreateContext"):NULL;
    if(!create || !native_engine(create)){fputs("REFUSED nonmatching native engine\n",stderr);goto done;}
    const unsigned char *gli=(const void *)context->rend;
    const void *const *dispatch=*(const void *const *const *)(gli+0xd550);
    if(!dispatch)goto done;
    if(!describe("present-slot-e8",dispatch[0xe8/sizeof(void *)]) ||
       !describe("flush-slot-f0",dispatch[0xf0/sizeof(void *)]))goto done;
    /* Matching Mercury gldFlushContext loads object+8's table, then calls
     * table+0x1c8 with mode2. Resolve, but never invoke, that function. */
    const unsigned char *driver=*(const unsigned char *const *)(gli+0xd530);
    const void *const *driver_dispatch=driver?*(const void *const *const *)(driver+8):NULL;
    if(!driver_dispatch || !describe("driver-submit-slot-1c8",driver_dispatch[0x1c8/sizeof(void *)]))goto done;
    /* Matching GLEngine FinishRender loops these renderer engines and calls
     * each table+0x220. An inactive engine count is reported, not invented. */
    unsigned engines=gli[0xe714];printf("finish-render-engines=%u\n",engines);
    if(engines>8)goto done;
    for(unsigned i=0;i<engines;++i){
        const void *const *table=*(const void *const *const *)(gli+0xf218+i*0xb10);
        char label[64];snprintf(label,sizeof(label),"finish-engine-%u-slot-220",i);
        if(!table || !describe(label,table[0x220/sizeof(void *)]))goto done;
    }
    result=0;
done:
    CGLSetCurrentContext(NULL);if(context)CGLDestroyContext(context);if(format)CGLDestroyPixelFormat(format);
    return result;
}
