/* Ordinary (not type-5a/unflipped) Mavericks window -> private destination
 * texture. Same owned window/API sequence on physical Intel and the VM.
 * No WindowServer injection, CPU-cache deletion or system-binary patch. */
#import <Cocoa/Cocoa.h>
#import <OpenGL/CGLContext.h>
#import <OpenGL/gl.h>
#import <OpenGL/glext.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
extern CGLError CGLGetSurface(CGLContextObj,uint32_t *,uint32_t *,uint32_t *);
typedef CGLError (*SetInteger)(GLIContext,unsigned,const GLint *);
static int failures;
static int back_import;
static void VIMAssert(int okay,const char *name)
{fprintf(stderr,"%s %s\n",okay?"PASS":"FAIL",name);failures+=!okay;}
static void colorAt(const char *name,int y,unsigned r,unsigned g,unsigned b)
{
    unsigned char p[4]={0};glReadPixels(16,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
    GLenum error=glGetError();fprintf(stderr,"%s pixel=%u,%u,%u,%u error=%x\n",name,p[0],p[1],p[2],p[3],error);
    VIMAssert(!error && p[0]==r && p[1]==g && p[2]==b && p[3]==255,name);
}
static void color(const char *name,unsigned r,unsigned g,unsigned b)
{colorAt(name,16,r,g,b);}
static void sample(GLuint texture,GLuint output,int w,int h)
{
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,output);glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
    glViewport(0,0,32,32);glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glDisable(GL_SCISSOR_TEST);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);glEnable(GL_TEXTURE_RECTANGLE_ARB);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);glColor4f(1,1,1,1);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    glBegin(GL_QUADS);glTexCoord2f(0,0);glVertex2f(-1,-1);glTexCoord2f(w,0);glVertex2f(1,-1);
    glTexCoord2f(w,h);glVertex2f(1,1);glTexCoord2f(0,h);glVertex2f(-1,1);glEnd();
    glDisable(GL_TEXTURE_RECTANGLE_ARB);
}
static void produce(float r,float g,float b,int w,int h)
{
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDrawBuffer(back_import?GL_BACK:GL_FRONT);glReadBuffer(back_import?GL_BACK:GL_FRONT);
    glViewport(0,0,w,h);glDisable(GL_SCISSOR_TEST);glClearColor(r,g,b,1);glClear(GL_COLOR_BUFFER_BIT);
}
@interface VIMDestinationView:NSOpenGLView { BOOL _measured; }
@property BOOL measured;
@end
@implementation VIMDestinationView
@synthesize measured=_measured;
- (void)drawRect:(NSRect)dirty
{
    (void)dirty;if(self.measured)return;self.measured=YES;
    [self.openGLContext makeCurrentContext];CGLContextObj c=self.openGLContext.CGLContextObj;
    const char *renderer=(const char *)glGetString(GL_RENDERER);
    fprintf(stderr,"renderer=%s\n",renderer?renderer:"none");
    if(!renderer || !strstr(renderer,"Intel")){fputs("REFUSED non-Intel reference\n",stderr);exit(2);}
    NSRect size=[self convertRectToBacking:self.bounds];int w=size.size.width,h=size.size.height;
    uint32_t ids[3]={0};CGLError e=CGLGetSurface(c,ids,ids+1,ids+2);
    fprintf(stderr,"ordinary source=%x geometry=%dx%d CGLGetSurface=%d\n",ids[2],w,h,e);
    void *engine=dlopen("/System/Library/Frameworks/OpenGL.framework/Resources/GLEngine.bundle/GLEngine",RTLD_NOW|RTLD_LOCAL);
    SetInteger set=engine?(SetInteger)dlsym(engine,"gliSetInteger"):NULL;
    if(e || !ids[2] || !set)exit(2);
    GLuint texture=0,colorTexture=0,fbo=0;glGenTextures(1,&texture);glGenTextures(1,&colorTexture);
    glBindTexture(GL_TEXTURE_2D,colorTexture);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,32,32,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,colorTexture,0);
    VIMAssert(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT,"sample FBO complete");
    produce(0,0,1,w,h);glFinish();
    GLint record[]={ids[2],GL_TEXTURE_RECTANGLE_ARB,GL_RGBA,w,h,0,GL_BACK,0};
    unsigned import_key=back_import?0x3e5:0x3e7;
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    e=set(c->rend,import_key,record);VIMAssert(!e,"ordinary current-window import");
    sample(texture,fbo,w,h);color("initial published blue",0,0,255);
    produce(0,1,0,w,h); // Queued GPU writes: no source flush/CPU publication.
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);e=set(c->rend,import_key,record);
    VIMAssert(!e,"reimport orders earlier GPU writes without source CPU publication");
    sample(texture,fbo,w,h);color("ordered unflushed green",0,255,0);
    produce(1,1,0,w,h);GLint zero=back_import?1:0;e=set(c->rend,300,&zero);
    VIMAssert(!e,"native texture barrier accepted");
    sample(texture,fbo,w,h);color("barrier keeps previously imported destination live",255,255,0);
    produce(1,0,0,w,h);glEnable(GL_SCISSOR_TEST);glScissor(0,h/2,w,h-h/2);
    glClearColor(0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);glDisable(GL_SCISSOR_TEST);
    e=set(c->rend,300,&zero);VIMAssert(!e,"asymmetric destination barrier");
    sample(texture,fbo,w,h);
    colorAt("private texture low rows see window top",8,0,255,0);
    colorAt("private texture high rows see window bottom",24,255,0,0);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&fbo);
    glDeleteTextures(1,&colorTexture);glDeleteTextures(1,&texture);
    fprintf(stderr,"ordinary-window destination failures=%d\n",failures);exit(failures?1:0);
}
@end
int main(int argc,char **argv)
{
    back_import=argc==2 && !strcmp(argv[1],"--back-import");
    if(argc!=1 && !back_import)return 2;
    @autoreleasepool {
        [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        NSOpenGLPixelFormatAttribute attributes[]={NSOpenGLPFAAccelerated,NSOpenGLPFANoRecovery,
            (NSOpenGLPixelFormatAttribute)kCGLPFASupportsAutomaticGraphicsSwitching,
            NSOpenGLPFAColorSize,24,NSOpenGLPFAAlphaSize,8,back_import?NSOpenGLPFADoubleBuffer:0,0};
        NSOpenGLPixelFormat *format=[[NSOpenGLPixelFormat alloc] initWithAttributes:attributes];if(!format)return 2;
        NSWindow *window=[[NSWindow alloc] initWithContentRect:NSMakeRect(180,160,256,192)
            styleMask:NSTitledWindowMask backing:NSBackingStoreBuffered defer:NO];
        window.title=@"Native ordinary-window destination qualification";
        VIMDestinationView *view=[[VIMDestinationView alloc] initWithFrame:NSMakeRect(0,0,256,192) pixelFormat:format];
        [view setWantsBestResolutionOpenGLSurface:YES];window.contentView=view;
        [window makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES];[NSApp run];
    }return 2;
}
