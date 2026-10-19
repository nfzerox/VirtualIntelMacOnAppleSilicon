// Native AppKit/OpenGL entry points, real double-buffered window presentation.
// Research runner may select a private GLEngine through GL_RESOURCES; never
// inserts a library into another process. No production environment changes.
#define GL_SILENCE_DEPRECATION
#import <Cocoa/Cocoa.h>
#import <OpenGL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
// gl.h and gl3.h are mutually exclusive; these two core-only declarations
// match gl3.h so one harness can exercise both legacy and core contexts.
extern void glGenVertexArrays(GLsizei n,GLuint *arrays);
extern void glBindVertexArray(GLuint array);

static unsigned frames,bad;
static double firstFrame;
static double monotonicSeconds(void) {
    struct timespec value;clock_gettime(CLOCK_MONOTONIC,&value);
    return value.tv_sec+value.tv_nsec/1e9;
}
static GLuint program,vao;
static GLint colorLocation;
static GLuint shader(GLenum type,const char *source) {
    GLuint result=glCreateShader(type);glShaderSource(result,1,&source,NULL);glCompileShader(result);
    GLint ok=0;glGetShaderiv(result,GL_COMPILE_STATUS,&ok);
    if(!ok){char log[2048];glGetShaderInfoLog(result,sizeof(log),NULL,log);fprintf(stderr,"%s\n",log);exit(4);}
    return result;
}
@interface PVGGLWindowView : NSOpenGLView
@end
@implementation PVGGLWindowView
- (void)prepareOpenGL {
    [super prepareOpenGL];
    GLint accelerated=0;
    [[self pixelFormat] getValues:&accelerated forAttribute:NSOpenGLPFAAccelerated forVirtualScreen:0];
    printf("accelerated=%d renderer=%s\n",accelerated,glGetString(GL_RENDERER));
    if(!accelerated)exit(3);
    if(!self.wantsLayer){[[self openGLContext] setView:self];[[self openGLContext] update];}
    glDisable(GL_DITHER);
    if(getenv("PVG_GL_DEPTH_READ"))glEnable(GL_DEPTH_TEST);
    if(getenv("PVG_GL_CORE_PROFILE")) {
        GLuint vs=shader(GL_VERTEX_SHADER,"#version 150\nvoid main(){const vec2 p[3]=vec2[3](vec2(-.8,-.7),vec2(.8,-.7),vec2(0,.8));gl_Position=vec4(p[gl_VertexID],0,1);}");
        GLuint fs=shader(GL_FRAGMENT_SHADER,"#version 150\nuniform vec4 color;out vec4 c;void main(){c=color;}");
        program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);
        GLint ok=0;glGetProgramiv(program,GL_LINK_STATUS,&ok);if(!ok)exit(4);
        colorLocation=glGetUniformLocation(program,"color");if(colorLocation<0)exit(4);
        glGenVertexArrays(1,&vao);glBindVertexArray(vao);
    }
    GLint interval=1;[[self openGLContext] setValues:&interval forParameter:NSOpenGLCPSwapInterval];
}
- (void)tick:(NSTimer *)timer {
    if(frames&&frames%100==0&&frames<600&&getenv("PVG_GL_RECREATE_CONTEXT")) {
        [[self openGLContext] clearDrawable];
        NSOpenGLContext *next=[[NSOpenGLContext alloc] initWithFormat:[self pixelFormat] shareContext:nil];
        if(!next)exit(5);
        [self setOpenGLContext:next];[next release];
        [[self openGLContext] makeCurrentContext];
        [self prepareOpenGL];
        printf("recreated context at frame=%u\n",frames);
    }
    [self setNeedsDisplay:YES];
    if(frames>=600){[timer invalidate];printf("frames=%u bad=%u elapsed=%.3fs\n",frames,bad,monotonicSeconds()-firstFrame);exit(bad?1:0);}
}
- (void)drawRect:(NSRect)dirty {
    if(!frames)firstFrame=monotonicSeconds();
    (void)dirty;[[self openGLContext] makeCurrentContext];
    NSRect pixels=[self convertRectToBacking:self.bounds];
    if(!frames) {
        GLint backing[2]={0,0};
        [[self openGLContext] getValues:backing forParameter:NSOpenGLContextParameterSurfaceBackingSize];
        printf("drawable view=%.0fx%.0f backingRect=%.0fx%.0f contextBacking=%dx%d\n",
            self.bounds.size.width,self.bounds.size.height,pixels.size.width,pixels.size.height,backing[0],backing[1]);
    }
    glViewport(0,0,(GLsizei)pixels.size.width,(GLsizei)pixels.size.height);
    glClearColor(0.05f,0.15f,0.4f,1);
    if(getenv("PVG_GL_SPLIT_CLEAR")) {
        glClear(GL_STENCIL_BUFFER_BIT);glFlush();
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    } else glClear(GL_COLOR_BUFFER_BIT|(getenv("PVG_GL_WINDOW_MSAA")?(GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT):0));
    unsigned char red=(frames/30)%2?255:64;
    if(program) {
        glUseProgram(program);glBindVertexArray(vao);glUniform4f(colorLocation,red/255.f,192/255.f,64/255.f,1);
        glDrawArrays(GL_TRIANGLES,0,3);
    } else {
        glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();
        glColor4ub(red,192,64,255);
        glBegin(GL_TRIANGLES);glVertex2f(-.8f,-.7f);glVertex2f(.8f,-.7f);glVertex2f(0,.8f);glEnd();
    }
    if(frames==0||frames==300||frames==599) {
        unsigned char pixel[4]={0};
        glReadPixels((GLint)pixels.size.width/2,(GLint)pixels.size.height/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
        GLenum error=glGetError();
        printf("frame=%u center=%u,%u,%u,%u error=0x%x\n",frames,pixel[0],pixel[1],pixel[2],pixel[3],error);
        if(pixel[0]!=red||pixel[1]!=192||pixel[2]!=64||error)bad++;
        if(getenv("PVG_GL_DEPTH_READ")) {
            GLfloat depth=-1;
            glReadPixels((GLint)pixels.size.width/2,(GLint)pixels.size.height/2,1,1,GL_DEPTH_COMPONENT,GL_FLOAT,&depth);
            error=glGetError();printf("frame=%u depth=%f error=0x%x\n",frames,depth,error);
            if(depth<.499f||depth>.501f||error)bad++;
        }
    }
    [[self openGLContext] flushBuffer];frames++;
}
@end
int main(void) {
    alarm(90);
    setbuf(stdout,NULL);
    @autoreleasepool {
        if((getenv("PVG_GL_DEPTH_READ")||getenv("PVG_GL_SPLIT_CLEAR"))&&!getenv("PVG_GL_WINDOW_MSAA"))return 64;
        [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        NSOpenGLPixelFormatAttribute attrs[]={NSOpenGLPFAAccelerated,NSOpenGLPFANoRecovery,
            NSOpenGLPFADoubleBuffer,NSOpenGLPFAColorSize,24,NSOpenGLPFAOpenGLProfile,
            getenv("PVG_GL_CORE_PROFILE")?NSOpenGLProfileVersion3_2Core:NSOpenGLProfileVersionLegacy,0};
        NSOpenGLPixelFormatAttribute depthAttrs[]={NSOpenGLPFAAccelerated,NSOpenGLPFANoRecovery,
            NSOpenGLPFADoubleBuffer,NSOpenGLPFAWindow,NSOpenGLPFAColorSize,24,NSOpenGLPFAAlphaSize,8,
            NSOpenGLPFADepthSize,16,NSOpenGLPFAStencilSize,1,NSOpenGLPFAMultisample,
            NSOpenGLPFASampleBuffers,1,NSOpenGLPFASamples,4,
            NSOpenGLPFAOpenGLProfile,
            getenv("PVG_GL_CORE_PROFILE")?NSOpenGLProfileVersion3_2Core:NSOpenGLProfileVersionLegacy,0};
        if(getenv("PVG_GL_WINDOW_MSAA")) {
            int samples=atoi(getenv("PVG_GL_WINDOW_MSAA"));
            if(samples!=2&&samples!=4&&samples!=8)return 64;
            depthAttrs[sizeof(depthAttrs)/sizeof(depthAttrs[0])-4]=samples;
        }
        // The deprecated legacy window-selection attribute excludes a core
        // format even though its accelerated MSAA drawable is supported.
        if(getenv("PVG_GL_CORE_PROFILE")||getenv("PVG_GL_OMIT_WINDOW_ATTRIBUTE")) {
            for(NSUInteger i=3;i+1<sizeof(depthAttrs)/sizeof(depthAttrs[0]);i++)
                depthAttrs[i]=depthAttrs[i+1];
        }
        NSOpenGLPixelFormat *pf=[[NSOpenGLPixelFormat alloc] initWithAttributes:
            getenv("PVG_GL_WINDOW_MSAA")?depthAttrs:attrs];if(!pf)return 2;
        if(getenv("PVG_GL_QUERY_ONLY")) {
            NSOpenGLContext *context=[[NSOpenGLContext alloc] initWithFormat:pf shareContext:nil];
            if(!context)return 3;
            [context makeCurrentContext];
            GLint maximum=0,selected=0;
            glGetIntegerv(GL_MAX_SAMPLES,&maximum);
            GLenum error=glGetError();
            [pf getValues:&selected forAttribute:NSOpenGLPFASamples forVirtualScreen:0];
            printf("GL capability maxSamples=%d selectedSamples=%d error=0x%x\n",maximum,selected,error);
            [NSOpenGLContext clearCurrentContext];
            return error?4:0;
        }
        int width=getenv("PVG_GL_WINDOW_WIDTH")?atoi(getenv("PVG_GL_WINDOW_WIDTH")):500;
        int height=getenv("PVG_GL_WINDOW_HEIGHT")?atoi(getenv("PVG_GL_WINDOW_HEIGHT")):350;
        if(width<64||height<64||width>2048||height>2048)return 64;
        NSWindow *window=[[NSWindow alloc] initWithContentRect:NSMakeRect(30,60,width,height)
            styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskResizable
            backing:NSBackingStoreBuffered defer:NO];
        window.title=@"PVG native OpenGL presentation test";
        PVGGLWindowView *view=[[PVGGLWindowView alloc] initWithFrame:NSMakeRect(0,0,width,height) pixelFormat:pf];
        if(getenv("PVG_GL_LAYER_BACKED"))view.wantsLayer=YES;
        window.contentView=view;[window makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES];
        [NSTimer scheduledTimerWithTimeInterval:1.0/30 target:view selector:@selector(tick:) userInfo:nil repeats:YES];
        [NSApp run];
    }
    return 2;
}
