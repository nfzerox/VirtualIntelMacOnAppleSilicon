/* Explicitly load a candidate engine in an isolated physical-reference process.
 * No system replacement, environment injection or guest transport is required.
 * Hold Intel using opengl-physical-intel-mode for the entire invocation. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/CGLRenderers.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

int main(int argc,char **argv)
{
    int versionOnly=argc==3&&!strcmp(argv[1],"--version-only");
    if(argc!=2&&!versionOnly)return 64;
    const char *path=argv[versionOnly?2:1];
    CGLContextObj context=NULL;
    if(!versionOnly){
    CGLRendererInfoObj information=NULL;GLint renderers=0,selected=0;
    if(CGLQueryRendererInfo(~0u,&information,&renderers))return 1;
    for(GLint index=0;index<renderers;++index){
        GLint online=0,hardware=0,renderer=0;
        CGLDescribeRenderer(information,index,kCGLRPOnline,&online);
        CGLDescribeRenderer(information,index,kCGLRPAccelerated,&hardware);
        CGLDescribeRenderer(information,index,kCGLRPRendererID,&renderer);
        // Public CGLRenderers.h families: Intel HD 3000 and HD 5000, the two
        // supplied physical references. The 10.6 SDK predates their macros.
        // Never create an AMD/NVIDIA context and only reject it afterwards.
        GLint family=renderer&kCGLRendererIDMatchingMask;
        if(online&&hardware&&(family==0x24300||family==0x24500)){
            if(selected&&selected!=renderer){CGLDestroyRendererInfo(information);return 77;}
            selected=renderer;
        }
    }
    CGLDestroyRendererInfo(information);if(!selected)return 77;
    /* A generic accelerated format can still request the offline discrete
     * driver on Lion. Select the actual online renderer before creating any
     * context; checking GL_VENDOR only afterwards is too late. */
    CGLPixelFormatAttribute attributes[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFARendererID,(CGLPixelFormatAttribute)selected,0};
    CGLPixelFormatObj format=NULL;GLint count=0;
    CGLError error=CGLChoosePixelFormat(attributes,&format,&count);
    if(error||!format)return 1;
    error=CGLCreateContext(format,NULL,&context);CGLDestroyPixelFormat(format);
    if(error||!context||CGLSetCurrentContext(context))return 2;
    const char *vendor=(const char *)glGetString(GL_VENDOR);
    if(!vendor||!strstr(vendor,"Intel")){CGLDestroyContext(context);return 77;}
    GLint bytes=0;error=CGLGetParameter(context,kCGLCPDispatchTableSize,&bytes);
    printf("Intel native dispatch: error=%d bytes=%d pointer-bytes=%zu\n",error,bytes,sizeof(void *));
    }else puts("Version-only load/compatibility check; no rendering qualification.");
    void *library=dlopen(path,RTLD_NOW|RTLD_LOCAL);
    if(!library){puts(dlerror());if(context)CGLDestroyContext(context);return 3;}
    unsigned char (*version)(GLint *,GLint *,GLint *)=dlsym(library,"gliGetVersion");
    GLint major=0,minor=0,engine=0;
    int accepted=version&&version(&major,&minor,&engine);
    printf("candidate version accepted=%d ABI=%d.%d engine=%x\n",accepted,major,minor,engine);
    if(context){CGLSetCurrentContext(NULL);CGLDestroyContext(context);}dlclose(library);
    return accepted?0:4;
}
