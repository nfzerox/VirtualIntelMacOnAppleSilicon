/* Exercise the documented IOGraphicsAccelerator interface on this probe's
 * own registered window only. Never select/fill the display framebuffer.
 * The same binary runs against physical NVIDIA and the research provider. */
#import <Cocoa/Cocoa.h>
#import <OpenGL/CGLContext.h>
#import <OpenGL/gl.h>
#import <OpenGL/glext.h>
#include <dlfcn.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/graphics/IOGraphicsInterface.h>
#include <IOKit/graphics/IOGraphicsLib.h>
#include <stdint.h>
#include <sys/sysctl.h>
extern CGLError CGLGetSurface(CGLContextObj,uint32_t *,uint32_t *,uint32_t *);
static int outcome=2;
static uint32_t pixel(vm_address_t address,unsigned stride,int x,int y)
{ uint32_t value;memcpy(&value,(void *)(address+(size_t)y*stride+(size_t)x*4),4);return value; }
/* Matching 13F1911 IOAccelerator2D.plugin disassembly: own CGS surface,
 * never the plugin Start's default framebuffer selection. The stock plugin
 * has no LockSurface implementation, so verify the result through native GL
 * rather than inventing a CPU mapping ABI. This opt-in is physical-only. */
static int nativeFill(io_service_t framebuffer,CGLContextObj context,uint32_t surface,int width,int height)
{
    char build[64]={0};size_t build_size=sizeof(build);
    if(sysctlbyname("kern.osversion",build,&build_size,NULL,0) || strcmp(build,"13F1911")) {
        fprintf(stderr,"Private native 2D command fixture is qualified only for 13F1911; refusing this build\n");return 2;
    }
    io_service_t accelerator=0;UInt32 index=0;io_connect_t connection=0;
    IOReturn e=IOAccelFindAccelerator(framebuffer,&accelerator,&index);
    if(!e && IOObjectConformsTo(accelerator,"VMsvga2Accel"))e=kIOReturnUnsupported;
    if(!e)e=IOServiceOpen(accelerator,mach_task_self(),2,&connection);
    if(accelerator)IOObjectRelease(accelerator);
    uint64_t target[]={surface,0x800};
    if(!e)e=IOConnectCallScalarMethod(connection,0x100,target,2,NULL,NULL);
    fprintf(stderr,"native direct own-surface selection=%x\n",e);
    struct { uint32_t operation,count,kind,color;uint32_t reserved;int16_t x,y,w,h; }
        command={0,1,2,0x7f123456,0,7,9,13,11};
    if(getenv("VIM_2D_COLOR"))command.color=(uint32_t)strtoul(getenv("VIM_2D_COLOR"),NULL,16);
    if(!e)e=IOConnectCallStructMethod(connection,0x102,&command,sizeof(command),NULL,NULL);
    uint64_t options=0;
    if(!e)e=IOConnectCallScalarMethod(connection,0x101,&options,1,NULL,NULL);
    fprintf(stderr,"native GPU fill and completion=%x command-bytes=%lu\n",e,(unsigned long)sizeof(command));
    if(connection)IOServiceClose(connection);
    if(e)return 2;
    unsigned char direct[4]={0};glReadBuffer(GL_FRONT);
    glReadPixels(12,height-1-14,1,1,GL_RGBA,GL_UNSIGNED_BYTE,direct);
    fprintf(stderr,"native ordinary framebuffer after 2D operation RGBA=%u,%u,%u,%u error=%x\n",
        direct[0],direct[1],direct[2],direct[3],glGetError());
    void *engine=dlopen("/System/Library/Frameworks/OpenGL.framework/Resources/GLEngine.bundle/GLEngine",RTLD_NOW|RTLD_LOCAL);
    CGLError (*set)(GLIContext,unsigned,const GLint *)=engine?(void *)dlsym(engine,"gliSetInteger"):NULL;
    GLuint imported=0,output=0,fbo=0;glGenTextures(1,&imported);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,imported);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    GLint record[]={surface,GL_TEXTURE_RECTANGLE_ARB,GL_RGBA,width,height};
    CGLError ce=set?set(context->rend,0x3e7,record):kCGLBadContext;
    fprintf(stderr,"native readback surface import=%d\n",ce);
    if(ce)return 2;
    glGenTextures(1,&output);glBindTexture(GL_TEXTURE_2D,output);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,output,0);
    if(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)!=GL_FRAMEBUFFER_COMPLETE_EXT)return 2;
    glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
    glViewport(0,0,width,height);glDisable(GL_SCISSOR_TEST);glDisable(GL_BLEND);glDisable(GL_LIGHTING);
    glDisable(GL_COLOR_LOGIC_OP);glDisable(GL_ALPHA_TEST);glDisable(GL_DEPTH_TEST);glDisable(GL_STENCIL_TEST);glDisable(GL_FOG);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,imported);glEnable(GL_TEXTURE_RECTANGLE_ARB);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    glBegin(GL_QUADS);glTexCoord2f(0,0);glVertex2f(-1,-1);glTexCoord2f(width,0);glVertex2f(1,-1);
    glTexCoord2f(width,height);glVertex2f(1,1);glTexCoord2f(0,height);glVertex2f(-1,1);glEnd();
    unsigned char inside[4]={0},outside[4]={0};
    glReadPixels(12,14,1,1,GL_RGBA,GL_UNSIGNED_BYTE,inside);
    glReadPixels(6,14,1,1,GL_RGBA,GL_UNSIGNED_BYTE,outside);GLenum error=glGetError();
    int good=!error && inside[0]==(0x33&((command.color>>16)&255)) && inside[1]==(0x66&((command.color>>8)&255)) &&
        inside[2]==(0xcc&(command.color&255)) && !inside[3] && !memcmp(inside,direct,4) &&
        outside[0]==0x33 && outside[1]==0x66 && outside[2]==0xcc && outside[3]==0xff;
    fprintf(stderr,"%s actual native GPU fill input=%08x RGBA=%u,%u,%u,%u outside=%u,%u,%u,%u error=%x\n",good?"PASS":"FAIL",
        command.color,inside[0],inside[1],inside[2],inside[3],outside[0],outside[1],outside[2],outside[3],error);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&fbo);
    glDeleteTextures(1,&imported);glDeleteTextures(1,&output);return good?0:1;
}
@interface VIMFillView : NSOpenGLView
@property BOOL measured;
@end
@implementation VIMFillView
- (void)finish:(NSTimer *)timer { (void)timer;exit(outcome); }
- (void)drawRect:(NSRect)rect
{
    (void)rect;if(self.measured)return;self.measured=YES;
    [self performSelector:@selector(measure) withObject:nil afterDelay:0];
}
- (void)measure
{
    [self.openGLContext makeCurrentContext];
    NSRect backing=[self convertRectToBacking:self.bounds];
    glViewport(0,0,backing.size.width,backing.size.height);
    glClearColor(.2,.4,.8,1);glClear(GL_COLOR_BUFFER_BIT);glFinish();
    uint32_t ids[3]={0};CGLError ce=CGLGetSurface(self.openGLContext.CGLContextObj,&ids[0],&ids[1],&ids[2]);
    fprintf(stderr,"renderer=%s own surface=%x CGL=%d error=%x\n",glGetString(GL_RENDERER),ids[2],ce,glGetError());
    IOCFPlugInInterface **plugin=NULL;IOGraphicsAcceleratorInterface **ga=NULL;
    SInt32 score=0;io_service_t fb=CGDisplayIOServicePort(CGMainDisplayID());
    if(getenv("VIM_2D_RAW_NATIVE")) {
        outcome=nativeFill(fb,self.openGLContext.CGLContextObj,ids[2],backing.size.width,backing.size.height);
        [NSTimer scheduledTimerWithTimeInterval:1 target:self selector:@selector(finish:) userInfo:nil repeats:NO];return;
    }
    io_service_t accelerator=0;UInt32 index=0;
    IOReturn found=fb?IOAccelFindAccelerator(fb,&accelerator,&index):kIOReturnNotFound;
    fprintf(stderr,"find native accelerator=%x framebuffer-index=%u\n",found,index);
    if(!found && getenv("VIM_2D_OPEN_DIAGNOSTIC")) {
        io_connect_t connection=0;IOReturn opened=IOServiceOpen(accelerator,mach_task_self(),2,&connection);
        fprintf(stderr,"direct native 2D user-client open=%x\n",opened);
        if(connection)IOServiceClose(connection);
    }
    if(accelerator)IOObjectRelease(accelerator);
    IOReturn e=ce || !ids[2] || !fb?kIOReturnNotReady:
        IOCreatePlugInInterfaceForService(fb,kIOGraphicsAcceleratorTypeID,kIOCFPlugInInterfaceID,&plugin,&score);
    fprintf(stderr,"create accelerator plugin=%x score=%d\n",e,score);
    if(!e) {
        HRESULT query=(*plugin)->QueryInterface(plugin,CFUUIDGetUUIDBytes(kIOGraphicsAcceleratorInterfaceID),(LPVOID *)&ga);
        if(query || !ga)e=kIOReturnUnsupported;
        fprintf(stderr,"query graphics interface=%x\n",query);
    }
    IOBlitSurface surface={0};int allocated=0;
    surface.pixelFormat=kIO32ARGBPixelFormat;
    surface.size=(IOBlitRectangle){0,0,(SInt32)backing.size.width,(SInt32)backing.size.height};
    if(!e) { e=(*ga)->AllocateSurface(ga,kIOBlitHasCGSSurface|kIOBlitFixedSource,&surface,(void *)(uintptr_t)ids[2]);allocated=!e; }
    fprintf(stderr,"allocate own surface=%x\n",e);
    IOBlitterPtr fill=NULL;
    if(!e)e=(*ga)->SetDestination(ga,kIOBlitSurfaceDestination,&surface);
    fprintf(stderr,"select own surface destination=%x\n",e);
    if(!e)e=(*ga)->GetBlitter(ga,0,kIOBlitTypeRects|kIOBlitCopyOperation,kIOBlitSourceSolid,&fill);
    fprintf(stderr,"get solid-copy blitter=%x available=%d\n",e,fill!=NULL);
    if(!e && !fill)e=kIOReturnUnsupported;
    vm_address_t address=0;uint32_t before=0;
    if(!e)e=(*ga)->LockSurface(ga,0,&surface,&address);
    if(!e && address && surface.rowBytes>=(unsigned)backing.size.width*4) {
        before=pixel(address,surface.rowBytes,6,14);
        IOOptionBits flags=0;e=(*ga)->UnlockSurface(ga,0,&surface,&flags);
    } else if(!e)e=kIOReturnNotReadable;
    IOBlitRectangles rectangles={0};rectangles.count=1;rectangles.rects[0]=(IOBlitRectangle){7,9,13,11};
    if(!e)e=fill(ga,0,kIOBlitTypeRects|kIOBlitCopyOperation,kIOBlitSourceSolid,&rectangles.operation,(void *)(uintptr_t)0x7f123456);
    fprintf(stderr,"fill solid 7f123456 at 7,9 13x11=%x\n",e);
    if(!e)e=(*ga)->WaitComplete(ga,kIOBlitWaitContext);
    address=0;if(!e)e=(*ga)->LockSurface(ga,0,&surface,&address);
    if(!e && address) {
        uint32_t a=pixel(address,surface.rowBytes,7,9),b=pixel(address,surface.rowBytes,19,19);
        uint32_t outside=pixel(address,surface.rowBytes,6,14),right=pixel(address,surface.rowBytes,20,14);
        int good=a==(before&0x00123456) && b==a && outside==before && right==before;
        fprintf(stderr,"%s native 2D raw fill bounds: corners=%08x,%08x outside=%08x,%08x before=%08x stride=%u\n",
            good?"PASS":"FAIL",a,b,outside,right,before,surface.rowBytes);
        if(getenv("VIM_2D_FILL_GUARDS")) {
            rectangles.rects[0]=(IOBlitRectangle){-3,-2,5,4};
            IOReturn clipped=fill(ga,0,0,kIOBlitSourceSolid,&rectangles.operation,(void *)(uintptr_t)0x80445566);
            int clip=!clipped && pixel(address,surface.rowBytes,0,0)==(before&0x00445566) &&
                pixel(address,surface.rowBytes,1,1)==(before&0x00445566) &&
                pixel(address,surface.rowBytes,2,1)==before && pixel(address,surface.rowBytes,1,2)==before;
            fprintf(stderr,"%s signed rectangle clips to backing without changing neighboring pixels error=%x\n",clip?"PASS":"FAIL",clipped);
            good=clip && good;
            rectangles.rects[0]=(IOBlitRectangle){INT32_MAX,INT32_MAX,INT32_MAX,INT32_MAX};
            IOReturn outside=fill(ga,0,0,kIOBlitSourceSolid,&rectangles.operation,(void *)(uintptr_t)0xffffffffu);
            int nooverflow=!outside && pixel(address,surface.rowBytes,6,14)==before;
            fprintf(stderr,"%s out-of-bounds coordinate addition does not overflow error=%x\n",nooverflow?"PASS":"FAIL",outside);
            good=nooverflow && good;
            struct { IOBlitOperation operation;IOItemCount count;IOBlitRectangle rects[2]; } batch={0};
            batch.count=2;batch.rects[0]=(IOBlitRectangle){32,16,4,2};batch.rects[1]=(IOBlitRectangle){40,16,-1,2};
            IOReturn rejected=fill(ga,0,0,kIOBlitSourceSolid,&batch.operation,(void *)(uintptr_t)0xffffffffu);
            int atomic=rejected==kIOReturnBadArgument && pixel(address,surface.rowBytes,32,16)==before;
            fprintf(stderr,"%s malformed later rectangle rejects the whole batch before any write error=%x\n",atomic?"PASS":"FAIL",rejected);
            good=atomic && good;
            batch.rects[1].width=4;
            IOReturn white=fill(ga,0,0,kIOBlitSourceSolid,&batch.operation,(void *)(uintptr_t)0xffffffffu);
            int copied=!white && pixel(address,surface.rowBytes,32,16)==(before&0x00ffffffu) &&
                pixel(address,surface.rowBytes,40,16)==(before&0x00ffffffu) && pixel(address,surface.rowBytes,36,16)==before;
            fprintf(stderr,"%s white mask keeps RGB, clears alpha and preserves the rectangle gap error=%x\n",copied?"PASS":"FAIL",white);
            good=copied && good;
        }
        IOOptionBits flags=0;IOReturn unlock=(*ga)->UnlockSurface(ga,0,&surface,&flags);
        outcome=good && !unlock?0:1;
    }
    fprintf(stderr,"fill probe final IOReturn=%x\n",e);
    if(allocated)(*ga)->FreeSurface(ga,0,&surface);
    if(ga)(*ga)->Release(ga);
    if(plugin)IODestroyPlugInInterface(plugin);
    [NSTimer scheduledTimerWithTimeInterval:1 target:self selector:@selector(finish:) userInfo:nil repeats:NO];
}
@end
int main(void)
{
    @autoreleasepool {
        NSApplication *app=[NSApplication sharedApplication];[app setActivationPolicy:NSApplicationActivationPolicyRegular];
        NSOpenGLPixelFormatAttribute attrs[]={NSOpenGLPFAAccelerated,NSOpenGLPFANoRecovery,
            NSOpenGLPFAColorSize,24,NSOpenGLPFAAlphaSize,8,0};
        NSOpenGLPixelFormat *format=[[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];if(!format)return 2;
        NSWindow *window=[[NSWindow alloc] initWithContentRect:NSMakeRect(180,180,96,64)
            styleMask:NSTitledWindowMask|NSClosableWindowMask backing:NSBackingStoreBuffered defer:NO];
        [window setTitle:@"Native accelerator rectangle-fill contract"];
        VIMFillView *view=[[VIMFillView alloc] initWithFrame:NSMakeRect(0,0,96,64) pixelFormat:format];
        [window setContentView:view];[window makeKeyAndOrderFront:nil];[app activateIgnoringOtherApps:YES];[app run];
    }
    return outcome;
}
