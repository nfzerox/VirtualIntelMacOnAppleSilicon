/* Isolated native Tiger candidate; no system engine replacement. Exercises
 * its runtime-audited table and our owned 2D transport, not a software CGL
 * context or a different private-dispatch layout. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gliDispatch.h>
#include <IOKit/IOKitLib.h>
#include <dlfcn.h>
#include <mach-o/loader.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#ifndef VIM_TIGER_RUNTIME_SLOTS
#error Supply the independently audited runtime dispatch count
#endif

static int native_image(void)
{
    void *library=dlopen("/System/Library/Frameworks/OpenGL.framework/OpenGL",RTLD_LAZY|RTLD_LOCAL);
    void *function=library?dlsym(library,"CGLCreateContext"):NULL;
    Dl_info info={0};
    if(!function||!dladdr(function,&info)){puts("native image lookup failed");return 0;}
    const struct mach_header *header=info.dli_fbase;
    printf("native function=%p image=%s base=%p magic=%x commands=%u bytes=%u\n",function,
        info.dli_fname?info.dli_fname:"<none>",info.dli_fbase,header->magic,header->ncmds,header->sizeofcmds);
    if(header->magic!=MH_MAGIC||header->ncmds>256||header->sizeofcmds>65536)return 0;
    const char *at=(const char *)(header+1),*end=at+header->sizeofcmds;
    for(unsigned i=0;i<header->ncmds;++i){
        const struct load_command *command=(const void *)at;
        if(end-at<(int)sizeof(*command)||command->cmdsize<sizeof(*command)||command->cmdsize>(unsigned)(end-at))return 0;
        if(command->cmd==LC_SEGMENT&&command->cmdsize>=sizeof(struct segment_command)){
            const struct segment_command *segment=(const void *)at;
            printf("segment=%.*s addr=%x file=%x sections=%u\n",16,segment->segname,segment->vmaddr,segment->fileoff,segment->nsects);
            if(segment->nsects>(command->cmdsize-sizeof(*segment))/sizeof(struct section))return 0;
            const struct section *section=(const void *)(segment+1);
            for(unsigned n=0;n<segment->nsects;++n)if(!strncmp(section[n].sectname,SECT_TEXT,16))
                printf("textsection=%.*s addr=%x bytes=%x\n",16,section[n].sectname,section[n].addr,section[n].size);
        }
        at+=command->cmdsize;
    }
    /* ABI control only: the stock software context proves table size, not
     * hardware acceleration. The candidate's own draw below is separate. */
    CGLError (*choose)(const CGLPixelFormatAttribute *,CGLPixelFormatObj *,GLint *)=dlsym(library,"CGLChoosePixelFormat");
    CGLError (*create)(CGLPixelFormatObj,CGLContextObj,CGLContextObj *)=dlsym(library,"CGLCreateContext");
    CGLError (*parameter)(CGLContextObj,CGLContextParameter,GLint *)=dlsym(library,"CGLGetParameter");
    CGLError (*destroy)(CGLContextObj)=dlsym(library,"CGLDestroyContext");
    CGLError (*destroy_format)(CGLPixelFormatObj)=dlsym(library,"CGLDestroyPixelFormat");
    if(!choose||!create||!parameter||!destroy||!destroy_format)return 0;
    CGLPixelFormatAttribute attrs[]={0};CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;
    GLint count=0,bytes=0;CGLError error=choose(attrs,&format,&count);
    if(!error&&format)error=create(format,NULL,&context);
    if(!error&&context)error=parameter(context,kCGLCPDispatchTableSize,&bytes);
    printf("stock ABI context_error=%d table_bytes=%d candidate_bytes=%u\n",(int)error,(int)bytes,(unsigned)sizeof(GLIFunctionDispatch));
    if(context)destroy(context);if(format)destroy_format(format);
    return !error&&(bytes==686*4||bytes==759*4)&&bytes<=(GLint)sizeof(GLIFunctionDispatch);
}

