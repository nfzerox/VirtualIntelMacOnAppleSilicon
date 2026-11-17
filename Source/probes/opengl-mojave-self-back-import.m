/* Reproduce Core Image's actual Mojave toolbar request: import GL_BACK of
 * the current window into a texture, then consume it in an independent FBO.
 * Run unchanged against native Intel and the guest's installed renderer. */
#import <Cocoa/Cocoa.h>
#import <OpenGL/CGLContext.h>
#import <OpenGL/gl.h>
#import <OpenGL/glext.h>
#include <dlfcn.h>
#include <unistd.h>
extern CGLError CGLGetSurface(CGLContextObj,uint32_t *,uint32_t *,uint32_t *);
typedef CGLError (*SetInteger)(GLIContext,unsigned,const GLint *);
static int result=2;
@interface BackProbe : NSOpenGLView { BOOL _measured; }
@property BOOL measured;
@end
@implementation BackProbe
@synthesize measured=_measured;
- (void)drawRect:(NSRect)dirty
{
    (void)dirty;if(self.measured)return;self.measured=YES;
    [self.openGLContext makeCurrentContext];CGLContextObj context=self.openGLContext.CGLContextObj;
    NSRect backing=[self convertRectToBacking:self.bounds];int w=backing.size.width,h=backing.size.height;
    printf("renderer=%s dimensions=%dx%d\n",glGetString(GL_RENDERER),w,h);
    uint32_t ids[3]={0};CGLError error=CGLGetSurface(context,&ids[0],&ids[1],&ids[2]);
    glViewport(0,0,w,h);glDrawBuffer(GL_BACK);glDisable(GL_SCISSOR_TEST);
    glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);glScissor(0,h/2,w,h-h/2);glClearColor(1,0,0,1);
    glClear(GL_COLOR_BUFFER_BIT);glDisable(GL_SCISSOR_TEST);
    const char *resources=getenv("GL_RESOURCES");char path[4096];
    snprintf(path,sizeof(path),"%s/GLEngine.bundle/GLEngine",resources?resources:
        "/System/Library/Frameworks/OpenGL.framework/Resources");
    void *engine=dlopen(path,RTLD_NOW|RTLD_LOCAL);
    SetInteger set=engine?(SetInteger)dlsym(engine,"gliSetInteger"):NULL;
    GLuint source=0,output=0,fbo=0;glGenTextures(1,&source);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,source);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    GLint request[]={ids[2],GL_TEXTURE_RECTANGLE_ARB,GL_RGBA,w,h,0,GL_BACK,0};
    if(!error)error=set?set(context->rend,0x3e5,request):kCGLBadContext;
    printf("self import=%d surface=%u flags=%x\n",error,ids[2],GL_BACK);
    if(!error){
        glGenTextures(1,&output);glBindTexture(GL_TEXTURE_2D,output);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,output,0);
        GLenum status=glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);
        glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
        glBindTexture(GL_TEXTURE_RECTANGLE_ARB,source);glEnable(GL_TEXTURE_RECTANGLE_ARB);
        glDisable(GL_BLEND);glColor4f(1,1,1,1);
        glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();
        glBegin(GL_QUADS);
        glTexCoord2f(0,0);glVertex2f(-1,-1);glTexCoord2f(w,0);glVertex2f(1,-1);
        glTexCoord2f(w,h);glVertex2f(1,1);glTexCoord2f(0,h);glVertex2f(-1,1);glEnd();
        unsigned char bottom[4]={0},top[4]={0};
        glReadPixels(w/2,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,bottom);
        glReadPixels(w/2,h-1,1,1,GL_RGBA,GL_UNSIGNED_BYTE,top);
        GLenum gl_error=glGetError();
        printf("FBO=%x bottom=%u,%u,%u,%u top=%u,%u,%u,%u error=%x\n",status,
            bottom[0],bottom[1],bottom[2],bottom[3],top[0],top[1],top[2],top[3],gl_error);
        // Both native Mojave Intel ABIs expose the surface texture in the
        // opposite row order from glReadPixels on the window's back buffer.
        result=status!=GL_FRAMEBUFFER_COMPLETE_EXT || gl_error ||
            bottom[0]!=255 || bottom[1] || bottom[2] || bottom[3]!=255 ||
            top[0] || top[1] || top[2]!=255 || top[3]!=255;
    }
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);
    if(fbo)glDeleteFramebuffersEXT(1,&fbo);if(output)glDeleteTextures(1,&output);if(source)glDeleteTextures(1,&source);
    if(!result && getenv("VIM_GL_SWAP_RECTANGLE")) {
        [self.openGLContext flushBuffer];
        GLint rectangle[]={0,0,w/2,h},queried[4]={0},enabled=0;
        CGLError configured=CGLSetParameter(context,kCGLCPSwapRectangle,rectangle);
        CGLError activated=CGLEnable(context,kCGLCESwapRectangle);
        CGLError read_rect=CGLGetParameter(context,kCGLCPSwapRectangle,queried);
        CGLError read_enable=CGLIsEnabled(context,kCGLCESwapRectangle,&enabled);
        glDrawBuffer(GL_BACK);glDisable(GL_SCISSOR_TEST);glClearColor(0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);
        [self.openGLContext flushBuffer];glReadBuffer(GL_FRONT);
        unsigned char left[4]={0},right[4]={0};
        glReadPixels(w/4,h/4,1,1,GL_RGBA,GL_UNSIGNED_BYTE,left);
        glReadPixels(w*3/4,h/4,1,1,GL_RGBA,GL_UNSIGNED_BYTE,right);
        GLenum gl_error=glGetError();
        printf("swap rect set=%d enable=%d query=%d/%d enabled=%d rect=%d,%d,%d,%d left=%u,%u,%u,%u right=%u,%u,%u,%u error=%x\n",
            configured,activated,read_rect,read_enable,enabled,queried[0],queried[1],queried[2],queried[3],
            left[0],left[1],left[2],left[3],right[0],right[1],right[2],right[3],gl_error);
        result=configured || activated || read_rect || read_enable || !enabled ||
            memcmp(rectangle,queried,sizeof(rectangle)) || gl_error ||
            left[0] || left[1]!=255 || left[2] || left[3]!=255 ||
            // Native Mojave Intel front readback sees the whole green back
            // buffer here, despite accepting the half-width swap hint. This
            // does not establish the compositor's eventual damage region.
            right[0] || right[1]!=255 || right[2] || right[3]!=255;
        CGLDisable(context,kCGLCESwapRectangle);
    }
    // stop: can run before the initial run-loop entry or leave it waiting
    // for another event, causing the finite probe's alarm to report a false
    // graphics crash. Readback and cleanup above already completed.
    exit(result);
}
@end
int main(void)
{
    alarm(15);@autoreleasepool {
        [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        NSOpenGLPixelFormatAttribute attributes[]={NSOpenGLPFAAccelerated,NSOpenGLPFANoRecovery,
            NSOpenGLPFADoubleBuffer,NSOpenGLPFABackingStore,NSOpenGLPFAColorSize,24,NSOpenGLPFAAlphaSize,8,
            (NSOpenGLPixelFormatAttribute)kCGLPFASupportsAutomaticGraphicsSwitching,NSOpenGLPFARendererID,0x1024500,0};
        if(!getenv("VIM_REFERENCE_INTEL"))attributes[9]=0;
        NSOpenGLPixelFormat *format=[[NSOpenGLPixelFormat alloc]initWithAttributes:attributes];if(!format)return 2;
        NSWindow *window=[[NSWindow alloc]initWithContentRect:NSMakeRect(200,200,256,128)
            styleMask:NSTitledWindowMask|NSClosableWindowMask backing:NSBackingStoreBuffered defer:NO];
        [window setTitle:@"Mojave GL back-buffer self import"];
        BackProbe *view=[[BackProbe alloc]initWithFrame:[[window contentView] bounds] pixelFormat:format];
        view.wantsBestResolutionOpenGLSurface=YES;
        window.contentView=view;[window makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES];
        if(!view.measured)[NSApp run];return result;
    }
}
