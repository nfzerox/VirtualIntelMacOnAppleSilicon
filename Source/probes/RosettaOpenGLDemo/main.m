/* PowerPC-only Tiger/Leopard/Snow Leopard/Rosetta1 acceptance app. Build with Apple's
 * period GCC, never add an Intel slice that Launch Services could prefer.
 * No private APIs, renderer injection, host patches or benchmark upload.
 */
#import <Cocoa/Cocoa.h>
#import <OpenGL/OpenGL.h>
#import <OpenGL/gl.h>
#import <OpenGL/glext.h>
#import <ApplicationServices/ApplicationServices.h>
#include <sys/sysctl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <signal.h>
#include <unistd.h>
#if !defined(__ppc__) || defined(__ppc64__)
#error This fixture must be a 32-bit PowerPC executable, not Intel or PPC64.
#endif

static int failures, frameLimit, frames, traceGate;
__attribute__((noinline)) int VIMRosettaCodeUpdateProbe(void){return 1;}
static NSTextField *status;
static void VIMCheck(int good,const char *name)
{printf("%s %s\n",good?"PASS":"FAIL",name);fflush(stdout);failures+=!good;}
static void pixel(const char *name,unsigned r,unsigned g,unsigned b)
{
    GLubyte p[4]={0};glReadPixels(4,4,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
    GLenum error=glGetError();printf("pixel %s=%u,%u,%u,%u GL=%x\n",name,p[0],p[1],p[2],p[3],(unsigned)error);
    VIMCheck(!error && abs((int)p[0]-(int)r)<3 && abs((int)p[1]-(int)g)<3 &&
        abs((int)p[2]-(int)b)<3 && p[3]==255,name);
}
static void quad(void)
{
    glBegin(GL_QUADS);
    glTexCoord2f(0,0);glVertex3f(-1,-1,0);glTexCoord2f(1,0);glVertex3f(1,-1,0);
    glTexCoord2f(1,1);glVertex3f(1,1,0);glTexCoord2f(0,1);glVertex3f(-1,1,0);glEnd();
}
static void qualify(void)
{
    GLuint target=0,fbo=0,depth=0,texture=0,vertices=0,elements=0;
    glGenTextures(1,&target);glBindTexture(GL_TEXTURE_2D,target);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,16,16,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,target,0);
    glGenRenderbuffersEXT(1,&depth);glBindRenderbufferEXT(GL_RENDERBUFFER_EXT,depth);
    glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT,GL_DEPTH_COMPONENT24,16,16);
    glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT,GL_DEPTH_ATTACHMENT_EXT,GL_RENDERBUFFER_EXT,depth);
    VIMCheck(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT,"offscreen color/depth framebuffer");
    glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);glViewport(0,0,16,16);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    glClearColor(0,0,0,1);glClearDepth(1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);glColor4f(1,0,0,1);quad();
    glTranslatef(0,0,.5);glColor4f(0,0,1,1);quad();pixel("depth occlusion",255,0,0);
    glLoadIdentity();glDisable(GL_DEPTH_TEST);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glBlendFuncSeparateEXT(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0,0,1,.5);quad();pixel("alpha blending",127,0,128);glDisable(GL_BLEND);
    glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);glEnable(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    /* Native-endian integer data: these bytes are not an Intel buffer with
     * a PPC executable wrapped around it. Rosetta's GL boundary must handle
     * the actual packed-pixel type and big-endian guest memory correctly. */
    uint32_t green=0xff00ff00U;glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,
        GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,&green);quad();pixel("native-endian packed texture",0,255,0);
    glDisable(GL_TEXTURE_2D);
    /* Likewise the VBO really contains big-endian PowerPC floats. */
    const GLfloat points[]={-1,-1, 1,-1, 1,1, -1,1};
    glGenBuffers(1,&vertices);glBindBuffer(GL_ARRAY_BUFFER,vertices);
    glBufferData(GL_ARRAY_BUFFER,sizeof(points),points,GL_STATIC_DRAW);
    glEnableClientState(GL_VERTEX_ARRAY);glVertexPointer(2,GL_FLOAT,0,0);glColor4f(1,1,0,1);
    glDrawArrays(GL_QUADS,0,4);pixel("native-endian float vertex buffer",255,255,0);
    GLfloat retained[8]={0};glGetBufferSubData(GL_ARRAY_BUFFER,0,sizeof(retained),retained);
    VIMCheck(!memcmp(retained,points,sizeof(points)),"draw preserves raw vertex buffer bytes");
    glBindBuffer(GL_ARRAY_BUFFER,0);glVertexPointer(2,GL_FLOAT,0,points);
    glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);glDrawArrays(GL_QUADS,0,4);
    pixel("native-endian client float array",255,255,0);
    const GLushort shortIndices[]={0,1,2,3};
    glClear(GL_COLOR_BUFFER_BIT);glDrawElements(GL_QUADS,4,GL_UNSIGNED_SHORT,shortIndices);
    pixel("native-endian client short indices",255,255,0);
    glBindBuffer(GL_ARRAY_BUFFER,vertices);glVertexPointer(2,GL_FLOAT,0,0);
    const GLuint wordIndices[]={0,1,2,3};
    glGenBuffers(1,&elements);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,elements);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(wordIndices),wordIndices,GL_STATIC_DRAW);
    glClear(GL_COLOR_BUFFER_BIT);glDrawElements(GL_QUADS,4,GL_UNSIGNED_INT,0);
    pixel("native-endian element buffer",255,255,0);
    GLuint retainedIndices[4]={0};glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER,0,sizeof(retainedIndices),retainedIndices);
    VIMCheck(!memcmp(retainedIndices,wordIndices,sizeof(wordIndices)),"draw preserves raw element buffer bytes");
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);glDeleteBuffers(1,&elements);
    struct Vertex {GLfloat position[2];GLubyte color[4];GLuint padding;};
    const struct Vertex interleaved[]={{{-1,-1},{255,255,0,255},0x12345678},
        {{1,-1},{255,255,0,255},0x87654321},{{1,1},{255,255,0,255},0xaabbccdd},
        {{-1,1},{255,255,0,255},0xddccbbaa}};
    glBufferData(GL_ARRAY_BUFFER,sizeof(interleaved),interleaved,GL_STATIC_DRAW);
    glVertexPointer(2,GL_FLOAT,sizeof(struct Vertex),0);
    glEnableClientState(GL_COLOR_ARRAY);glColorPointer(4,GL_UNSIGNED_BYTE,sizeof(struct Vertex),(void *)8);
    glClear(GL_COLOR_BUFFER_BIT);glDrawArrays(GL_QUADS,0,4);
    pixel("interleaved float and byte VBO",255,255,0);glDisableClientState(GL_COLOR_ARRAY);
    struct Vertex retainedInterleaved[4];glGetBufferSubData(GL_ARRAY_BUFFER,0,sizeof(retainedInterleaved),retainedInterleaved);
    VIMCheck(!memcmp(retainedInterleaved,interleaved,sizeof(interleaved)),"typed conversion preserves interleaved padding");
    const GLfloat identity[]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    glLoadMatrixf(identity);glClear(GL_COLOR_BUFFER_BIT);glDrawArrays(GL_QUADS,0,4);
    pixel("native-endian matrix pointer",255,255,0);
    glDisableClientState(GL_VERTEX_ARRAY);glBindBuffer(GL_ARRAY_BUFFER,0);glDeleteBuffers(1,&vertices);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&fbo);
    glDeleteRenderbuffersEXT(1,&depth);glDeleteTextures(1,&target);glDeleteTextures(1,&texture);
    glDrawBuffer(GL_BACK);glReadBuffer(GL_BACK);VIMCheck(glGetError()==GL_NO_ERROR,"clean GL state after checks");
}
@interface RosettaView:NSOpenGLView {GLuint pattern;BOOL initialized;float angle;NSTimer *clock;}
@end
@implementation RosettaView
- (void)tick:(NSTimer *)timer {(void)timer;angle+=.7f;[self setNeedsDisplay:YES];}
- (void)drawRect:(NSRect)dirty
{
    (void)dirty;[[self openGLContext] makeCurrentContext];
    if(!initialized){
        initialized=YES;GLint accelerated=0;CGLPixelFormatObj format=[[self pixelFormat] CGLPixelFormatObj];
        CGLDescribePixelFormat(format,0,kCGLPFAAccelerated,&accelerated);
        int native=-1;size_t size=sizeof(native);int nativeStatus=sysctlbyname("sysctl.proc_native",&native,&size,NULL,0);
        const char *renderer=(const char *)glGetString(GL_RENDERER);
        printf("architecture=PowerPC32 sizeof(void*)=%lu proc_native=%d query=%d renderer=%s GL=%s\n",
            (unsigned long)sizeof(void*),native,nativeStatus,renderer?renderer:"none",glGetString(GL_VERSION));
        VIMCheck(!nativeStatus && native==0,"running under Rosetta1");
        VIMCheck(accelerated && renderer && !strstr(renderer,"Software") && !strstr(renderer,"Float"),"accelerated renderer selected");
        qualify();
        GLubyte image[64*64*4];int x,y;
        for(y=0;y<64;y++)for(x=0;x<64;x++){int i=(y*64+x)*4;
            image[i]=((x/8+y/8)&1)?245:32;image[i+1]=(GLubyte)(80+x*2);
            image[i+2]=(GLubyte)(245-y*2);image[i+3]=255;}
        glGenTextures(1,&pattern);glBindTexture(GL_TEXTURE_2D,pattern);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,64,64,0,GL_RGBA,GL_UNSIGNED_BYTE,image);
        [status setStringValue:[NSString stringWithFormat:@"PowerPC · Rosetta 1 · %s · %@",renderer?renderer:"No renderer",
            failures?@"Checks FAILED":@"Graphics checks passed"]];
        clock=[NSTimer scheduledTimerWithTimeInterval:1.0/60.0 target:self selector:@selector(tick:) userInfo:nil repeats:YES];
    }
    NSRect bounds=[self bounds];glViewport(0,0,bounds.size.width,bounds.size.height);
    glClearColor(.025,.04,.09,1);glClearDepth(1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();float aspect=bounds.size.width/bounds.size.height;
    glFrustum(-aspect,aspect,-1,1,2,20);glMatrixMode(GL_MODELVIEW);glLoadIdentity();glTranslatef(0,0,-6);
    glRotatef(angle*.7f,1,0,0);glRotatef(angle,0,1,0);glRotatef(angle*.3f,0,0,1);
    glEnable(GL_DEPTH_TEST);glEnable(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,pattern);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
    const GLfloat faces[6][12]={{-1,-1,1,1,-1,1,1,1,1,-1,1,1},{1,-1,-1,-1,-1,-1,-1,1,-1,1,1,-1},
        {-1,-1,-1,-1,-1,1,-1,1,1,-1,1,-1},{1,-1,1,1,-1,-1,1,1,-1,1,1,1},
        {-1,1,1,1,1,1,1,1,-1,-1,1,-1},{-1,-1,-1,1,-1,-1,1,-1,1,-1,-1,1}};
    int f;for(f=0;f<6;f++){glColor4f(.65f+.35f*(f&1),.7f+.3f*(f%3==0),1,1);glBegin(GL_QUADS);
        glTexCoord2f(0,0);glVertex3fv(faces[f]);glTexCoord2f(1,0);glVertex3fv(faces[f]+3);
        glTexCoord2f(1,1);glVertex3fv(faces[f]+6);glTexCoord2f(0,1);glVertex3fv(faces[f]+9);glEnd();}
    glDisable(GL_TEXTURE_2D);[[self openGLContext] flushBuffer];
    if(frameLimit && ++frames>=frameLimit){printf("RESULT failures=%d visible_frames=%d\n",failures,frames);fflush(stdout);exit(failures?1:0);}
}
@end
int main(int argc,char **argv)
{
    int argument;for(argument=1;argument<argc;argument++){
        if(!strncmp(argv[argument],"--frames=",9)){frameLimit=atoi(argv[argument]+9);if(frameLimit<1||frameLimit>36000)return 64;}
        else if(!strcmp(argv[argument],"--trace-gate"))traceGate=1;
        else return 64;
    }
    setvbuf(stdout,NULL,_IOLBF,0);NSAutoreleasePool *pool=[NSAutoreleasePool new];
    puts("PowerPC startup: creating native Cocoa application");
    printf("PowerPC code-cache fixture initial=%d\n",VIMRosettaCodeUpdateProbe());
    [NSApplication sharedApplication];
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
    ProcessSerialNumber current={0,kCurrentProcess};
    TransformProcessType(&current,kProcessTransformToForegroundApplication);
#else
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
#endif
    NSOpenGLPixelFormatAttribute attributes[]={NSOpenGLPFAAccelerated,NSOpenGLPFANoRecovery,NSOpenGLPFADoubleBuffer,
        NSOpenGLPFAColorSize,24,NSOpenGLPFAAlphaSize,8,NSOpenGLPFADepthSize,24,0};
    puts("PowerPC startup: selecting accelerated OpenGL pixel format");
    NSOpenGLPixelFormat *format=[[NSOpenGLPixelFormat alloc] initWithAttributes:attributes];
    if(!format){fputs("FAIL no accelerated pixel format\n",stderr);return 2;}
    if(traceGate){printf("TRACE_GATE pid=%d; attach native probes, then SIGCONT\n",getpid());raise(SIGSTOP);}
    puts("PowerPC startup: creating visible OpenGL window");
    NSWindow *window=[[NSWindow alloc] initWithContentRect:NSMakeRect(220,150,640,460)
        styleMask:NSTitledWindowMask|NSClosableWindowMask|NSMiniaturizableWindowMask|NSResizableWindowMask
        backing:NSBackingStoreBuffered defer:NO];[window setTitle:@"PowerPC OpenGL — Rosetta 1"];
    RosettaView *view=[[RosettaView alloc] initWithFrame:NSMakeRect(0,30,640,430) pixelFormat:format];
    [view setAutoresizingMask:NSViewWidthSizable|NSViewHeightSizable];[[window contentView] addSubview:view];
    status=[[NSTextField alloc] initWithFrame:NSMakeRect(12,5,616,20)];[status setEditable:NO];[status setSelectable:NO];
    [status setBordered:NO];[status setDrawsBackground:NO];[status setFont:[NSFont systemFontOfSize:11]];
    [status setAutoresizingMask:NSViewWidthSizable|NSViewMaxYMargin];[[window contentView] addSubview:status];
    [window makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES];
    puts("PowerPC startup: entering Cocoa event loop");[NSApp run];[pool drain];return failures?1:0;
}
