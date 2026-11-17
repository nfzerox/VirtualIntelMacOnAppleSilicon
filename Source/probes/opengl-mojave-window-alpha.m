/* Native Intel comparison of transparent GL content over CPU AppKit pixels.
 * Reads only this fixture's own window. No system preferences or hooks. */
#import <Cocoa/Cocoa.h>
#import <OpenGL/gl.h>
#include <unistd.h>
static int result=2;
static unsigned phase,failures;
@interface AlphaBackground : NSView
@end
@implementation AlphaBackground
- (void)drawRect:(NSRect)rect { (void)rect;[[NSColor greenColor]setFill];NSRectFill(self.bounds); }
@end
@interface AlphaView : NSOpenGLView
@end
@implementation AlphaView
- (BOOL)isOpaque { return NO; }
- (void)drawRect:(NSRect)rect
{
    (void)rect;[self.openGLContext makeCurrentContext];
    NSRect pixels=[self convertRectToBacking:self.bounds];
    GLint opacity=phase==1;CGLError e=CGLSetParameter(self.openGLContext.CGLContextObj,kCGLCPSurfaceOpacity,&opacity);
    glViewport(0,0,pixels.size.width,pixels.size.height);glDisable(GL_SCISSOR_TEST);
    glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);glScissor(0,0,pixels.size.width/2,pixels.size.height);
    glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);glDisable(GL_SCISSOR_TEST);
    unsigned char p[4]={0};glReadPixels(pixels.size.width*3/4,pixels.size.height/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
    fprintf(stderr,"renderer=%s opacity=%d transparent-buffer=%u,%u,%u,%u error=%x\n",
        glGetString(GL_RENDERER),e,p[0],p[1],p[2],p[3],glGetError());
    if(e || p[0] || p[1] || p[2] || p[3])++failures;
    [self.openGLContext flushBuffer];
}
- (void)measure:(NSTimer *)timer
{
    (void)timer;CGImageRef image=CGWindowListCreateImage(CGRectNull,kCGWindowListOptionIncludingWindow,
        self.window.windowNumber,kCGWindowImageBoundsIgnoreFraming);
    if(!image)exit(2);
    size_t w=CGImageGetWidth(image),h=CGImageGetHeight(image);
    unsigned char *bytes=calloc(w*h,4);CGColorSpaceRef color=CGColorSpaceCreateDeviceRGB();
    CGContextRef bitmap=CGBitmapContextCreate(bytes,w,h,8,w*4,color,kCGImageAlphaPremultipliedLast|kCGBitmapByteOrder32Big);
    CGContextDrawImage(bitmap,CGRectMake(0,0,w,h),image);
    unsigned char *left=bytes+(h/2*w+w/4)*4,*right=bytes+(h/2*w+w*3/4)*4;
    fprintf(stderr,"phase=%u composed=%zux%zu left=%u,%u,%u,%u right=%u,%u,%u,%u\n",phase,w,h,
        left[0],left[1],left[2],left[3],right[0],right[1],right[2],right[3]);
    if(!(left[0]>200&&left[1]<80&&left[2]<80&&right[0]<80&&right[2]<80&&
         (phase==1?right[1]<80:right[1]>200)))++failures;
    CGContextRelease(bitmap);CGColorSpaceRelease(color);free(bytes);CGImageRelease(image);
    if(++phase<3) {
        [self setNeedsDisplay:YES];[self displayIfNeeded];
        [NSTimer scheduledTimerWithTimeInterval:2 target:self selector:@selector(measure:) userInfo:nil repeats:NO];
    } else { result=failures?1:0;exit(result); }
}
@end
int main(void)
{
    alarm(15);@autoreleasepool {
        [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        NSWindow *window=[[NSWindow alloc]initWithContentRect:NSMakeRect(150,150,256,128)
            styleMask:NSTitledWindowMask|NSClosableWindowMask backing:NSBackingStoreBuffered defer:NO];
        window.title=@"Native GL transparent window contract";
        AlphaBackground *background=[[AlphaBackground alloc]initWithFrame:NSMakeRect(0,0,256,128)];window.contentView=background;
        NSOpenGLPixelFormatAttribute attributes[]={NSOpenGLPFAAccelerated,NSOpenGLPFANoRecovery,
            NSOpenGLPFADoubleBuffer,NSOpenGLPFAColorSize,24,NSOpenGLPFAAlphaSize,8,
            (NSOpenGLPixelFormatAttribute)kCGLPFASupportsAutomaticGraphicsSwitching,NSOpenGLPFARendererID,0x1024500,0};
        if(!getenv("VIM_REFERENCE_INTEL"))attributes[7]=0;
        NSOpenGLPixelFormat *format=[[NSOpenGLPixelFormat alloc]initWithAttributes:attributes];if(!format)return 2;
        AlphaView *view=[[AlphaView alloc]initWithFrame:background.bounds pixelFormat:format];
        view.wantsBestResolutionOpenGLSurface=YES;[background addSubview:view];
        [window makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES];[window orderFrontRegardless];
        [NSTimer scheduledTimerWithTimeInterval:2 target:view selector:@selector(measure:) userInfo:nil repeats:NO];
        [NSApp run];return result;
    }
}
