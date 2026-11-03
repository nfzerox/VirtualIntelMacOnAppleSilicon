/* Real Intel private live-import contract across shared GL contexts. Only
 * this program's own window/texture/FBO are touched. Producer flush/finish
 * and consumer rebind follow Apple's documented IOSurface ordering rules;
 * no CPU screenshot, framebuffer read, global preference or app exception. */
#import <Cocoa/Cocoa.h>
#import <OpenGL/CGLContext.h>
#import <OpenGL/gl.h>
#import <OpenGL/glext.h>
#include <dlfcn.h>
extern CGLError CGLGetSurface(CGLContextObj,uint32_t *,uint32_t *,uint32_t *);
static unsigned failures;
static void VIMCheck(int good,const char *label)
{ fprintf(stderr,"%s %s\n",good?"PASS":"FAIL",label);failures+=!good; }
static void paint(int w,int h,float r,float g,float b)
{
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDrawBuffer(GL_FRONT);glReadBuffer(GL_FRONT);
    glViewport(0,0,w,h);glDisable(GL_SCISSOR_TEST);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glClearColor(r,g,b,1);glClear(GL_COLOR_BUFFER_BIT);
}
static void sample(CGLContextObj consumer,GLuint texture,unsigned r,unsigned g,unsigned b,const char *label)
{
    CGLSetCurrentContext(consumer);
    GLuint output=0,fbo=0;glGenTextures(1,&output);glBindTexture(GL_TEXTURE_2D,output);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,output,0);
    VIMCheck(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT,"shared consumer FBO complete");
    glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
    glViewport(0,0,1,1);glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glDisable(GL_SCISSOR_TEST);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);glEnable(GL_TEXTURE_RECTANGLE_ARB);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    glBegin(GL_QUADS);glTexCoord2f(16,16);glVertex2f(-1,-1);
    glVertex2f(1,-1);glVertex2f(1,1);glVertex2f(-1,1);glEnd();
    unsigned char p[4]={0};glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);GLenum error=glGetError();
    fprintf(stderr,"shared RGBA=%u,%u,%u,%u expected=%u,%u,%u,255 error=%x\n",p[0],p[1],p[2],p[3],r,g,b,error);
    VIMCheck(!error && p[0]==r && p[1]==g && p[2]==b && p[3]==255,label);
    glDisable(GL_TEXTURE_RECTANGLE_ARB);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);
    glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&output);
}
@interface VIMSharedImportView:NSOpenGLView { BOOL _measured; }
@end
@implementation VIMSharedImportView
- (void)drawRect:(NSRect)dirty
{
    (void)dirty;if(_measured)return;_measured=YES;[self.openGLContext makeCurrentContext];
    CGLContextObj producer=self.openGLContext.CGLContextObj;
    const char *renderer=(const char *)glGetString(GL_RENDERER);
    fprintf(stderr,"renderer=%s\n",renderer?renderer:"none");if(!renderer||!strstr(renderer,"Intel"))exit(2);
    CGLContextObj consumer=NULL;CGLError error=CGLCreateContext(CGLGetPixelFormat(producer),producer,&consumer);
    if(error || !consumer)exit(2);
    CGLSetCurrentContext(producer);
    uint32_t ids[3]={0};if(CGLGetSurface(producer,ids,ids+1,ids+2)||!ids[2])exit(2);
    NSRect bounds=[self convertRectToBacking:self.bounds];int w=bounds.size.width,h=bounds.size.height;
    char path[4096];const char *resources=getenv("GL_RESOURCES");
    snprintf(path,sizeof(path),"%s/GLEngine.bundle/GLEngine",resources?resources:"/System/Library/Frameworks/OpenGL.framework/Resources");
    void *engine=dlopen(path,RTLD_NOW|RTLD_LOCAL);
    CGLError (*set)(GLIContext,unsigned,const GLint *)=engine?dlsym(engine,"gliSetInteger"):NULL;
    if(!set)exit(2);
    for(unsigned op=0;op<2;++op) {
        CGLSetCurrentContext(producer);paint(w,h,0,0,1);glFlush();
        GLuint texture=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        GLint record[]={ids[2],GL_TEXTURE_RECTANGLE_ARB,GL_RGBA,w,h,0,GL_FRONT,0};
        unsigned key=op?0x3e7:0x3e5;fprintf(stderr,"shared private import operation=%x\n",key);
        VIMCheck(!set(producer->rend,key,record),"initial shared private import");glFlush();
        sample(consumer,texture,0,0,255,"shared initial blue");
        CGLSetCurrentContext(producer);paint(w,h,0,1,1);glFlush();
        sample(consumer,texture,0,255,255,"producer flush updates retained alias in another context");
        CGLSetCurrentContext(producer);paint(w,h,1,1,0);glFinish();
        sample(consumer,texture,255,255,0,"producer finish updates retained alias in another context");
        CGLSetCurrentContext(producer);glDeleteTextures(1,&texture);
    }
    CGLSetCurrentContext(NULL);CGLDestroyContext(consumer);CGLSetCurrentContext(producer);
    fprintf(stderr,"shared-current-import failures=%u\n",failures);exit(failures?1:0);
}
@end
int main(void)
{
    @autoreleasepool {
        [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        NSOpenGLPixelFormatAttribute attrs[]={NSOpenGLPFAAccelerated,NSOpenGLPFANoRecovery,
            (NSOpenGLPixelFormatAttribute)kCGLPFASupportsAutomaticGraphicsSwitching,NSOpenGLPFAColorSize,24,NSOpenGLPFAAlphaSize,8,0};
        NSOpenGLPixelFormat *format=[[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];if(!format)return 2;
        NSWindow *window=[[NSWindow alloc] initWithContentRect:NSMakeRect(180,180,96,64)
            styleMask:NSTitledWindowMask backing:NSBackingStoreBuffered defer:NO];
        window.title=@"Owned shared-context live import";
        VIMSharedImportView *view=[[VIMSharedImportView alloc] initWithFrame:NSMakeRect(0,0,96,64) pixelFormat:format];
        window.contentView=view;[window makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES];[NSApp run];
    }
    return 2;
}
