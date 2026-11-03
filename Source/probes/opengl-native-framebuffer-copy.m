/* Same SDK framebuffer -> owned CGS window copy on accelerated Mavericks
 * hardware and our guest. No desktop capture or unrelated surface writes.
 * Source pixels are the green patch in this fixture's own visible window.
 */
#import <Cocoa/Cocoa.h>
#import <OpenGL/CGLContext.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <OpenGL/CGLRenderers.h>
#include <IOKit/graphics/IOGraphicsLib.h>
#include <IOKit/graphics/IOGraphicsTypes.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/graphics/IOGraphicsInterface.h>
#include <dlfcn.h>
#include <stdint.h>
#include <sys/sysctl.h>
extern CGLError CGLGetSurface(CGLContextObj,uint32_t *,uint32_t *,uint32_t *);
static int outcome=2;
/* The matching physical IOAccelerator2D plugin's copy callback was
 * decompiled at 0x1bd4: method 0x102 receives a four-word header followed by
 * six INT16 source/destination/extent fields per rectangle. Like the native
 * fill fixture, this only operates on the owned CGS destination. */
static IOReturn nativeCopy(io_service_t fb,uint32_t surface,int sx,int sy)
{
    char build[64]={0};size_t n=sizeof(build);
    if(sysctlbyname("kern.osversion",build,&n,NULL,0) || strcmp(build,"13F1911"))return kIOReturnUnsupported;
    io_service_t accelerator=0;UInt32 index=0;io_connect_t connection=0;
    IOReturn e=IOAccelFindAccelerator(fb,&accelerator,&index);
    if(!e && IOObjectConformsTo(accelerator,"VMsvga2Accel"))e=kIOReturnUnsupported;
    if(!e)e=IOServiceOpen(accelerator,mach_task_self(),2,&connection);
    if(accelerator)IOObjectRelease(accelerator);
    uint64_t target[]={surface,0x800};
    if(!e)e=IOConnectCallScalarMethod(connection,0x100,target,2,NULL,NULL);
    struct { uint32_t operation,count,source,index;int16_t sx,sy,dx,dy,w,h; }
        copy={1,1,0,index,sx,sy,70,20,16,16};
    if(!e)e=IOConnectCallStructMethod(connection,0x102,&copy,sizeof(copy),NULL,NULL);
    uint64_t options=0;
    if(!e)e=IOConnectCallScalarMethod(connection,0x101,&options,1,NULL,NULL);
    if(connection)IOServiceClose(connection);return e;
}
static int readSurface(CGLContextObj context,uint32_t surface,int w,int h,int x,int y,unsigned r,unsigned g,unsigned b)
{
    const char *resources=getenv("GL_RESOURCES");char path[4096];
    snprintf(path,sizeof(path),"%s/GLEngine.bundle/GLEngine",resources?resources:
        "/System/Library/Frameworks/OpenGL.framework/Resources");
    void *engine=dlopen(path,RTLD_NOW|RTLD_LOCAL);
    CGLError (*set)(GLIContext,unsigned,const GLint *)=engine?(void *)dlsym(engine,"gliSetInteger"):NULL;
    if(!set)return 0;
    GLuint source=0,output=0,fbo=0;glGenTextures(1,&source);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,source);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    GLint record[]={surface,GL_TEXTURE_RECTANGLE_ARB,GL_RGBA,w,h};
    CGLError error=set(context->rend,0x3e7,record);
    glGenTextures(1,&output);glBindTexture(GL_TEXTURE_2D,output);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,output,0);
    if(error || glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)!=GL_FRAMEBUFFER_COMPLETE_EXT)return 0;
    glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
    glViewport(0,0,1,1);glDisable(GL_SCISSOR_TEST);glDisable(GL_BLEND);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,source);glEnable(GL_TEXTURE_RECTANGLE_ARB);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);glColor4f(1,1,1,1);
    glBegin(GL_QUADS);glTexCoord2f(x,y);glVertex2f(-1,-1);glTexCoord2f(x+1,y);glVertex2f(1,-1);
    glTexCoord2f(x+1,y+1);glVertex2f(1,1);glTexCoord2f(x,y+1);glVertex2f(-1,1);glEnd();
    unsigned char pixel[4]={0};glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    GLenum ge=glGetError();int good=!ge && abs((int)pixel[0]-(int)r)<8 &&
        abs((int)pixel[1]-(int)g)<8 && abs((int)pixel[2]-(int)b)<8;
    fprintf(stderr,"%s own patch at surface-local %d,%d RGBA=%u,%u,%u,%u expected-RGB=%u,%u,%u CGL=%d GL=%x\n",
        good?"PASS":"FAIL",x,y,pixel[0],pixel[1],pixel[2],pixel[3],r,g,b,error,ge);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&fbo);
    glDeleteTextures(1,&source);glDeleteTextures(1,&output);glDisable(GL_TEXTURE_RECTANGLE_ARB);
    dlclose(engine);return good;
}
@interface VIMCopyView : NSOpenGLView { BOOL _painted; }
@property BOOL painted;
@end
@implementation VIMCopyView
@synthesize painted=_painted;
- (void)finish:(NSTimer *)timer { (void)timer;exit(outcome); }
- (void)drawRect:(NSRect)rect
{
    (void)rect;if(self.painted)return;self.painted=YES;
    [self.openGLContext makeCurrentContext];
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1070
    NSRect pixels=self.bounds;
#else
    NSRect pixels=[self convertRectToBacking:self.bounds];
#endif
    glViewport(0,0,pixels.size.width,pixels.size.height);
    glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);glScissor(16,16,16,16);
    glClearColor(0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);glDisable(GL_SCISSOR_TEST);glFinish();
    [self performSelector:@selector(measure) withObject:nil afterDelay:2];
}
- (void)measure
{
    [self.openGLContext makeCurrentContext];CGLContextObj context=self.openGLContext.CGLContextObj;
    const char *renderer=(const char *)glGetString(GL_RENDERER);
    if(!renderer || !strstr(renderer,"Intel")){
        fprintf(stderr,"REFUSED non-Intel framebuffer-copy reference: %s\n",renderer?renderer:"none");exit(2);
    }
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1070
    NSRect pixels=self.bounds;
#else
    NSRect pixels=[self convertRectToBacking:self.bounds];
#endif
    int w=pixels.size.width,h=pixels.size.height;
    uint32_t ids[3]={0};CGLError ce=CGLGetSurface(context,&ids[0],&ids[1],&ids[2]);
    io_service_t framebuffer=CGDisplayIOServicePort(CGMainDisplayID());
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1070
    NSRect screenRect=[NSWindow contentRectForFrameRect:self.window.frame styleMask:self.window.styleMask];
    CGFloat scale=1;
#else
    NSRect screenRect=[self.window convertRectToScreen:[self convertRect:self.bounds toView:nil]];
    CGFloat scale=self.window.backingScaleFactor;
#endif
    NSRect screen=self.window.screen.frame;
    int sx=(int)llround((screenRect.origin.x-screen.origin.x)*scale)+16;
    int sy=(int)llround((NSMaxY(screen)-NSMaxY(screenRect))*scale)+h-32;
    if(getenv("VIM_COPY_PENDING_GPU")) {
        // A whole-CPU-buffer reimport after the copy would lose these queued
        // GPU writes outside the copy's destination. Both must survive.
        glEnable(GL_SCISSOR_TEST);glScissor(4,4,4,4);
        glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);glDisable(GL_SCISSOR_TEST);
    }
    if(getenv("VIM_COPY_RAW_NATIVE")) {
        IOReturn e=ce || !ids[2]?kIOReturnNotReady:nativeCopy(framebuffer,ids[2],sx,sy);
        fprintf(stderr,"native owned copy renderer=%s source=%d,%d destination=70,20 result=%x\n",glGetString(GL_RENDERER),sx,sy,e);
        if(!e && getenv("VIM_COPY_FLUSH_CURRENT")){glFlush();fprintf(stderr,"flushed current GL drawable after external 2D copy\n");}
        int good=!e && readSurface(context,ids[2],w,h,74,24,0,255,0);
        if(!e && getenv("VIM_COPY_PENDING_GPU"))good=readSurface(context,ids[2],w,h,6,h-1-6,255,0,0) && good;
        outcome=good?0:1;
        [NSTimer scheduledTimerWithTimeInterval:1 target:self selector:@selector(finish:) userInfo:nil repeats:NO];return;
    }
    IOCFPlugInInterface **plugin=NULL;IOGraphicsAcceleratorInterface **ga=NULL;SInt32 score=0;
    IOReturn error=ce || !ids[2] || !framebuffer?kIOReturnNotReady:
        IOCreatePlugInInterfaceForService(framebuffer,kIOGraphicsAcceleratorTypeID,kIOCFPlugInInterfaceID,&plugin,&score);
    fprintf(stderr,"create SDK framebuffer plugin=%x\n",error);
    if(!error && ((*plugin)->QueryInterface(plugin,CFUUIDGetUUIDBytes(kIOGraphicsAcceleratorInterfaceID),(LPVOID *)&ga) || !ga))
        error=kIOReturnUnsupported;
    fprintf(stderr,"query SDK accelerator interface=%x\n",error);
    IOBlitSurface target={0};target.pixelFormat=kIO32ARGBPixelFormat;
    target.size=(IOBlitRectangle){0,0,w,h};int allocated=0;
    if(!error){error=(*ga)->AllocateSurface(ga,kIOBlitHasCGSSurface,&target,(void *)(uintptr_t)ids[2]);allocated=!error;}
    fprintf(stderr,"allocate owned CGS surface=%x\n",error);
    if(!error)error=(*ga)->SetDestination(ga,kIOBlitSurfaceDestination,&target);
    fprintf(stderr,"set owned destination=%x\n",error);
    IOBlitterPtr copy=NULL;
    if(!error)error=(*ga)->GetBlitter(ga,0,kIOBlitTypeCopyRegion,kIOBlitSourceDefault,&copy);
    if(!error && !copy)error=kIOReturnUnsupported;
    struct { uint32_t count;IOAccelBounds bounds,rect; } region={1,{sx,sy,16,16},{sx,sy,16,16}};
    fprintf(stderr,"renderer=%s own surface=%x pixels=%dx%d source=%d,%d destination=70,20 prepare=%x\n",
        glGetString(GL_RENDERER),ids[2],w,h,sx,sy,error);
    IOBlitCopyRegion operation={0};operation.deltaX=70;operation.deltaY=20;
    operation.region=(IOAccelDeviceRegion *)&region;
    if(!error)error=copy(ga,0,kIOBlitTypeCopyRegion,kIOBlitSourceDefault,&operation.operation,NULL);
    if(!error)error=(*ga)->Flush(ga,0);
    if(!error)error=(*ga)->WaitComplete(ga,0);
    fprintf(stderr,"SDK own-window framebuffer copy result=%x\n",error);
    if(!error && getenv("VIM_COPY_FLUSH_CURRENT")){glFlush();fprintf(stderr,"flushed current GL drawable after external 2D copy\n");}
    int good=!error && readSurface(context,ids[2],w,h,74,24,0,255,0);
    if(!error && getenv("VIM_COPY_PENDING_GPU"))good=readSurface(context,ids[2],w,h,6,h-1-6,255,0,0) && good;
    if(allocated)(*ga)->FreeSurface(ga,0,&target);
    if(ga)(*ga)->Release(ga);if(plugin)IODestroyPlugInInterface(plugin);
    outcome=good?0:1;
    [NSTimer scheduledTimerWithTimeInterval:1 target:self selector:@selector(finish:) userInfo:nil repeats:NO];
}
@end
int main(void)
{
    @autoreleasepool {
        NSApplication *app=[NSApplication sharedApplication];[app setActivationPolicy:NSApplicationActivationPolicyRegular];
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1070
        // Select the online Intel renderer BEFORE creating a context on the
        // dual-GPU reference. The VM uses its own advertised renderer.
        GLint selected=0;
        io_service_t vm=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("VMsvga2"));
        if(vm)IOObjectRelease(vm);
        else {
            CGLRendererInfoObj info=NULL;GLint count=0;
            if(CGLQueryRendererInfo(~0U,&info,&count))return 2;
            for(GLint i=0;i<count;i++) {
                GLint id=0,online=0,accelerated=0;
                CGLDescribeRenderer(info,i,kCGLRPRendererID,&id);CGLDescribeRenderer(info,i,kCGLRPOnline,&online);
                CGLDescribeRenderer(info,i,kCGLRPAccelerated,&accelerated);
                if(online&&accelerated&&(id&kCGLRendererIDMatchingMask)==0x24300)selected=id;
            }
            CGLDestroyRendererInfo(info);if(!selected)return 77;
        }
        NSOpenGLPixelFormatAttribute attrs[]={NSOpenGLPFAAccelerated,NSOpenGLPFANoRecovery,
            NSOpenGLPFAColorSize,24,NSOpenGLPFAAlphaSize,8,
            selected?NSOpenGLPFARendererID:0,(NSOpenGLPixelFormatAttribute)selected,0};
#else
        NSOpenGLPixelFormatAttribute attrs[]={NSOpenGLPFAAccelerated,NSOpenGLPFANoRecovery,
            (NSOpenGLPixelFormatAttribute)kCGLPFASupportsAutomaticGraphicsSwitching,
            NSOpenGLPFAColorSize,24,NSOpenGLPFAAlphaSize,8,0};
#endif
        NSOpenGLPixelFormat *format=[[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];if(!format)return 2;
        NSWindow *window=[[NSWindow alloc] initWithContentRect:NSMakeRect(100,150,160,120)
            styleMask:NSTitledWindowMask|NSClosableWindowMask backing:NSBackingStoreBuffered defer:NO];
        VIMCopyView *view=[[VIMCopyView alloc] initWithFrame:NSMakeRect(0,0,160,120) pixelFormat:format];
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1070
        view.wantsBestResolutionOpenGLSurface=YES;
#endif
        window.contentView=view;
        window.title=@"Owned framebuffer copy contract";[window makeKeyAndOrderFront:nil];
        [app activateIgnoringOtherApps:YES];[app run];
    }
    return outcome;
}
