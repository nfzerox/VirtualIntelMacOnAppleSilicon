/* Actual native AppKit/NSOpenGLView drawable test. Uses system CGL/libGL.
 * The GL_RESOURCES override is scoped to this executable, not launchctl. */
#import <Cocoa/Cocoa.h>
#if VIM_GL_WINDOW_CORE
#import <OpenGL/gl3.h>
#else
#import <OpenGL/gl.h>
#endif
#import <OpenGL/CGLRenderers.h>
#include "opengl-reference-renderer.h"
static unsigned probeFailures;
static unsigned probeFrames;
static unsigned probeRecoveryKinds;
#if VIM_GL_WINDOW_CORE
static GLuint windowShader(GLenum kind,const char *source)
{
    GLuint shader=glCreateShader(kind);GLint good=0;
    glShaderSource(shader,1,&source,NULL);glCompileShader(shader);
    glGetShaderiv(shader,GL_COMPILE_STATUS,&good);
    if(!good) {
        char log[2048]={0};glGetShaderInfoLog(shader,sizeof(log),NULL,log);
        fprintf(stderr,"FAIL window core shader: %s\n",log);probeFailures++;
    }
    return shader;
}
#endif
@interface VIMGLView : NSOpenGLView {
    NSUInteger _moveStep; /* i386's fragile ObjC runtime needs an explicit ivar. */
#if VIM_GL_WINDOW_CORE
    GLuint _program,_vao,_vbo;
#endif
}
@property NSUInteger moveStep;
@end
@implementation VIMGLView
@synthesize moveStep=_moveStep;
#if VIM_GL_WINDOW_CORE
- (void)prepareOpenGL
{
    [super prepareOpenGL];[[self openGLContext] makeCurrentContext];
    const char *vs="#version 150\nin vec2 position;in vec3 color;out vec3 tint;"
        "void main(){gl_Position=vec4(position,0,1);tint=color;}";
    const char *fs="#version 150\nin vec3 tint;out vec4 outputColor;"
        "void main(){outputColor=vec4(tint,1);}";
    GLuint vertex=windowShader(GL_VERTEX_SHADER,vs),fragment=windowShader(GL_FRAGMENT_SHADER,fs);
    _program=glCreateProgram();glAttachShader(_program,vertex);glAttachShader(_program,fragment);
    glBindAttribLocation(_program,0,"position");glBindAttribLocation(_program,1,"color");
    glBindFragDataLocation(_program,0,"outputColor");glLinkProgram(_program);
    GLint linked=0;glGetProgramiv(_program,GL_LINK_STATUS,&linked);
    if(!linked) { fprintf(stderr,"FAIL window core program link\n");probeFailures++; }
    glDeleteShader(vertex);glDeleteShader(fragment);
    // Same asymmetric bars/triangle as the legacy fixture, using core-only
    // triangles, VBOs and VAOs rather than removed immediate mode or quads.
    const GLfloat vertices[]={
        -1,.9,1,0,0, 1,.9,1,0,0, 1,1,1,0,0,
        -1,.9,1,0,0, 1,1,1,0,0, -1,1,1,0,0,
        -1,-1,0,0,1, 1,-1,0,0,1, 1,-.9,0,0,1,
        -1,-1,0,0,1, 1,-.9,0,0,1, -1,-.9,0,0,1,
        -.8,-.7,1,.2,.15, .8,-.7,.1,1,.45, 0,.8,.2,.45,1};
    glGenVertexArrays(1,&_vao);glBindVertexArray(_vao);
    glGenBuffers(1,&_vbo);glBindBuffer(GL_ARRAY_BUFFER,_vbo);
    glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,5*sizeof(GLfloat),NULL);
    glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,5*sizeof(GLfloat),(void *)(2*sizeof(GLfloat)));
    glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);
    GLint mask=0;glGetIntegerv(GL_CONTEXT_PROFILE_MASK,&mask);
    const GLubyte *version=glGetString(GL_VERSION);GLenum error=glGetError();
    BOOL good=(mask&GL_CONTEXT_CORE_PROFILE_BIT) && !error;
    fprintf(stderr,"%s window core version=%s mask=%x error=%x\n",
        good?"PASS":"FAIL",version,mask,error);
    if(!good)probeFailures++;
}
#endif
- (void)finishProbe:(NSTimer *)timer
{
    (void)timer;
    if(!probeFrames || (getenv("VIM_GL_WINDOW_MOVE") && self.moveStep!=6))probeFailures++;
    if(getenv("VIM_GL_WINDOW_RECOVERY")&&probeRecoveryKinds!=3)probeFailures++;
    fprintf(stderr,"probe completed frames=%u moves=%lu failures=%u\n",probeFrames,
        (unsigned long)self.moveStep,probeFailures);
    exit(probeFailures?1:0); /* NSApplication terminate: otherwise always exits 0. */
}
- (void)moveProbe:(NSTimer *)timer
{
    if(self.moveStep==6) { [timer invalidate];return; }
    NSWindow *window=self.window;
    NSRect frame=window.frame;
    frame.origin.x+=(self.moveStep&1) ? -32 : 48;
    frame.origin.y+=(self.moveStep&1) ? 16 : -24;
    if(getenv("VIM_GL_WINDOW_RESIZE")) {
        NSRect content=[window contentRectForFrameRect:frame];
        content.size=NSMakeSize((self.moveStep&1)?640:664,(self.moveStep&1)?440:456);
        frame.size=[window frameRectForContentRect:content].size;
    }
    [window setFrame:frame display:YES];self.moveStep++;
    [self setNeedsDisplay:YES];
    fprintf(stderr,"probe move %lu window=%ld frame=%g,%g %gx%g\n",(unsigned long)self.moveStep,
        (long)window.windowNumber,frame.origin.x,frame.origin.y,frame.size.width,frame.size.height);
}
- (void)drawRect:(NSRect)rect
{
    (void)rect;
    probeFrames++;
    [[self openGLContext] makeCurrentContext];
    unsigned recoveryKind=0;BOOL recoveryCorrect=YES;
    if(getenv("VIM_GL_WINDOW_RECOVERY")){
        /* AppKit may reselect the display's hardware screen during reshape.
         * Explicitly select via its own API immediately before actual drawing;
         * a transient software string before a resize is NOT a software draw. */
        NSOpenGLContext *context=self.openGLContext;
        recoveryKind=self.moveStep?((self.moveStep-1)&1):0;GLint selected=-1,actual=-1;
        NSOpenGLPixelFormat *format=self.pixelFormat;
        for(GLint i=0;i<format.numberOfVirtualScreens;++i){
            GLint accelerated=0;[format getValues:&accelerated forAttribute:NSOpenGLPFAAccelerated forVirtualScreen:i];
            if(!!accelerated!=!!recoveryKind){selected=i;break;}
        }
        if(selected>=0)[context setCurrentVirtualScreen:selected];
        CGLError e=CGLGetVirtualScreen(context.CGLContextObj,&actual);
        const char *renderer=(const char *)glGetString(GL_RENDERER);
        recoveryCorrect=selected>=0&&!e&&actual==selected&&renderer&&strstr(renderer,recoveryKind?"Software":"Intel");
        fprintf(stderr,"%s window recovery software=%u screen=%d actual=%d error=%d renderer=%s\n",
            recoveryCorrect?"PASS":"FAIL",recoveryKind,selected,actual,e,renderer?renderer:"none");
        if(!recoveryCorrect)++probeFailures;
    }
    if(getenv("VIM_GL_WINDOW_OPACITY_PROBE")){
        const char *renderer=(const char *)glGetString(GL_RENDERER);
        if(!renderer||!strstr(renderer,"Intel")){
            fprintf(stderr,"FAIL opacity probe refused renderer=%s\n",renderer?renderer:"none");exit(2);
        }
        for(GLint opacity=0;opacity<2;++opacity){
            CGLError e=CGLSetParameter([[self openGLContext] CGLContextObj],kCGLCPSurfaceOpacity,&opacity);
            GLint actual=-1;
            CGLError get=CGLGetParameter([[self openGLContext] CGLContextObj],kCGLCPSurfaceOpacity,&actual);
            BOOL good=!e&&!get&&actual==opacity;
            fprintf(stderr,"%s attached-window opacity=%d set=%d get=%d actual=%d renderer=%s\n",
                good?"PASS":"FAIL",opacity,e,get,actual,renderer);
            if(!good)++probeFailures;
        }
    }
    NSRect r=[self convertRectToBacking:[self bounds]];glViewport(0,0,r.size.width,r.size.height);
    fprintf(stderr,"window logical=%gx%g backing=%gx%g scale=%g\n",self.bounds.size.width,
        self.bounds.size.height,r.size.width,r.size.height,self.window.backingScaleFactor);
    glClearColor(0.035,0.055,0.12,1);glClear(GL_COLOR_BUFFER_BIT);
#if VIM_GL_WINDOW_CORE
    glUseProgram(_program);glBindVertexArray(_vao);glDrawArrays(GL_TRIANGLES,0,15);
#else
    // Asymmetric geometry distinguishes row inversion from stale placement.
    glBegin(GL_QUADS);glColor3f(1,0,0);
    glVertex2f(-1,0.9);glVertex2f(1,0.9);glVertex2f(1,1);glVertex2f(-1,1);
    glColor3f(0,0,1);
    glVertex2f(-1,-1);glVertex2f(1,-1);glVertex2f(1,-0.9);glVertex2f(-1,-0.9);glEnd();
    glBegin(GL_TRIANGLES);
    glColor3f(1,0.2,0.15);glVertex2f(-0.8,-0.7);
    glColor3f(0.1,1,0.45);glVertex2f(0.8,-0.7);
    glColor3f(0.2,0.45,1);glVertex2f(0,0.8);
    glEnd();
#endif
    unsigned char pixel[4]={0};glReadPixels(r.size.width/2,r.size.height/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    GLenum drawError=glGetError();
    BOOL correct=abs((int)pixel[0]-98)<=3 && abs((int)pixel[1]-135)<=3 &&
        abs((int)pixel[2]-160)<=3 && pixel[3]==255 && !drawError;
    fprintf(stderr,"%s window renderer=%s center=%u,%u,%u,%u error=%x\n",correct?"PASS":"FAIL",
        glGetString(GL_RENDERER),pixel[0],pixel[1],pixel[2],pixel[3],drawError);
    if(!correct) probeFailures++;
    unsigned char top[4]={0},bottom[4]={0};
    glReadPixels(r.size.width/2,r.size.height-2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,top);
    glReadPixels(r.size.width/2,1,1,1,GL_RGBA,GL_UNSIGNED_BYTE,bottom);
    BOOL orientation=top[0]==255 && !top[1] && !top[2] && top[3]==255 &&
        !bottom[0] && !bottom[1] && bottom[2]==255 && bottom[3]==255 && !glGetError();
    fprintf(stderr,"%s window orientation top=%u,%u,%u,%u bottom=%u,%u,%u,%u\n",
        orientation?"PASS":"FAIL",top[0],top[1],top[2],top[3],bottom[0],bottom[1],bottom[2],bottom[3]);
    if(!orientation)probeFailures++;
    glFlush();
    [[self openGLContext] flushBuffer];
    GLenum presentError=glGetError();
    fprintf(stderr,"%s window presentation error=%x\n",presentError?"FAIL":"PASS",presentError);
    if(presentError) probeFailures++;
    if(getenv("VIM_GL_WINDOW_RECOVERY")&&correct&&orientation&&!presentError&&recoveryCorrect)
        probeRecoveryKinds|=1u<<recoveryKind;
}
@end
int main(void)
{
    @autoreleasepool {
        NSApplication *app=[NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        GLint intel=getenv("VIM_GL_WINDOW_INTEL_HD5000")?VIMReferenceIntelHD5000():0;
        if(getenv("VIM_GL_WINDOW_INTEL_HD5000")&&!intel)return 2;
        NSOpenGLPixelFormatAttribute attrs[]={NSOpenGLPFAAccelerated,NSOpenGLPFANoRecovery,
#if VIM_GL_WINDOW_CORE
            NSOpenGLPFAOpenGLProfile,NSOpenGLProfileVersion3_2Core,
#endif
            NSOpenGLPFAColorSize,24,NSOpenGLPFAAlphaSize,8,0};
        NSOpenGLPixelFormatAttribute intelAttrs[]={NSOpenGLPFAAccelerated,NSOpenGLPFANoRecovery,
            NSOpenGLPFAAllowOfflineRenderers,(NSOpenGLPixelFormatAttribute)kCGLPFASupportsAutomaticGraphicsSwitching,
            NSOpenGLPFARendererID,(NSOpenGLPixelFormatAttribute)intel,
#if VIM_GL_WINDOW_CORE
            NSOpenGLPFAOpenGLProfile,NSOpenGLProfileVersion3_2Core,
#endif
            NSOpenGLPFAColorSize,24,NSOpenGLPFAAlphaSize,8,0};
        NSOpenGLPixelFormatAttribute softwareAttrs[]={NSOpenGLPFARendererID,kCGLRendererGenericFloatID,
            NSOpenGLPFAColorSize,24,NSOpenGLPFAAlphaSize,8,0};
        NSOpenGLPixelFormatAttribute recoveryAttrs[]={NSOpenGLPFARendererID,
            (NSOpenGLPixelFormatAttribute)(intel?intel:0x01036000),
            (NSOpenGLPixelFormatAttribute)kCGLPFASupportsAutomaticGraphicsSwitching,
            NSOpenGLPFAColorSize,24,NSOpenGLPFAAlphaSize,8,0};
        NSOpenGLPixelFormat *pf=[[NSOpenGLPixelFormat alloc] initWithAttributes:
            getenv("VIM_GL_WINDOW_RECOVERY") ? recoveryAttrs :getenv("VIM_GL_WINDOW_SOFTWARE") ? softwareAttrs :
                getenv("VIM_GL_WINDOW_INTEL_HD5000") ? intelAttrs : attrs];
        if(!pf) { fprintf(stderr,"No window pixel format\n");return 2; }
        BOOL behindMenu=getenv("VIM_GL_WINDOW_BEHIND_MENU")!=NULL;
        NSRect frame=behindMenu ? [[NSScreen mainScreen] frame] : NSMakeRect(150,150,640,440);
        NSWindow *window=[[NSWindow alloc] initWithContentRect:frame
            styleMask:behindMenu ? NSBorderlessWindowMask : NSTitledWindowMask|NSClosableWindowMask|NSResizableWindowMask
            backing:NSBackingStoreBuffered defer:NO];
        const char *title=getenv("VIM_GL_WINDOW_TITLE");
        [window setTitle:title ? [NSString stringWithUTF8String:title] : @"PassthroughOpenGL — native Mavericks OpenGL window"];
        VIMGLView *view=[[VIMGLView alloc] initWithFrame:NSMakeRect(0,0,frame.size.width,frame.size.height) pixelFormat:pf];
        if(getenv("VIM_GL_WINDOW_HIDPI")) [view setWantsBestResolutionOpenGLSurface:YES];
        const char *seconds=getenv("VIM_GL_WINDOW_SECONDS");
        if(!seconds&&getenv("VIM_GL_WINDOW_OPACITY_PROBE"))seconds="5";
        if(seconds && atof(seconds)>0) [NSTimer scheduledTimerWithTimeInterval:atof(seconds)
            target:view selector:@selector(finishProbe:) userInfo:nil repeats:NO];
        if(getenv("VIM_GL_WINDOW_MOVE")) [NSTimer scheduledTimerWithTimeInterval:2
            target:view selector:@selector(moveProbe:) userInfo:nil repeats:YES];
        [window setContentView:view];
        if(behindMenu) {
            // A temporary diagnostic producer beneath the menu bar, without
            // replacing the wallpaper preference or synthesizing user input.
            // CGXUpdateMagicMenuBar includes wallpaper-level windows
            // (desktop - 1), excluding ordinary desktop/icon windows.
            [window setLevel:CGWindowLevelForKey(kCGDesktopWindowLevelKey)-1];
            [window setIgnoresMouseEvents:YES];[window orderFront:nil];
        } else {
            [window makeKeyAndOrderFront:nil];[app activateIgnoringOtherApps:YES];
            // Keep fixture presentation independent of Launch Services
            // activation in CLI/SSH runs. Raise only our own window.
            [window orderFrontRegardless];
        }
        fprintf(stderr,"probe window=%ld visible=%d active=%d level=%ld\n",
            (long)window.windowNumber,(int)[window isVisible],(int)[app isActive],(long)window.level);
        [app run];
    }return probeFailures?1:0;
}
