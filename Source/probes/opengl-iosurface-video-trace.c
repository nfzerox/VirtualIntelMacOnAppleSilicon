/* Finite process-only observation. Load into the selected video player using
 * DYLD_FORCE_FLAT_NAMESPACE=1 and DYLD_INSERT_LIBRARIES. Preserve every native
 * return and GL error; never install globally or change a rendering decision. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/CGLIOSurface.h>
#include <IOSurface/IOSurface.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
static unsigned parameter_records,surface_records,packed_records;
static void *Original(const char *name) {
    void *function=dlsym(RTLD_NEXT,name);
    if(!function){fprintf(stderr,"VIM VIDEO missing original %s\n",name);abort();}
    return function;
}
CGLError CGLSetParameter(CGLContextObj context,CGLContextParameter key,const GLint *value) {
    static CGLError (*original)(CGLContextObj,CGLContextParameter,const GLint *);
    if(!original)original=Original("CGLSetParameter");
    int record=(GLint)key==0x38e&&value&&__sync_fetch_and_add(&parameter_records,1)<64;
    if(record)fprintf(stderr,"VIM VIDEO IOSurface parameter ctx=%p id=%u target=%x internal=%x size=%d,%d format=%x type=%x plane=%u caller=%p\n",
        (void *)context,(unsigned)value[0],(unsigned)value[1],(unsigned)value[2],value[3],value[4],
        (unsigned)value[5],(unsigned)value[6],(unsigned)value[7],__builtin_return_address(0));
    CGLError result=original(context,key,value);
    if(record)fprintf(stderr,"VIM VIDEO IOSurface parameter result=%d\n",result);
    return result;
}
CGLError CGLTexImageIOSurface2D(CGLContextObj context,GLenum target,GLenum internal,
    GLsizei width,GLsizei height,GLenum format,GLenum type,IOSurfaceRef surface,GLuint plane) {
    static CGLError (*original)(CGLContextObj,GLenum,GLenum,GLsizei,GLsizei,GLenum,GLenum,IOSurfaceRef,GLuint);
    if(!original)original=Original("CGLTexImageIOSurface2D");
    int record=__sync_fetch_and_add(&surface_records,1)<64;
    if(record){
        fprintf(stderr,"VIM VIDEO IOSurface public ctx=%p target=%x internal=%x size=%d,%d format=%x type=%x plane=%u caller=%p",
            (void *)context,target,internal,width,height,format,type,plane,__builtin_return_address(0));
        if(surface)fprintf(stderr," id=%u fourcc=%08x planes=%zu plane-size=%zu,%zu bpe=%zu row=%zu",
            IOSurfaceGetID(surface),(unsigned)IOSurfaceGetPixelFormat(surface),IOSurfaceGetPlaneCount(surface),
            IOSurfaceGetWidthOfPlane(surface,plane),IOSurfaceGetHeightOfPlane(surface,plane),
            IOSurfaceGetBytesPerElementOfPlane(surface,plane),IOSurfaceGetBytesPerRowOfPlane(surface,plane));
        fputc('\n',stderr);
    }
    CGLError result=original(context,target,internal,width,height,format,type,surface,plane);
    if(record)fprintf(stderr,"VIM VIDEO IOSurface public result=%d\n",result);
    return result;
}
void glTexImage2D(GLenum target,GLint level,GLenum internal,GLsizei width,GLsizei height,GLint border,GLenum format,GLenum type,const GLvoid *pixels) {
    static void (*original)(GLenum,GLint,GLenum,GLsizei,GLsizei,GLint,GLenum,GLenum,const GLvoid *);
    if(!original)original=Original("glTexImage2D");
    if((format==0x85b9||format==0x8a1f)&&__sync_fetch_and_add(&packed_records,1)<32)
        fprintf(stderr,"VIM VIDEO packed upload target=%x internal=%x size=%d,%d format=%x type=%x caller=%p\n",
            target,(unsigned)internal,width,height,format,type,__builtin_return_address(0));
    original(target,level,internal,width,height,border,format,type,pixels);
}
__attribute__((constructor)) static void Loaded(void) {
    fputs("VIM VIDEO finite IOSurface trace loaded; original results preserved\n",stderr);
}
