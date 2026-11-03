/* Physical hardware reference for a real CGL window shared
 * with a separate single-buffer consumer. No WindowServer injection/patching.
 * Pixel expectations are qualified on native Mavericks, not GLRendererFloat.
 */
#import <Cocoa/Cocoa.h>
#import <OpenGL/CGLContext.h>
#import <OpenGL/gl.h>
#import <OpenGL/glext.h>
#include <dlfcn.h>
#include <stdint.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/graphics/IOGraphicsInterfaceTypes.h>
#include "../vendor/source/VMsvga2/AC/UCMethods.h"
#include "../vendor/source/VMsvga2/vminclude/vim_framebuffer.h"
extern CGLError CGLGetSurface(CGLContextObj,uint32_t *,uint32_t *,uint32_t *);
extern int CGSGetSurfaceBounds(int,int,int,CGRect *);
typedef CGLError (*SetInteger)(GLIContext,unsigned,const GLint *);
static int result=2;
static void makeProducerCurrent(CGLContextObj context,int unflipped,const uint32_t ids[3],void *engine)
{
    CGLError error=CGLSetCurrentContext(context);
    if(!error && unflipped) {
        CGLError (*attach)(GLIContext,unsigned,const void *,unsigned,unsigned)=
            engine?(void *)dlsym(engine,"gliAttachDrawableWithOptions"):NULL;
        uint32_t target[6]={0,0,ids[2],0,0,0};
        error=attach?attach(context->rend,0x5a,target,0,0):kCGLBadContext;
    }
    if(error) { fprintf(stderr,"producer attachment error=%d\n",error);exit(2); }
}
static int pixels(const char *name,int w,int h,int green)
{
    unsigned char bottom[4]={0},top[4]={0};
    glReadPixels(w/2,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,bottom);
    glReadPixels(w/2,h-1,1,1,GL_RGBA,GL_UNSIGNED_BYTE,top);
    GLenum error=glGetError();
    int good=!error && bottom[0]==0 && bottom[1]==(green?255:0) &&
        bottom[2]==(green?0:255) && bottom[3]==255 &&
        top[0]==(green?0:255) && top[1]==(green?255:0) && !top[2] && top[3]==255;
    fprintf(stderr,"%s %s bottom=%u,%u,%u,%u top=%u,%u,%u,%u error=%x\n",good?"PASS":"FAIL",name,
        bottom[0],bottom[1],bottom[2],bottom[3],top[0],top[1],top[2],top[3],error);
    return good;
}
static void sample(GLuint source,int w,int h)
{
    glViewport(0,0,w,h);glDisable(GL_SCISSOR_TEST);glDisable(GL_BLEND);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,source);
    glEnable(GL_TEXTURE_RECTANGLE_ARB);glColor4f(1,1,1,1);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    glBegin(GL_QUADS);
    glTexCoord2f(0,0);glVertex2f(-1,-1);glTexCoord2f(w,0);glVertex2f(1,-1);
    glTexCoord2f(w,h);glVertex2f(1,1);glTexCoord2f(0,h);glVertex2f(-1,1);glEnd();glFinish();
}
static int rgbaPixels(const char *label,int w,int h,unsigned r,unsigned g,unsigned b,unsigned a)
{
    unsigned char pixel[4]={0};glReadPixels(w/2,h/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    GLenum error=glGetError();int good=!error && pixel[0]==r && pixel[1]==g && pixel[2]==b && pixel[3]==a;
    fprintf(stderr,"%s %s RGBA=%u,%u,%u,%u error=%x\n",good?"PASS":"FAIL",label,
        pixel[0],pixel[1],pixel[2],pixel[3],error);return good;
}
static int solidPixels(const char *label,int w,int h,unsigned r,unsigned g,unsigned b)
{ return rgbaPixels(label,w,h,r,g,b,255); }
static int fillSurface(uint32_t surface,int w,int h)
{
    io_service_t service=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("VMsvga2Accel"));
    io_connect_t connection=0;IOReturn e=service?IOServiceOpen(service,mach_task_self(),2,&connection):kIOReturnNotFound;
    if(service)IOObjectRelease(service);
    uint64_t target[]={surface,0x800};uint32_t record[11];size_t bytes=sizeof(record);
    if(!e)e=IOConnectCallMethod(connection,kIOVM2DSetSurface,target,2,NULL,0,NULL,NULL,record,&bytes);
    IOBlitRectangle rect={0,0,w,h};uint64_t mask=0xffffffff;
    if(!e)e=IOConnectCallMethod(connection,kIOVM2DRectFill,&mask,1,&rect,sizeof(rect),NULL,NULL,NULL,NULL);
    if(connection)IOServiceClose(connection);
    fprintf(stderr,"%s 2D alpha-clear on producer storage error=%x\n",e?"FAIL":"PASS",e);return !e;
}
/* Only the opt-in research-provider phase uses this private extension.
 * The normal physical reference probe never invokes VMsvga2 methods. */
