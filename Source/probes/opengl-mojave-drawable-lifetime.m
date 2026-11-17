/* Finite native/guest comparison of two real window surfaces attached to one
 * context. Neither original view redraws after the shared context switches.
 * A context switch must not erase the previously published window surface.
 */
#import <Cocoa/Cocoa.h>
#import <OpenGL/OpenGL.h>
#import <OpenGL/gl.h>
#include <dlfcn.h>
#include <unistd.h>
extern CGLError CGLGetSurface(CGLContextObj,uint32_t *,uint32_t *,uint32_t *);
extern CGLError CGLSetSurface(CGLContextObj,uint32_t,uint32_t,uint32_t);
static NSOpenGLPixelFormat *format;
static uint32_t surfaces[2][3];
static unsigned ready,failures;
static BOOL partialRedraw,readbackOnly;
static void Check(int okay,const char *label)
{ fprintf(stderr,"%s %s\n",okay?"PASS":"FAIL",label);if(!okay)failures++; }
static int Binding(unsigned index,const char *label)
{
    int (*query)(int,int,unsigned,int *,int *,int *)=dlsym(RTLD_DEFAULT,"SLSGetSurfaceBinding");
    int mode=-1,first=-1,second=-1;
    int error=query?query(surfaces[index][0],surfaces[index][1],surfaces[index][2],&mode,&first,&second):-1;
    fprintf(stderr,"%s surface=%x window=%u error=%d mode=%d first=%x second=%x\n",
        label,surfaces[index][2],surfaces[index][1],error,mode,first,second);
    Check(!error,label);return mode;
}
static void CheckWindowColor(unsigned index,unsigned red,unsigned blue)
{
    CGImageRef image=CGWindowListCreateImage(CGRectNull,kCGWindowListOptionIncludingWindow,
        surfaces[index][1],kCGWindowImageBoundsIgnoreFraming);
    unsigned char pixel[4]={0};CGColorSpaceRef space=CGColorSpaceCreateDeviceRGB();
    CGContextRef context=CGBitmapContextCreate(pixel,1,1,8,4,space,
        kCGImageAlphaPremultipliedLast|kCGBitmapByteOrder32Big);
    if(image && context){
        size_t w=CGImageGetWidth(image),h=CGImageGetHeight(image);
        CGContextDrawImage(context,CGRectMake(-(CGFloat)(w/2),-(CGFloat)(h/2),w,h),image);
    }
    fprintf(stderr,"window=%u captured center RGBA=%u,%u,%u,%u\n",surfaces[index][1],
        pixel[0],pixel[1],pixel[2],pixel[3]);
    // This is a color-managed window capture, not raw framebuffer readback.
    // Allow its small profile conversion; a lost/blank backing is nowhere near
    // either expected opaque primary color.
    Check(image && context && abs((int)pixel[0]-(int)red)<=4 && pixel[1]<=4 &&
        abs((int)pixel[2]-(int)blue)<=4 && pixel[3]==255,"published window color survives switch and context destruction");
    if(context)CGContextRelease(context);CGColorSpaceRelease(space);if(image)CGImageRelease(image);
}
static void RunComparison(void)
{
    CGLContextObj context=NULL;
    Check(!CGLCreateContext(format.CGLPixelFormatObj,NULL,&context),"create shared drawing context");
    if(!context)exit(2);
    Check(!CGLSetCurrentContext(context),"make shared drawing context current");
    fprintf(stderr,"renderer=%s\n",glGetString(GL_RENDERER));
    Check(!CGLSetSurface(context,surfaces[0][0],surfaces[0][1],surfaces[0][2]),"attach first window");
    Check(!CGLUpdateContext(context),"update first window");
    glDrawBuffer(GL_BACK);glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    Check(!CGLFlushDrawable(context),"publish first window red");
    int before=Binding(0,"first window before switch");
    Check(before>0,"first window has published backing");
    Check(!CGLSetSurface(context,surfaces[1][0],surfaces[1][1],surfaces[1][2]),"attach second window");
    Check(!CGLUpdateContext(context),"update second window");
    glDrawBuffer(GL_BACK);glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT);
    Check(!CGLFlushDrawable(context),"publish second window blue");
    int after=Binding(0,"first window after switch");
    Check(after>0,"switch retains first window backing");
    Check(Binding(1,"second window after switch")>0,"second window has published backing");
    Check(!CGLSetSurface(context,surfaces[0][0],surfaces[0][1],surfaces[0][2]),"reattach first window without redrawing");
    Check(!CGLUpdateContext(context),"update reattached first window");
    Check(Binding(1,"second window after return")>0,"return retains second window backing");
    if(partialRedraw){
        // A front-buffer update must retain previously published pixels
        // outside its scissor after returning from another surface. This is
        // distinct from retaining a CGS binding without drawing again.
        glDrawBuffer(GL_FRONT);glEnable(GL_SCISSOR_TEST);glScissor(0,0,16,16);
        glClearColor(0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);glFinish();
        glDisable(GL_SCISSOR_TEST);
        unsigned char pixel[4]={0};glReadBuffer(GL_FRONT);
        glReadPixels(120,80,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
        fprintf(stderr,"reattached front center RGBA=%u,%u,%u,%u\n",pixel[0],pixel[1],pixel[2],pixel[3]);
        Check(pixel[0]==255&&!pixel[1]&&!pixel[2]&&pixel[3]==255,
            "partial front redraw retains previous pixels outside scissor");
        glReadPixels(4,4,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
        Check(!pixel[0]&&pixel[1]==255&&!pixel[2]&&pixel[3]==255,
            "partial front redraw publishes the new scissored pixels");
    }
    Check(!glGetError(),"no GL error");
    Check(!CGLSetCurrentContext(NULL),"clear shared context");
    Check(!CGLDestroyContext(context),"destroy shared context");
    Check(Binding(0,"first window after context destruction")>0,"first backing outlives drawing context");
    Check(Binding(1,"second window after context destruction")>0,"second backing outlives drawing context");
    fprintf(stderr,"drawable lifetime failures=%u\n",failures);
    if(readbackOnly)exit(failures?1:0);
    // Keep these owned windows briefly available for a normal window capture.
    // The checks above do not rely on a repaint or on this observation delay.
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,2*NSEC_PER_SEC),dispatch_get_main_queue(),^{
        CheckWindowColor(0,255,0);CheckWindowColor(1,0,255);
        fprintf(stderr,"drawable lifetime final failures=%u\n",failures);
        exit(failures?1:0);
    });
}
@interface LifetimeView:NSOpenGLView { BOOL measured; unsigned index; }
- (id)initWithIndex:(unsigned)value;
@end
@implementation LifetimeView
- (id)initWithIndex:(unsigned)value
{ if((self=[super initWithFrame:NSMakeRect(0,0,240,160) pixelFormat:format]))index=value;return self; }
- (void)drawRect:(NSRect)dirty
{
    (void)dirty;if(measured)return;measured=YES;
    [self.openGLContext makeCurrentContext];
    glClearColor(0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);[self.openGLContext flushBuffer];
    Check(!CGLGetSurface(self.openGLContext.CGLContextObj,surfaces[index],surfaces[index]+1,surfaces[index]+2),"read actual view surface");
    Check(surfaces[index][2]!=0,"view surface exists");
    if(++ready==2)dispatch_async(dispatch_get_main_queue(),^{RunComparison();});
}
@end
int main(int argc,char **argv)
{
    @autoreleasepool {
        if(argc==2&&!strcmp(argv[1],"--partial-redraw"))partialRedraw=YES;
        else if(argc==2&&!strcmp(argv[1],"--partial-readback"))partialRedraw=readbackOnly=YES;
        else if(argc!=1)return 2;
        alarm(20);[NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        NSOpenGLPixelFormatAttribute attrs[]={NSOpenGLPFAAccelerated,NSOpenGLPFANoRecovery,
            NSOpenGLPFADoubleBuffer,NSOpenGLPFAColorSize,24,NSOpenGLPFAAlphaSize,8,
            (NSOpenGLPixelFormatAttribute)kCGLPFASupportsAutomaticGraphicsSwitching,
            NSOpenGLPFARendererID,0x1024500,0};
        if(!getenv("VIM_REFERENCE_INTEL"))attrs[8]=0;
        format=[[NSOpenGLPixelFormat alloc]initWithAttributes:attrs];if(!format)return 2;
        for(unsigned i=0;i<2;i++){
            NSWindow *window=[[NSWindow alloc]initWithContentRect:NSMakeRect(200+280*i,280,240,160)
                styleMask:NSTitledWindowMask backing:NSBackingStoreBuffered defer:NO];
            window.title=[NSString stringWithFormat:@"Drawable lifetime %u",i+1];
            window.contentView=[[[LifetimeView alloc]initWithIndex:i]autorelease];
            [window makeKeyAndOrderFront:nil];
        }
        [NSApp activateIgnoringOtherApps:YES];[NSApp run];return 2;
    }
}
