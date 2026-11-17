/* Compare real AppKit publication boundaries with native Intel GLEngine.
 * Trace this finite, owned window with opengl-mojave-front-publication.d.
 * No injection, global environment or system framework changes are used. */
#import <Cocoa/Cocoa.h>
#import <OpenGL/gl.h>
#import <OpenGL/glext.h>
#include <signal.h>
#include <unistd.h>

__attribute__((noinline,used)) void VIMFrontPhase(unsigned phase)
{ __asm__ volatile("" : : "r"(phase) : "memory"); }
static unsigned failures;
static void Phase(unsigned phase,const char *name)
{ VIMFrontPhase(phase);fprintf(stderr,"phase=%u %s\n",phase,name); }
static void EndPhase(void)
{
    GLenum error=glGetError();if(error){++failures;fprintf(stderr,"FAIL GL error=%x\n",error);}
    VIMFrontPhase(0);
}
static void CheckFront(void)
{
    unsigned char pixel[4]={0};glReadBuffer(GL_FRONT);
    glReadPixels(8,8,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);glReadBuffer(GL_BACK);
    BOOL correct=abs((int)pixel[0]-102)<=1 && abs((int)pixel[1]-128)<=1 &&
        abs((int)pixel[2]-153)<=1 && pixel[3]==255;
    fprintf(stderr,"%s front RGBA=%u,%u,%u,%u\n",correct?"PASS":"FAIL",pixel[0],pixel[1],pixel[2],pixel[3]);
    if(!correct)++failures;
}
@interface FrontProbe : NSOpenGLView { BOOL _measured; }
@end
@implementation FrontProbe
- (void)drawRect:(NSRect)dirty
{
    (void)dirty;if(_measured)return;_measured=YES;
    [self.openGLContext makeCurrentContext];
    GLint interval=0;[self.openGLContext setValues:&interval forParameter:NSOpenGLCPSwapInterval];
    NSRect backing=[self convertRectToBacking:self.bounds];
    glViewport(0,0,backing.size.width,backing.size.height);
    fprintf(stderr,"renderer=%s double-buffered layered window\n",glGetString(GL_RENDERER));
    glDrawBuffer(GL_BACK);glClearColor(.2,.3,.4,1);glClear(GL_COLOR_BUFFER_BIT);
    [self.openGLContext flushBuffer];

    if(getenv("VIM_FRONT_SHARED_LIST")){
        // A shared context can compile a front selection without executing it.
        // The parent must not mistake its own back binding for proof that the
        // later CallList/Flush cannot publish front content.
        NSOpenGLContext *compiler=[[NSOpenGLContext alloc]initWithFormat:self.pixelFormat shareContext:self.openGLContext];
        if(!compiler)exit(2);[compiler makeCurrentContext];
        GLuint shared=glGenLists(1);if(!shared)exit(2);
        glNewList(shared,GL_COMPILE);glDrawBuffer(GL_FRONT);glClearColor(.4,.5,.6,1);
        glClear(GL_COLOR_BUFFER_BIT);glDrawBuffer(GL_BACK);glEndList();
        [self.openGLContext makeCurrentContext];
        Phase(16,"front list compiled by a different sharing context, Flush");
        glCallList(shared);glFlush();CheckFront();EndPhase();glDeleteLists(shared,1);
        fprintf(stderr,"shared front publication probe failures=%u\n",failures);exit(failures?1:0);
    }

    Phase(1,"back clear then Flush");
    glDrawBuffer(GL_BACK);glClearColor(.3,.4,.5,1);glClear(GL_COLOR_BUFFER_BIT);glFlush();EndPhase();
    Phase(2,"unchanged back Flush");glFlush();EndPhase();
    Phase(3,"back Swap");[self.openGLContext flushBuffer];EndPhase();
    Phase(4,"unchanged back Swap");[self.openGLContext flushBuffer];EndPhase();
    Phase(5,"select front without drawing then Flush");glDrawBuffer(GL_FRONT);glFlush();EndPhase();
    Phase(6,"unchanged front Flush");glFlush();EndPhase();
    Phase(7,"front clear, select back, then Flush");
    glClearColor(.4,.5,.6,1);glClear(GL_COLOR_BUFFER_BIT);glDrawBuffer(GL_BACK);glFlush();CheckFront();EndPhase();
    Phase(8,"unchanged back Flush after front publication");glFlush();EndPhase();
    Phase(9,"select front then back without drawing, Flush");
    glDrawBuffer(GL_FRONT);glDrawBuffer(GL_BACK);glFlush();EndPhase();
    Phase(10,"back Finish");glFinish();EndPhase();

    GLuint texture=0,fbo=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,16,16,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,texture,0);
    if(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)!=GL_FRAMEBUFFER_COMPLETE_EXT)++failures;
    Phase(11,"offscreen clear then Flush");
    glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);glClear(GL_COLOR_BUFFER_BIT);glFlush();EndPhase();
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);
    Phase(12,"front selection, bind offscreen, Flush");
    glDrawBuffer(GL_FRONT);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);glFlush();EndPhase();
    Phase(13,"return to window back after offscreen Flush");
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDrawBuffer(GL_BACK);glFlush();EndPhase();
    Phase(14,"front selection through attribute push/pop, Flush");
    glPushAttrib(GL_COLOR_BUFFER_BIT);glDrawBuffer(GL_FRONT);glPopAttrib();glFlush();EndPhase();
    Phase(15,"front selection inside display list ending on back, Flush");
    GLuint list=glGenLists(1);glNewList(list,GL_COMPILE);
    glDrawBuffer(GL_FRONT);glClear(GL_COLOR_BUFFER_BIT);glDrawBuffer(GL_BACK);glEndList();
    glCallList(list);glFlush();CheckFront();EndPhase();glDeleteLists(list,1);
    Phase(17,"Swap while an unrelated offscreen FBO remains bound");
    glDrawBuffer(GL_BACK);glClearColor(.6,.25,.75,1);glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
    glClearColor(.8,.7,.1,1);glClear(GL_COLOR_BUFFER_BIT);
    GLint viewport[4],after[4],draw=-1,read=-1,buffer=-1;
    glGetIntegerv(GL_VIEWPORT,viewport);
    [self.openGLContext flushBuffer];
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&draw);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&read);
    glGetIntegerv(GL_DRAW_BUFFER,&buffer);glGetIntegerv(GL_VIEWPORT,after);
    BOOL preserved=draw==(GLint)fbo && read==(GLint)fbo && buffer==GL_COLOR_ATTACHMENT0_EXT &&
        !memcmp(viewport,after,sizeof(viewport));
    fprintf(stderr,"%s Swap preserves offscreen read/draw binding, buffer and viewport\n",preserved?"PASS":"FAIL");
    if(!preserved)++failures;
    unsigned char pixel[4]={0};glReadPixels(8,8,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    BOOL offscreen=abs((int)pixel[0]-204)<=1 && abs((int)pixel[1]-179)<=1 &&
        abs((int)pixel[2]-26)<=1 && pixel[3]==255;
    fprintf(stderr,"%s offscreen RGBA=%u,%u,%u,%u\n",offscreen?"PASS":"FAIL",pixel[0],pixel[1],pixel[2],pixel[3]);
    if(!offscreen)++failures;
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glReadBuffer(GL_FRONT);
    glReadPixels(8,8,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);glReadBuffer(GL_BACK);
    // Real Mojave Intel ignores the window swap while this FBO is bound.
    // It preserves both the old front and the unconsumed newer back image.
    BOOL unchanged=abs((int)pixel[0]-102)<=1 && abs((int)pixel[1]-128)<=1 &&
        abs((int)pixel[2]-153)<=1 && pixel[3]==255;
    fprintf(stderr,"%s offscreen Swap retains window front RGBA=%u,%u,%u,%u\n",unchanged?"PASS":"FAIL",pixel[0],pixel[1],pixel[2],pixel[3]);
    if(!unchanged)++failures;EndPhase();
    Phase(18,"Swap after returning to the window consumes the retained back image");
    [self.openGLContext flushBuffer];glReadBuffer(GL_FRONT);
    glReadPixels(8,8,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);glReadBuffer(GL_BACK);
    BOOL swapped=abs((int)pixel[0]-153)<=1 && abs((int)pixel[1]-64)<=1 &&
        abs((int)pixel[2]-191)<=1 && pixel[3]==255;
    fprintf(stderr,"%s later window Swap RGBA=%u,%u,%u,%u\n",swapped?"PASS":"FAIL",pixel[0],pixel[1],pixel[2],pixel[3]);
    if(!swapped)++failures;EndPhase();
    glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&texture);
    fprintf(stderr,"front publication probe failures=%u\n",failures);exit(failures?1:0);
}
@end
int main(void)
{
    @autoreleasepool {
        [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        // Stop only our fixture so DTrace can attach as its real login user.
        // Tracing a sudo wrapper with -c would follow the wrapper, not its child.
        if(getenv("VIM_FRONT_TRACE_STOP"))raise(SIGSTOP);
        alarm(20);
        NSOpenGLPixelFormatAttribute attrs[]={NSOpenGLPFAAccelerated,NSOpenGLPFANoRecovery,
            NSOpenGLPFADoubleBuffer,NSOpenGLPFABackingStore,NSOpenGLPFAColorSize,24,NSOpenGLPFAAlphaSize,8,
            (NSOpenGLPixelFormatAttribute)kCGLPFASupportsAutomaticGraphicsSwitching,NSOpenGLPFARendererID,0x1024500,0};
        if(!getenv("VIM_REFERENCE_INTEL"))attrs[9]=0;
        NSOpenGLPixelFormat *format=[[NSOpenGLPixelFormat alloc]initWithAttributes:attrs];if(!format)return 2;
        NSWindow *window=[[NSWindow alloc]initWithContentRect:NSMakeRect(220,240,256,128)
            styleMask:NSTitledWindowMask|NSClosableWindowMask backing:NSBackingStoreBuffered defer:NO];
        window.title=@"Mojave front publication boundaries";
        FrontProbe *view=[[FrontProbe alloc]initWithFrame:[[window contentView] bounds] pixelFormat:format];
        view.wantsLayer=YES;view.wantsBestResolutionOpenGLSurface=YES;window.contentView=view;
        [window makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES];[NSApp run];return 2;
    }
}