static GLIFunctionDispatch *NativeTable(size_t bytes,void **mapping)
{
    int page=getpagesize();if(page<=0||!bytes||bytes>(size_t)page)return NULL;
    *mapping=mmap(NULL,(size_t)page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    if(*mapping==MAP_FAILED){*mapping=NULL;return NULL;}
    if(mprotect((char *)*mapping+page,(size_t)page,PROT_NONE)){
        munmap(*mapping,(size_t)page*2);*mapping=NULL;return NULL;
    }
    // An overlong759-slot publication into the native686-slot caller faults
    // at the first invalid byte, instead of silently corrupting a heap record.
    GLIFunctionDispatch *table=(void *)((char *)*mapping+page-bytes);
    memset(table,0,bytes);return table;
}

int main(int argc,char **argv)
{
    setvbuf(stdout,NULL,_IOLBF,0);
    if(argc!=2 || sizeof(GLIFunctionDispatch)!=VIM_TIGER_RUNTIME_SLOTS*4)return 64;
    /* ABI inspection creates a public CGL context and therefore loads the
     * installed renderer, including its guarded own-process corrections.
     * Do that in a SEPARATE invocation: loading an isolated second candidate
     * afterwards makes it rightly reject code already corrected by the first
     * image. That is test contamination, not candidate transport failure. */
    if(!strcmp(argv[1],"--native-abi"))return native_image()?0:1;
    void *engine=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    if(!engine){fprintf(stderr,"candidate load: %s\n",dlerror());return 1;}
    void (*initialize)(void *,void *,unsigned,void *,void *,void *)=dlsym(engine,"gliInitializeLibrary");
    void (*terminate)(void)=dlsym(engine,"gliTerminateLibrary");
    unsigned char (*version)(GLint *,GLint *,GLint *)=dlsym(engine,"gliGetVersion");
    int (*choose)(void **,const GLint *)=dlsym(engine,"gliChoosePixelFormat");
    int (*create)(GLIContext *,void *,GLIContext,GLIFunctionDispatch *,void *,unsigned)=dlsym(engine,"gliCreateContext");
    int (*attach)(GLIContext,unsigned,const void *,unsigned)=dlsym(engine,"gliAttachDrawable");
    int (*destroy)(GLIContext)=dlsym(engine,"gliDestroyContext");
    int (*destroy_format)(void *)=dlsym(engine,"gliDestroyPixelFormat");
    int (*integer)(GLIContext,unsigned,GLint *)=dlsym(engine,"gliGetInteger");
    if(!initialize||!terminate||!version||!choose||!create||!attach||!destroy||!destroy_format||!integer)return 1;
    io_service_t service=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("VMsvga2Accel"));
    if(!service){fputs("candidate accelerator absent\n",stderr);return 77;}
    initialize(&service,NULL,1,NULL,NULL,NULL);IOObjectRelease(service);
    GLint major=0,minor=0,identity=0;
    int ready=version(&major,&minor,&identity);
    printf("candidate version_ready=%d major=%d minor=%d engine=%x\n",ready,(int)major,(int)minor,(unsigned)identity);
    if(!ready)return 1;
    const GLint attrs[]={4,kCGLPFANoRecovery,kCGLPFAColorSize,24,kCGLPFAAlphaSize,8,0};
    void *format=NULL;GLIContext context=NULL;
    GLint bytes=0;
    if(integer(NULL,kCGLCPDispatchTableSize,&bytes)||(bytes!=686*4&&bytes!=759*4)||
       bytes>(GLint)sizeof(GLIFunctionDispatch))return 1;
    printf("native table_bytes=%d capacity=%u\n",(int)bytes,(unsigned)sizeof(GLIFunctionDispatch));
    void *primaryMemory=NULL,*secondaryMemory=NULL;
    GLIFunctionDispatch *dispatch=NativeTable((size_t)bytes,&primaryMemory),
        *fallback=NativeTable((size_t)bytes,&secondaryMemory);
    if(!dispatch||!fallback)return 1;
    int result=1,error=choose(&format,attrs);printf("choose=%d\n",error);
    if(error||!format)goto done;
    error=create(&context,format,NULL,dispatch,fallback,0);printf("create=%d\n",error);
    if(error||!context)goto done;
    for(int i=0;i<bytes;i++)if(((unsigned char *)fallback)[i])goto done;
    unsigned char backing[32*32*4]={0},pixels[sizeof(backing)]={0};
    struct {GLint width,height,row,reserved;void *pixels;} offscreen={32,32,128,0,backing};
    error=attach(context,kCGLPFAOffScreen,&offscreen,0);printf("attach=%d\n",error);
    if(error||!dispatch->get_string||!dispatch->clear_color||!dispatch->clear||
       !dispatch->finish||!dispatch->read_pixels||!dispatch->get_error)goto done;
    const char *renderer=(const char *)dispatch->get_string(context,GL_RENDERER);
    printf("renderer=%s\n",renderer?renderer:"<none>");
    if(!renderer || strstr(renderer,"Software"))goto done;
    dispatch->clear_color(context,.25f,.5f,.75f,1.f);
    dispatch->clear(context,GL_COLOR_BUFFER_BIT);dispatch->finish(context);
    dispatch->read_pixels(context,0,0,32,32,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    GLenum glerror=dispatch->get_error(context);
    printf("readback=%u,%u,%u,%u glerror=%x\n",pixels[0],pixels[1],pixels[2],pixels[3],(unsigned)glerror);
    if(glerror)goto done;
    for(unsigned i=0;i<sizeof(pixels);i+=4)
        if(abs((int)pixels[i]-64)>1 || abs((int)pixels[i+1]-128)>1 ||
           abs((int)pixels[i+2]-191)>1 || pixels[i+3]!=255)goto done;
    result=0;
done:
    if(context&&destroy(context))result=1;
    if(format&&destroy_format(format))result=1;
    munmap(primaryMemory,(size_t)getpagesize()*2);munmap(secondaryMemory,(size_t)getpagesize()*2);
    terminate();dlclose(engine);
    if(!result)puts("PASS Tiger candidate native dispatch, hardware transport, offscreen draw/readback and teardown");
    return result;
}
