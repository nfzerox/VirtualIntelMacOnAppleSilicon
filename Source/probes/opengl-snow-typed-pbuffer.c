/* Guest-owned qualification of the period x64 CGLS/IOKit pbuffer seam.
 * The48-byte record/0x2b mode are from matching Snow and native9J3050
 * cglsCreate/AttachPBuffer
 * disassembly, not a Float renderer. Never touches a display/foreign surface.
 * Public CGLSetPBuffer may select32-bit storage despite a64-bit request;
 * this fixture explicitly creates the same64-bit surface as the native server.
 * This is NOT a portable native-Intel CGLS context constructor: the public
 * CGL context lacks additional native server setup. Use the read-only live
 * native WindowServer probe for that comparison; refuse a non-VM provider.
 * --back-flush uses the 32-bit pbuffer with a double-buffered producer and
 * checks that flushing the selected back (then front) buffer reaches a
 * separate texture consumer without a swap.
 */
#include <OpenGL/OpenGL.h>
#include <OpenGL/CGLContext.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/graphics/IOAccelSurfaceConnect.h>
#include <dlfcn.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../vendor/source/VMsvga2/AC/UCMethods.h"
#include "../vendor/source/VMsvga2/vminclude/vim_framebuffer.h"
#if !defined(__x86_64__)
#error This native CGLS record has only been verified for Snow x64.
#endif
typedef CGLError (*Attach)(GLIContext,unsigned,const void *,unsigned,unsigned);
typedef CGLError (*Import)(GLIContext,unsigned,const GLint *);
struct SnowPBuffer {
    uint64_t reserved;uint32_t identifier,target,internal,level;
    void *surface;uint32_t accelerator,width,height;uint8_t mode,padding[3];
};
_Static_assert(sizeof(struct SnowPBuffer)==48 && offsetof(struct SnowPBuffer,surface)==24 &&
    offsetof(struct SnowPBuffer,width)==36,"actual10J3250/10K549 x64 CGLS record");
