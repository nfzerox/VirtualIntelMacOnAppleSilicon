/* Read-only chooser oracle for the matching Mavericks x64 framework.
 * Calls its REAL request parser/scorer on REAL GLI-owned records. No modified
 * framework, invented score, debugger, rendering, or production instrumentation.
 * Private offsets are UUID/prologue guarded and used only by this diagnostic. */
#include <OpenGL/OpenGL.h>
#include <dlfcn.h>
#include <mach-o/loader.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
struct Format {
    struct Format *next;
    uint32_t renderer,flags,buffers,color,accum,depth,stencil;
    uint16_t aux_depth_stencil,aux,samples,sample_buffers;
    uint32_t sample_modes,sample_alpha,display_mask;
};
_Static_assert(sizeof(struct Format)==56,"measured Mavericks x64 GLI record");
static int matching_image(const void *header)
{
    static const unsigned char uuid[]={0x4f,0xae,0xad,0xd8,0xee,0xb3,0x3f,0xd9,0xad,0xc6,0xba,0x65,0x80,0x62,0x28,0xcc};
    const struct mach_header_64 *h=header;if(h->magic!=MH_MAGIC_64)return 0;
    const unsigned char *p=(const void *)(h+1),*end=p+h->sizeofcmds;
    for(uint32_t i=0;i<h->ncmds;++i){
        const struct load_command *c=(const void *)p;
        if(p>end-sizeof(*c)||c->cmdsize<sizeof(*c)||c->cmdsize>(size_t)(end-p))return 0;
        if(c->cmd==LC_UUID)return c->cmdsize==sizeof(struct uuid_command)&&
            !memcmp(((const struct uuid_command *)c)->uuid,uuid,16);
        p+=c->cmdsize;
    }
    return 0;
}
int main(int argc,char **argv)
{
    int automatic=argc==4&&!strcmp(argv[3],"--automatic");
    if(argc!=3&&!automatic)return 2;
    CGLPixelFormatAttribute public_attrs[]={kCGLPFADoubleBuffer,kCGLPFAWindow,
        kCGLPFAColorSize,24,kCGLPFAAlphaSize,8,kCGLPFADepthSize,16,kCGLPFAStencilSize,1,
        automatic?kCGLPFASupportsAutomaticGraphicsSwitching:0,0};
    CGLPixelFormatObj format=NULL;GLint count=0;
    CGLError e=CGLChoosePixelFormat(public_attrs,&format,&count);
    printf("actual public choose=%d screens=%d\n",e,count);if(e||!format)return 2;
    for(int i=0;i<count;++i){GLint id=0;CGLDescribePixelFormat(format,i,kCGLPFARendererID,&id);printf("actual screen=%d id=%x\n",i,id);}
    void *library=dlopen("/System/Library/Frameworks/OpenGL.framework/OpenGL",RTLD_LAZY|RTLD_FIRST);
    unsigned char *choose=library?dlsym(library,"CGLChoosePixelFormat"):NULL;Dl_info info;
    if(!choose||!dladdr(choose,&info)||!matching_image(info.dli_fbase))return 2;
    unsigned char *parser=choose+0x2eb8,*scorer=choose+0x3348;
    static const unsigned char parse_bytes[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x50};
    static const unsigned char score_bytes[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x48};
    if(memcmp(parser,parse_bytes,sizeof(parse_bytes))||memcmp(scorer,score_bytes,sizeof(score_bytes)))return 2;
    int (*parse)(const CGLPixelFormatAttribute *,unsigned,void *,void *,unsigned char)=(void *)parser;
    int (*score)(const void *,const void *,const struct Format *,int,unsigned)=(void *)scorer;
    struct Format request={0};uint64_t values[2]={0};
    e=parse(public_attrs,0x10,&request,values,0);printf("real parser=%d flags=%x\n",e,request.flags);if(e)return 2;
    /* The real chooser's recovery pass clears renderer/request-only bits and
     * requires software bit 0x80 (13F1911 assembly at 0x9dce). */
    request.renderer=0;request.flags=(request.flags&0xfffefe7bU)|0x80;
    for(int engine=1;engine<3;++engine){
        library=dlopen(argv[engine],RTLD_NOW|RTLD_LOCAL);if(!library){puts(dlerror());return 2;}
        CGLError (*raw_choose)(struct Format **,const GLint *)=dlsym(library,"gliChoosePixelFormat");
        CGLError (*destroy)(struct Format *)=dlsym(library,"gliDestroyPixelFormat");
        if(!raw_choose||!destroy)return 2;
        const GLint raw[]={kCGLPFADoubleBuffer,kCGLPFAWindow,kCGLPFAColorSize,24,
            kCGLPFAAlphaSize,8,kCGLPFADepthSize,16,kCGLPFAStencilSize,1,4,0};
        struct Format *head=NULL;e=raw_choose(&head,raw);printf("engine=%d choose=%d\n",engine,e);if(e)return 2;
        unsigned seen=0;
        for(struct Format *p=head;p&&seen++<128;p=p->next)
            printf("engine=%d candidate=%x flags=%x buffers=%x color=%x depth=%x stencil=%x mask=%x REAL_SCORE=%d\n",
                engine,p->renderer,p->flags,p->buffers,p->color,p->depth,p->stencil,p->display_mask,
                score(&request,values,p,32,0x10));
        destroy(head);
    }
    CGLDestroyPixelFormat(format);return 0;
}
