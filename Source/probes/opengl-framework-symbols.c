/* Public framework lookup used by period GLEW and plug-ins; no injection. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <dlfcn.h>
#include <stdio.h>

int main(void) {
    CGLPixelFormatAttribute attributes[]={kCGLPFAAccelerated,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attributes,&format,&count)||!format)return 77;
    if(CGLCreateContext(format,NULL,&context)||CGLSetCurrentContext(context))return 2;
    CGLDestroyPixelFormat(format);
    printf("version=%s renderer=%s\n",glGetString(GL_VERSION),glGetString(GL_RENDERER));
    const char *paths[]={"/System/Library/Frameworks/OpenGL.framework/Versions/Current/OpenGL",
        "/System/Library/Frameworks/OpenGL.framework/Versions/A/OpenGL",
        "/System/Library/Frameworks/OpenGL.framework/OpenGL"};
    const char *names[]={"glTexImage3D","glTexSubImage3D","glCopyTexSubImage3D",
        "glDrawRangeElements","glActiveTexture","glCreateShader"};
    int missing=0;
    for(unsigned i=0;i<sizeof(paths)/sizeof(paths[0]);++i) {
        dlerror();void *library=dlopen(paths[i],RTLD_LAZY);
        printf("path=%s opened=%d error=%s\n",paths[i],library!=NULL,library?"none":dlerror());
        if(!library){++missing;continue;}
        for(unsigned n=0;n<sizeof(names)/sizeof(names[0]);++n) {
            dlerror();void *function=dlsym(library,names[n]);
            printf("  %s found=%d error=%s\n",names[n],function!=NULL,function?"none":dlerror());
            if(!function)++missing;
        }
        dlclose(library);
    }
    glTexImage3D(GL_PROXY_TEXTURE_3D,0,GL_RGBA8,1,1,1,0,GL_RGBA,GL_FLOAT,NULL);
    GLint width=0;glGetTexLevelParameteriv(GL_PROXY_TEXTURE_3D,0,GL_TEXTURE_WIDTH,&width);
    printf("linked TexImage3D proxy width=%d error=%x\n",width,glGetError());
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);
    return missing?1:0;
}