static int check(const char *phase,const GLushort expected[4])
{
    GLushort pixel[4]={0};glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_SHORT,pixel);
    GLenum error=glGetError();int good=!error;
    for(unsigned i=0;i<4;++i)good&=abs((int)pixel[i]-(int)expected[i])<=1;
    printf("%s pixel=%04x,%04x,%04x,%04x GL=%x %s\n",phase,pixel[0],pixel[1],pixel[2],pixel[3],error,good?"PASS":"FAIL");
    return good;
}
static int cpuWrite(io_service_t service,uint32_t identifier,int width,int height,const GLushort pixel[4])
{
    io_connect_t connection=0;IOReturn error=IOServiceOpen(service,mach_task_self(),2,&connection);
    uint64_t target[]={identifier,0x800};uint32_t words[11]={0};size_t bytes=sizeof(words);
    if(!error)error=IOConnectCallMethod(connection,kIOVM2DSetSurface,target,2,NULL,0,NULL,NULL,words,&bytes);
    struct VIMSurfaceMapping mapping={0};bytes=sizeof(mapping);int locked=0;
    if(!error){error=IOConnectCallStructMethod(connection,kIOVM2DLockSurfaceBacking,NULL,0,&mapping,&bytes);locked=!error;}
    if(!error && (bytes!=sizeof(mapping) || mapping.layout.version!=VIM_FRAMEBUFFER_LAYOUT_VERSION ||
       mapping.layout.bits_per_pixel!=64 || mapping.layout.width!=(unsigned)width || mapping.layout.height!=(unsigned)height ||
       mapping.layout.row_bytes<(uint64_t)width*8 || mapping.layout.offset>mapping.length ||
       (uint64_t)(height-1)*mapping.layout.row_bytes+(uint64_t)width*8>mapping.length-mapping.layout.offset))error=kIOReturnBadArgument;
    if(!error)for(int y=0;y<height;++y)for(int x=0;x<width;++x)
        memcpy((void *)(uintptr_t)(mapping.address+mapping.layout.offset+(uint64_t)y*mapping.layout.row_bytes+(unsigned)x*8),pixel,8);
    if(locked){uint64_t mode=error?VIM_SURFACE_UNLOCK_READ_ONLY:0;struct VIMSurfaceState state={0};bytes=sizeof(state);
        IOReturn unlock=IOConnectCallMethod(connection,kIOVM2DUnlockSurfaceBacking,&mode,1,NULL,0,NULL,NULL,&state,&bytes);
        if(!error)error=unlock;}
    if(connection)IOServiceClose(connection);printf("owned CPU write=%x\n",error);return !error;
}
int main(int argc,char **argv)
{
    enum {W=17,H=13};setvbuf(stdout,NULL,_IOLBF,0);
    int back=argc==2 && !strcmp(argv[1],"--back-flush");
    if(argc!=1&&!back)return 2;
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,back?kCGLPFADoubleBuffer:0,0};
    CGLPixelFormatAttribute consumerAttrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj format=NULL;CGLContextObj producer=NULL,consumer=NULL;GLint count=0;
    CGLPixelFormatObj consumerFormat=NULL;
    if(CGLChoosePixelFormat(attrs,&format,&count) || CGLCreateContext(format,NULL,&producer) ||
       CGLChoosePixelFormat(consumerAttrs,&consumerFormat,&count) ||
       CGLCreateContext(consumerFormat,NULL,&consumer) || CGLSetCurrentContext(producer))return 2;
    printf("renderer=%s\n",glGetString(GL_RENDERER));
    if(!strstr((const char *)glGetString(GL_RENDERER),"Intel"))return 2;
    io_service_t service=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("VMsvga2Accel"));
    if(!service){fprintf(stderr,"Requires the owned virtual accelerator; native server tracing is separate.\n");return 2;}
    void *iokit=dlopen("/System/Library/Frameworks/IOKit.framework/IOKit",RTLD_NOW|RTLD_LOCAL);
    IOReturn (*newID)(unsigned,int *)=iokit?dlsym(iokit,"IOAccelCreateAccelID"):NULL;
    IOReturn (*freeID)(unsigned,int)=iokit?dlsym(iokit,"IOAccelDestroyAccelID"):NULL;
    IOReturn (*create)(io_service_t,uint32_t,uint32_t,void **)=iokit?dlsym(iokit,"IOAccelCreateSurface"):NULL;
    IOReturn (*destroy)(void *)=iokit?dlsym(iokit,"IOAccelDestroySurface"):NULL;
    IOReturn (*shape)(void *,const IOAccelDeviceRegion *,unsigned,unsigned)=iokit?dlsym(iokit,"IOAccelSetSurfaceFramebufferShape"):NULL;
    void *engine=dlopen("/System/Library/Frameworks/OpenGL.framework/Resources/GLEngine.bundle/GLEngine",RTLD_NOW|RTLD_LOCAL);
    Attach attach=engine?dlsym(engine,"gliAttachDrawableWithOptions"):NULL;
    Import import=engine?dlsym(engine,"gliSetInteger"):NULL;
    if(!newID || !freeID || !create || !destroy || !shape || !attach || !import)return 2;
    int identifier=0;struct SnowPBuffer pb={0};
    IOReturn error=newID(0,&identifier);printf("new ID=%x status=%x\n",identifier,error);
    if(error || identifier<=2)return 2;
    pb.mode=back?0x24:0x2b;
    pb.internal=back?GL_RGBA8:GL_RGBA16;
    unsigned surfaceMode=back?0x200024:pb.mode;
    error=create(service,identifier,surfaceMode,&pb.surface);printf("create mode=%x status=%x\n",surfaceMode,error);
    int good=!error;GLuint texture=0,output=0,fbo=0;
    if(!good)goto done;
    struct {uint32_t count;IOAccelBounds bounds,rect;} region={1,{0,0,W,H},{0,0,0,0}};
    error=shape(pb.surface,(const IOAccelDeviceRegion *)&region,1,0);printf("shape=%x\n",error);
    if(error){good=0;goto done;}
    pb.identifier=identifier;pb.target=GL_TEXTURE_RECTANGLE_ARB;
    pb.accelerator=service;pb.width=W;pb.height=H;
    CGLError ce=attach(producer->rend,0x5a,&pb,0,0);printf("attach=%d\n",ce);
    if(ce){good=0;goto done;}
    GLint red=-1;glGetIntegerv(GL_RED_BITS,&red);printf("actual red bits=%d\n",red);
    const GLushort exact16[]={0x1234,0x5678,0x9abc,0xffff};
    const GLushort exact8[]={0x1212,0x5656,0x9a9a,0xffff};
    const GLushort *expected=back?exact8:exact16;
    glViewport(0,0,W,H);glDisable(GL_DITHER);glClearColor(expected[0]/65535.f,expected[1]/65535.f,expected[2]/65535.f,1);
    if(back){glDrawBuffer(GL_BACK);glReadBuffer(GL_BACK);}
    glClear(GL_COLOR_BUFFER_BIT);good=check(back?"producer back":"producer64",expected);
    if(back)glFlush();else glFinish();
    good&=glGetError()==GL_NO_ERROR;
    CGLSetCurrentContext(consumer);glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
    GLint record[]={identifier,GL_TEXTURE_RECTANGLE_ARB,pb.internal,W,H,0,GL_FRONT,0};
    ce=import(consumer->rend,0x3e5,record);printf("cross-context import=%d\n",ce);good&=!ce;
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glGenTextures(1,&output);glBindTexture(GL_TEXTURE_2D,output);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16,4,4,0,GL_RGBA,GL_UNSIGNED_SHORT,NULL);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,output,0);
    good&=glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT;
    glViewport(0,0,4,4);glDisable(GL_BLEND);glDisable(GL_DITHER);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);glEnable(GL_TEXTURE_RECTANGLE_ARB);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    for(unsigned pass=0;pass<2;++pass){
        const GLushort changed16[]={0x3456,0x789a,0xbcde,0xffff};
        const GLushort changed8[]={0x3434,0x7878,0xbcbc,0xffff};
        const GLushort *changed=back?changed8:changed16;
        if(pass){
            if(back){
                CGLSetCurrentContext(producer);glDrawBuffer(GL_FRONT);glReadBuffer(GL_FRONT);
                glClearColor(changed[0]/65535.f,changed[1]/65535.f,changed[2]/65535.f,1);
                glClear(GL_COLOR_BUFFER_BIT);good&=check("producer front",changed);glFlush();
                good&=glGetError()==GL_NO_ERROR;
                CGLSetCurrentContext(consumer);
            }else good&=cpuWrite(service,identifier,W,H,changed);
            ce=import(consumer->rend,0x3e5,record);good&=!ce;
        }
        glBegin(GL_QUADS);glTexCoord2f(0,0);glVertex2f(-1,-1);glTexCoord2f(W,0);glVertex2f(1,-1);
        glTexCoord2f(W,H);glVertex2f(1,1);glTexCoord2f(0,H);glVertex2f(-1,1);glEnd();
        good&=check(back?(pass?"flushed front -> texture":"flushed back -> texture"):
            (pass?"CPU dirty -> typed texture":"consumer64"),pass?changed:expected);
    }
done:
    CGLSetCurrentContext(consumer);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);
    if(fbo)glDeleteFramebuffersEXT(1,&fbo);if(output)glDeleteTextures(1,&output);if(texture)glDeleteTextures(1,&texture);
    attach(producer->rend,0,NULL,0,0);CGLSetCurrentContext(NULL);
    CGLDestroyContext(consumer);CGLDestroyContext(producer);CGLDestroyPixelFormat(consumerFormat);CGLDestroyPixelFormat(format);
    if(pb.surface)destroy(pb.surface);freeID(0,identifier);IOObjectRelease(service);
    printf("RESULT %s %s CGLS pbuffer\n",good?"PASS":"FAIL",back?"double-buffer flush":"actual64-bit");return good?0:1;
}
