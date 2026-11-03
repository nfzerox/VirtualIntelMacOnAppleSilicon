/* Compare actual CPU Quartz content as imported by accelerated GL. Only two
 * pixels underneath this program's own opaque window are sampled; no image
 * capture, primary drawing, global setting or system driver modification.
 * Supply the primary ID established by tracing the current reference boot:
 * VIM_PRIMARY_SOURCE=3 on the qualified NVIDIA Mac, 1 on the research VM.
 */
#import <Cocoa/Cocoa.h>
#import <OpenGL/CGLContext.h>
#import <OpenGL/gl.h>
#import <OpenGL/glext.h>
#include <stdint.h>
#include <dlfcn.h>
static int result=2;
@interface VIMCPUPatch : NSView
@end
@implementation VIMCPUPatch
- (BOOL)isOpaque { return YES; }
- (void)drawRect:(NSRect)rect
{
    (void)rect;NSRect bounds=self.bounds;
    [[NSColor redColor] setFill];NSRectFill(bounds);
    bounds.origin.y=NSMidY(bounds);bounds.size.height*=.5;
    [[NSColor blueColor] setFill];NSRectFill(bounds);
}
@end
@interface VIMPrimaryContract : NSObject
@property(retain) NSWindow *window;
@property(retain) NSOpenGLView *control;
@end
@implementation VIMPrimaryContract
- (void)measure:(NSTimer *)timer
{
    (void)timer;[self.control.openGLContext makeCurrentContext];
    CGLContextObj context=self.control.openGLContext.CGLContextObj;
    NSScreen *screen=self.window.screen;NSRect display=screen.frame;
    CGFloat scale=screen.backingScaleFactor;
    int width=display.size.width*scale,height=display.size.height*scale;
    uint32_t source=(uint32_t)strtoul(getenv("VIM_PRIMARY_SOURCE"),NULL,0);
    void *engine=dlopen("/System/Library/Frameworks/OpenGL.framework/Resources/GLEngine.bundle/GLEngine",RTLD_NOW|RTLD_LOCAL);
    CGLError (*import)(GLIContext,unsigned,const GLint *)=engine?(void *)dlsym(engine,"gliSetInteger"):NULL;
    if(!import || !source || width<1 || height<1 || width>8192 || height>8192)exit(2);
    GLuint texture=0,output=0,fbo=0;
    glActiveTexture(GL_TEXTURE0);glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
    GLint record[]={source,GL_TEXTURE_RECTANGLE_ARB,GL_RGBA,width,height,0,0x400,0};
    CGLError error=import(context->rend,0x3e5,record);
    fprintf(stderr,"CPU Quartz primary import=%d source=%x size=%dx%d renderer=%s\n",error,source,width,height,glGetString(GL_RENDERER));
    if(error)exit(2);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    glGenTextures(1,&output);glBindTexture(GL_TEXTURE_2D,output);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,output,0);
    if(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)!=GL_FRAMEBUFFER_COMPLETE_EXT)exit(2);
    glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
    glViewport(0,0,1,1);glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);glDisable(GL_ALPHA_TEST);glDisable(GL_LIGHTING);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);glEnable(GL_TEXTURE_RECTANGLE_ARB);
    int good=1;
    for(unsigned top=0;top<2;++top) {
        NSRect local=NSMakeRect(64,top?96:32,1,1);
        NSRect global=[self.window convertRectToScreen:local];
        int x=(NSMinX(global)-NSMinX(display))*scale;
        int y=(NSMaxY(display)-NSMinY(global))*scale;
        if(x<0 || y<0 || x>=width || y>=height)exit(2);
        glBegin(GL_QUADS);glTexCoord2f(x+.5,y+.5);glVertex2f(-1,-1);
        glVertex2f(1,-1);glVertex2f(1,1);glVertex2f(-1,1);glEnd();
        unsigned char p[4]={0};glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
        GLenum e=glGetError();
        // Native display color management changes calibrated RGB (the
        // NVIDIA reference's red is 255,42,26); compare the known patch's
        // identity and opaque alpha, not an assumed unmanaged profile.
        int pass=!e && p[3]==255 && p[1]<100 &&
            (top ? p[2]>200 && p[0]<100 : p[0]>200 && p[2]<100);
        fprintf(stderr,"%s CPU Quartz %s primary point=%d,%d RGBA=%u,%u,%u,%u error=%x\n",
            pass?"PASS":"FAIL",top?"upper blue":"lower red",x,y,p[0],p[1],p[2],p[3],e);
        good=pass && good;
    }
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&fbo);
    glDeleteTextures(1,&texture);glDeleteTextures(1,&output);dlclose(engine);
    result=good?0:1;exit(result);
}
@end
int main(void)
{
    if(!getenv("VIM_PRIMARY_SOURCE"))return 2;
    @autoreleasepool {
        NSApplication *app=[NSApplication sharedApplication];[app setActivationPolicy:NSApplicationActivationPolicyRegular];
        VIMPrimaryContract *probe=[VIMPrimaryContract new];
        probe.window=[[NSWindow alloc] initWithContentRect:NSMakeRect(400,220,128,128)
            styleMask:NSTitledWindowMask|NSClosableWindowMask backing:NSBackingStoreBuffered defer:NO];
        [probe.window setTitle:@"CPU Quartz primary contract"];
        // This fixture samples only its own opaque pixels. Keep that patch
        // above other ordinary windows so their colors cannot masquerade as
        // a backing-import failure on a busy desktop.
        [probe.window setLevel:NSFloatingWindowLevel];
        VIMCPUPatch *patch=[[VIMCPUPatch alloc] initWithFrame:NSMakeRect(0,0,128,128)];
        probe.window.contentView=patch;
        NSOpenGLPixelFormatAttribute attrs[]={NSOpenGLPFAAccelerated,NSOpenGLPFANoRecovery,
            NSOpenGLPFAColorSize,24,NSOpenGLPFAAlphaSize,8,0};
        NSOpenGLPixelFormat *format=[[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];if(!format)return 2;
        probe.control=[[NSOpenGLView alloc] initWithFrame:NSMakeRect(0,0,128,16) pixelFormat:format];
        [patch addSubview:probe.control];[probe.window makeKeyAndOrderFront:nil];[app activateIgnoringOtherApps:YES];
        [probe.window orderFrontRegardless];
        [probe.window display];[probe.window flushWindow];
        [probe.control.openGLContext makeCurrentContext];glClearColor(0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);
        [probe.control.openGLContext flushBuffer];
        if(getenv("VIM_PRIMARY_MANUAL")) {
            // On a busy TCG desktop, a timer does not prove WindowServer has
            // presented the patch. Let the operator/CUA verify it is visible
            // before taking the numeric sample instead of extending a sleep.
            NSButton *measure=[[NSButton alloc] initWithFrame:NSMakeRect(0,0,128,24)];
            measure.title=@"Measure";measure.bezelStyle=NSRoundedBezelStyle;
            measure.target=probe;measure.action=@selector(measure:);[patch addSubview:measure];
        } else [NSTimer scheduledTimerWithTimeInterval:2 target:probe selector:@selector(measure:) userInfo:nil repeats:NO];
        [app run];
    }
    return result;
}