static int cpuWrite(uint32_t surface,int w,int h)
{
    io_service_t service=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("VMsvga2Accel"));
    io_connect_t connection=0;IOReturn error=service?IOServiceOpen(service,mach_task_self(),2,&connection):kIOReturnNotFound;
    if(service)IOObjectRelease(service);
    uint64_t target[]={surface,0x800};uint32_t record[11];size_t bytes=sizeof(record);
    if(!error)error=IOConnectCallMethod(connection,kIOVM2DSetSurface,target,2,NULL,0,NULL,NULL,record,&bytes);
    struct VIMSurfaceMapping m={0};bytes=sizeof(m);int locked=0;
    if(!error) { error=IOConnectCallStructMethod(connection,kIOVM2DLockSurfaceBacking,NULL,0,&m,&bytes);locked=!error; }
    if(!error && (bytes!=sizeof(m) || m.layout.version!=VIM_FRAMEBUFFER_LAYOUT_VERSION ||
        m.layout.bits_per_pixel!=32 || m.layout.width!=(unsigned)w || m.layout.height!=(unsigned)h ||
        m.layout.row_bytes<(unsigned)w*4 || m.layout.offset>m.length ||
        (uint64_t)(h-1)*m.layout.row_bytes+(uint64_t)w*4>m.length-m.layout.offset)) error=kIOReturnBadArgument;
    if(!error) for(int y=0;y<h;++y) {
        unsigned char *row=(void *)(uintptr_t)(m.address+m.layout.offset+(uint64_t)y*m.layout.row_bytes);
        for(int x=0;x<w;++x) { row[x*4]=255;row[x*4+1]=0;row[x*4+2]=255;row[x*4+3]=255; }
    }
    if(locked) {
        uint64_t option=0,flags=0;uint32_t n=1;
        IOReturn unlocked=IOConnectCallScalarMethod(connection,kIOVM2DUnlockMemory,&option,1,&flags,&n);
        if(!error)error=unlocked;
    }
    if(connection)IOServiceClose(connection);
    fprintf(stderr,"%s explicit CPU write to the same resource error=%x\n",error?"FAIL":"PASS",error);return !error;
}
@interface VIMSharedWindow : NSOpenGLView
@property BOOL measured;
@end
@implementation VIMSharedWindow
- (void)finish:(NSTimer *)timer { (void)timer;exit(result); }
- (void)drawRect:(NSRect)rect
{
    (void)rect;if(self.measured)return;self.measured=YES;
    // AppKit may call drawRect synchronously inside makeKeyAndOrderFront.
    // Source-destruction tests must not deallocate that window on its stack.
    [self performSelector:@selector(runSharingProbe) withObject:nil afterDelay:0];
}
- (void)runSharingProbe
{
    NSOpenGLContext *producer=[self.openGLContext retain];[producer makeCurrentContext];
    CGLContextObj original=producer.CGLContextObj;
    int unflipped=getenv("VIM_GL_PRODUCER_UNFLIPPED")!=NULL;
    GLenum producer_buffer=unflipped?GL_FRONT:GL_BACK;
    const char *resources=getenv("GL_RESOURCES");char path[4096];
    snprintf(path,sizeof(path),"%s/GLEngine.bundle/GLEngine",resources?resources:
        "/System/Library/Frameworks/OpenGL.framework/Resources");
    void *engine=dlopen(path,RTLD_NOW|RTLD_LOCAL);
    uint32_t ids[3]={0};CGLError error=CGLGetSurface(original,&ids[0],&ids[1],&ids[2]);
    if(error)exit(2);
    makeProducerCurrent(original,unflipped,ids,engine);
    NSRect size=[self convertRectToBacking:self.bounds];int w=size.size.width,h=size.size.height;
    glViewport(0,0,w,h);glDrawBuffer(producer_buffer);glDisable(GL_SCISSOR_TEST);
    glClearColor(unflipped?0:1,0,unflipped?1:0,1);glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);glScissor(0,h/2,w,h-h/2);
    glClearColor(unflipped?1:0,0,unflipped?0:1,1);glClear(GL_COLOR_BUFFER_BIT);glDisable(GL_SCISSOR_TEST);
    // Private unflipped CGS drawing is submitted with glFlush, not an
    // ordinary AppKit window swap. Native NVIDIA retains the old image if
    // flushBuffer is used here; that is a different producer contract.
    if(unflipped)glFlush();else[producer flushBuffer];
    error=CGLGetSurface(original,&ids[0],&ids[1],&ids[2]);
    fprintf(stderr,"producer renderer=%s surface=%x get=%d swap-error=%x\n",glGetString(GL_RENDERER),ids[2],error,glGetError());
    int storage_width=w,storage_height=h;
    if(getenv("VIM_GL_IMPORT_CROP")) {
        w-=2;if(!strcmp(getenv("VIM_GL_IMPORT_CROP"),"both"))h-=2;
    }
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj format=NULL;CGLContextObj consumer=NULL;GLint count=0;
    if(!error)error=CGLChoosePixelFormat(attrs,&format,&count);
    GLint renderer=0;if(format)CGLDescribePixelFormat(format,0,kCGLPFARendererID,&renderer);
    fprintf(stderr,"consumer format error=%d count=%d renderer=%x\n",error,count,renderer);
    int unshared=getenv("VIM_GL_CONSUMER_UNSHARED")!=NULL;
    if(!error)error=CGLCreateContext(format,unshared?NULL:original,&consumer);
    if(format)CGLDestroyPixelFormat(format);
    if(!error)error=CGLSetCurrentContext(consumer);
    fprintf(stderr,"single-buffer consumer=%d share-group=%s\n",error,unshared?"independent":"producer");
    const char *(*transport)(void)=engine?(void *)dlsym(engine,"DGTransportName"):NULL;
    fprintf(stderr,"actual renderer transport=%s\n",transport?transport():"native GLD");
    SetInteger set=engine?(SetInteger)dlsym(engine,"gliSetInteger"):NULL;
    GLuint source=0,output=0,fbo=0;
    if(!error && ids[2] && set) {
        glGenTextures(1,&source);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,source);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        unsigned import_operation=getenv("VIM_GL_PRIVATE_IMPORT") && !strcmp(getenv("VIM_GL_PRIVATE_IMPORT"),"3e5")?0x3e5:0x3e7;
        unsigned flags=import_operation==0x3e5?0x404:0;
        if(import_operation==0x3e5 && getenv("VIM_GL_IMPORT_FLAGS"))
            flags=(unsigned)strtoul(getenv("VIM_GL_IMPORT_FLAGS"),NULL,16);
        GLint record[]={ids[2],GL_TEXTURE_RECTANGLE_ARB,getenv("VIM_GL_IMPORT_RGB8")?GL_RGB8:GL_RGBA,w,h,0,flags,0};
        error=set(consumer->rend,import_operation,record);
        fprintf(stderr,"import producer surface=%d operation=%x internal=%x flags=%x\n",error,import_operation,record[2],flags);
        if(!error) {
            GLint tw=0,th=0;glGetTexLevelParameteriv(GL_TEXTURE_RECTANGLE_ARB,0,GL_TEXTURE_WIDTH,&tw);
            glGetTexLevelParameteriv(GL_TEXTURE_RECTANGLE_ARB,0,GL_TEXTURE_HEIGHT,&th);
            GLenum size_error=glGetError();
            fprintf(stderr,"%s imported logical dimensions=%d,%d requested=%d,%d backing-width=%d error=%x\n",
                !size_error&&tw==w&&th==h?"PASS":"FAIL",tw,th,w,h,storage_width,size_error);
            if(size_error || tw!=w || th!=h)error=kCGLBadValue;
        }
        if(!error) {
            glGenTextures(1,&output);glBindTexture(GL_TEXTURE_2D,output);
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
            glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
            glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,output,0);
            GLenum status=glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);
            if(status==GL_FRAMEBUFFER_COMPLETE_EXT) {
                glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
                sample(source,w,h);int good=pixels("native window initial publication",w,h,0);
                makeProducerCurrent(original,unflipped,ids,engine);
                glDrawBuffer(producer_buffer);glDisable(GL_SCISSOR_TEST);
                glClearColor(0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);if(unflipped)glFlush();else[producer flushBuffer];
                CGLSetCurrentContext(consumer);
                // Rebind the existing private surface texture, no reimport.
                sample(source,w,h);good=pixels("same surface after producer redraw and texture rebind",w,h,1) && good;
                // Distinguish a stale consumer alias from failed publication.
                // Passing after reimport must not mask the live-alias failure.
                error=set(consumer->rend,import_operation,record);
                fprintf(stderr,"explicit reimport diagnostic=%d\n",error);
                sample(source,w,h);good=pixels("explicit reimport sees latest producer (not an alias pass)",w,h,1) && !error && good;
                if(getenv("VIM_GL_2D_FILL")) {
                    int filled=fillSurface(ids[2],storage_width,storage_height);sample(source,w,h);
                    good=filled && rgbaPixels("existing RGBA alias sees 2D mask-clear without reimport",w,h,0,255,0,0) && good;
                    makeProducerCurrent(original,unflipped,ids,engine);
                    glDrawBuffer(producer_buffer);glDisable(GL_SCISSOR_TEST);
                    glClearColor(0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);if(unflipped)glFlush();else[producer flushBuffer];
                    CGLSetCurrentContext(consumer);sample(source,w,h);
                    good=pixels("GPU redraw supersedes 2D alpha-clear on the same alias",w,h,1) && good;
                }
                if(getenv("VIM_GL_CONTEXT_LIFETIME_TEST")) {
                    CGLContextObj replacement=NULL;
                    error=CGLCreateContext(CGLGetPixelFormat(consumer),consumer,&replacement);
                    if(!error) {
                        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&fbo);fbo=0;
                        CGLSetCurrentContext(replacement);CGLDestroyContext(consumer);consumer=replacement;
                        glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
                        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,output,0);
                        glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
                        sample(source,w,h);good=pixels("shared texture survives importing context destruction",w,h,1) && good;
                    } else { fprintf(stderr,"replacement shared context error=%d\n",error);good=0; }
                }
                if(getenv("VIM_GL_CPU_SURFACE_WRITE")) {
                    // A failed redefinition must leave the live association.
                    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,source);
                    glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA8,-1,h,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
                    GLenum rejected=glGetError();
                    fprintf(stderr,"%s invalid texture redefinition rejected error=%x\n",rejected==GL_INVALID_VALUE?"PASS":"FAIL",rejected);
                    int wrote=cpuWrite(ids[2],storage_width,storage_height);good=(rejected==GL_INVALID_VALUE) && wrote && good;
                    sample(source,w,h);good=solidPixels("existing alias sees CPU-originated magenta without reimport",w,h,255,0,255) && good;
                    sample(source,w,h);good=solidPixels("unchanged CPU epoch stays readable on repeated bind",w,h,255,0,255) && good;
                    makeProducerCurrent(original,unflipped,ids,engine);
                    glDrawBuffer(producer_buffer);glDisable(GL_SCISSOR_TEST);
                    glClearColor(0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);if(unflipped)glFlush();else[producer flushBuffer];
                    CGLSetCurrentContext(consumer);sample(source,w,h);
                    good=pixels("GPU publication supersedes CPU write on the same alias",w,h,1) && good;
                    unsigned char *owned=calloc((size_t)w*h,4);
                    if(owned) {
                        for(int i=0;i<w*h;++i) { owned[i*4+2]=255;owned[i*4+3]=255; }
                        glBindTexture(GL_TEXTURE_RECTANGLE_ARB,source);
                        glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,owned);free(owned);
                        GLenum immutable=glGetError();
                        fprintf(stderr,"%s imported storage replacement rejected like native hardware error=%x\n",
                            immutable==GL_INVALID_OPERATION?"PASS":"FAIL",immutable);
                        int wrote_again=cpuWrite(ids[2],storage_width,storage_height);good=(immutable==GL_INVALID_OPERATION) && wrote_again && good;
                        sample(source,w,h);good=solidPixels("rejected storage replacement preserves the live alias",w,h,255,0,255) && good;
                    } else good=0;
                }
                if(getenv("VIM_GL_REDEFINE_OBSERVE")) {
                    // Observational native-hardware contract survey, not an
                    // assertion that ordinary textures and private imports
                    // must have the same storage-redefinition semantics.
                    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,source);
                    glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA8,-1,h,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
                    fprintf(stderr,"OBSERVATION imported texture invalid-width error=%x\n",glGetError());
                    unsigned char *data=calloc((size_t)w*h,4);
                    if(data) {
                        for(int i=0;i<w*h;++i) { data[i*4+2]=255;data[i*4+3]=255; }
                        glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,data);free(data);
                        fprintf(stderr,"OBSERVATION imported texture valid redefinition error=%x\n",glGetError());
                        sample(source,w,h);unsigned char pixel[4]={0};
                        glReadPixels(w/2,h/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
                        fprintf(stderr,"OBSERVATION after redefinition RGBA=%u,%u,%u,%u error=%x\n",pixel[0],pixel[1],pixel[2],pixel[3],glGetError());
                        makeProducerCurrent(original,unflipped,ids,engine);
                        glDrawBuffer(producer_buffer);glDisable(GL_SCISSOR_TEST);
                        glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);if(unflipped)glFlush();else[producer flushBuffer];CGLSetCurrentContext(consumer);
                        sample(source,w,h);glReadPixels(w/2,h/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
                        fprintf(stderr,"OBSERVATION after later producer redraw RGBA=%u,%u,%u,%u error=%x\n",pixel[0],pixel[1],pixel[2],pixel[3],glGetError());
                    }
                }
                if(getenv("VIM_GL_SOURCE_CLOSE_TEST")) {
                    // Keep this view/context alive, but destroy its actual
                    // NSWindow/CGS source before rebinding the imported image.
                    [self retain];NSWindow *closed=self.window;
                    [closed setReleasedWhenClosed:NO];[closed close];[closed release];
                    CGRect bounds=CGRectZero;
                    int gone=CGSGetSurfaceBounds(ids[0],ids[1],ids[2],&bounds);
                    fprintf(stderr,"closed source bounds query=%d\n",gone);
                    CGLSetCurrentContext(consumer);glDisable(GL_TEXTURE_RECTANGLE_ARB);
                    glClearColor(1,1,0,1);glClear(GL_COLOR_BUFFER_BIT);
                    good=solidPixels("consumer output remains usable after source close",w,h,255,255,0) && good;
                    sample(source,w,h);GLint tw=0,th=0;
                    glGetTexLevelParameteriv(GL_TEXTURE_RECTANGLE_ARB,0,GL_TEXTURE_WIDTH,&tw);
                    glGetTexLevelParameteriv(GL_TEXTURE_RECTANGLE_ARB,0,GL_TEXTURE_HEIGHT,&th);
                    fprintf(stderr,"closed source imported texture dimensions=%d,%d error=%x\n",tw,th,glGetError());
                    unsigned char bottom[4]={0},top[4]={0};
                    glReadPixels(w/2,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,bottom);
                    glReadPixels(w/2,h-1,1,1,GL_RGBA,GL_UNSIGNED_BYTE,top);
                    unsigned nonzero=0;for(unsigned i=0;i<4;++i)nonzero|=bottom[i]|top[i];
                    GLenum error=glGetError();int transparent=!nonzero && !error && tw==w && th==h && gone!=0;
                    fprintf(stderr,"%s closed source retains dimensions and samples transparent RGBA error=%x\n",transparent?"PASS":"FAIL",error);
                    good=transparent && good;
                }
                result=good?0:1;
            } else fprintf(stderr,"output framebuffer=%x\n",status);
        }
    }
    if(consumer) {
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);if(fbo)glDeleteFramebuffersEXT(1,&fbo);
        if(source)glDeleteTextures(1,&source);if(output)glDeleteTextures(1,&output);
        CGLSetCurrentContext(NULL);CGLDestroyContext(consumer);
    }
    [producer makeCurrentContext];
    [producer release];
    [NSTimer scheduledTimerWithTimeInterval:1 target:self selector:@selector(finish:) userInfo:nil repeats:NO];
}
@end
int main(void)
{
    @autoreleasepool {
        NSApplication *app=[NSApplication sharedApplication];[app setActivationPolicy:NSApplicationActivationPolicyRegular];
        NSOpenGLPixelFormatAttribute attrs[]={NSOpenGLPFAAccelerated,NSOpenGLPFANoRecovery,
            NSOpenGLPFADoubleBuffer,NSOpenGLPFABackingStore,NSOpenGLPFAColorSize,24,NSOpenGLPFAAlphaSize,8,0};
        NSOpenGLPixelFormatAttribute single[]={NSOpenGLPFAAccelerated,NSOpenGLPFANoRecovery,
            NSOpenGLPFAColorSize,24,NSOpenGLPFAAlphaSize,8,0};
        NSOpenGLPixelFormat *format=[[NSOpenGLPixelFormat alloc] initWithAttributes:
            getenv("VIM_GL_PRODUCER_UNFLIPPED")?single:attrs];if(!format)return 2;
        NSWindow *window=[[NSWindow alloc] initWithContentRect:NSMakeRect(180,180,128,96)
            styleMask:NSTitledWindowMask|NSClosableWindowMask backing:NSBackingStoreBuffered defer:NO];
        [window setTitle:@"Native double-buffer window sharing"];
        VIMSharedWindow *view=[[VIMSharedWindow alloc] initWithFrame:NSMakeRect(0,0,128,96) pixelFormat:format];
        [window setContentView:view];[window makeKeyAndOrderFront:nil];[app activateIgnoringOtherApps:YES];[app run];
    }
    return result;
}
