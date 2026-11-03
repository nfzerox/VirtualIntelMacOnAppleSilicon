/* Inspect the real GLI records after native CGL has initialized GLEngine.
 * No system replacement or injection. Run against the target OS itself. */
#include <OpenGL/OpenGL.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void dump(const char *kind,void *p,size_t length)
{
    unsigned count=0;
    while(p && count++<8) {
        printf("%s %u at %p\n",kind,count,p);
        for(size_t i=0;i<length;i+=4) {
            uint32_t v; memcpy(&v,(char *)p+i,4);
            printf(" %02zx:%08x",i,v);
            if(i%16==12) puts("");
        }
        puts(""); memcpy(&p,p,sizeof(p));
    }
}
int main(int argc,char **argv)
{
    int compositor=argc>1&&!strcmp(argv[1],"--compositor");
    CGLPixelFormatAttribute attrs[]={kCGLPFAColorSize,24,kCGLPFAAlphaSize,8,
        kCGLPFADepthSize,24,kCGLPFAStencilSize,8,0};
    CGLPixelFormatObj pf=NULL; GLint count=0;
    CGLError e=CGLChoosePixelFormat(attrs,&pf,&count);
    printf("native CGL init error=%d formats=%d\n",e,count);
    if(e || !pf) return 1;
    int image_arg=1+compositor;
    void *lib=dlopen(argc>image_arg ? argv[image_arg] : "/System/Library/Frameworks/OpenGL.framework/Resources/GLEngine.bundle/GLEngine",RTLD_NOW|RTLD_LOCAL);
    if(!lib) { puts(dlerror()); return 1; }
    int (*choose)(void **,const int *)=dlsym(lib,"gliChoosePixelFormat");
    int (*destroy)(void *)=dlsym(lib,"gliDestroyPixelFormat");
    int (*query)(void **,uint32_t)=dlsym(lib,"gliQueryRendererInfo");
    int (*destroyInfo)(void *)=dlsym(lib,"gliDestroyRendererInfo");
    if(!choose || !destroy || !query || !destroyInfo) return 1;
    void *record=NULL;
    int raw[]={4,8,24,11,8,12,24,13,8,0};
    /* Exact native 9J3050 CoreGraphics compositor request at TEXT 0x67db00.
     * Internal component selectors are not public CGL attribute enums. */
    int server[]={80,20,8,21,8,22,8,12,0,13,0,0};
    int chosen=choose(&record,compositor?server:raw);
    printf("GLI choose=%d compositor=%d\n",chosen,compositor); dump("pixel-format",record,48+sizeof(void *));
    if(record) destroy(record);
    record=NULL;
    printf("GLI query=%d\n",query(&record,~0U)); dump("renderer",record,80);
    if(record) destroyInfo(record);
    CGLDestroyPixelFormat(pf); return chosen?2:0;
}
