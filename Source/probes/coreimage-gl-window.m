// Isolate Core Image drawing to an AppKit double-buffered drawable.
#define GL_SILENCE_DEPRECATION
#import <Cocoa/Cocoa.h>
#import <CoreImage/CoreImage.h>
#import <OpenGL/gl.h>
#include <unistd.h>
@interface CIProbeView : NSOpenGLView
@property(retain) CIContext *renderer;
@property(retain) CIImage *image;
@end
@implementation CIProbeView
- (void)prepareOpenGL {
    [super prepareOpenGL];
    [[self openGLContext] makeCurrentContext];
    CGColorSpaceRef color=CGColorSpaceCreateDeviceRGB();
    self.renderer=[CIContext contextWithCGLContext:self.openGLContext.CGLContextObj
        pixelFormat:self.pixelFormat.CGLPixelFormatObj colorSpace:color options:nil];
    CGColorSpaceRelease(color);
    NSLog(@"GL renderer: %s",glGetString(GL_RENDERER));
}
- (void)drawRect:(NSRect)dirty {
    (void)dirty;[[self openGLContext] makeCurrentContext];
    NSRect backing=[self convertRectToBacking:self.bounds];
    glViewport(0,0,backing.size.width,backing.size.height);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();
    glOrtho(0,backing.size.width,0,backing.size.height,-1,1);
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    glClearColor(1,0,1,1);glClear(GL_COLOR_BUFFER_BIT);
    [self.renderer drawImage:self.image inRect:backing fromRect:self.image.extent];
    glFinish();
    unsigned char pixel[4]={0};
    glReadPixels(backing.size.width/2,backing.size.height/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    NSLog(@"center RGBA=%u,%u,%u,%u error=%x",pixel[0],pixel[1],pixel[2],pixel[3],glGetError());
    [[self openGLContext] flushBuffer];
}
@end
int main(int argc,char **argv) {@autoreleasepool {
    if(argc!=2)return 64;
    alarm(45);
    [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    NSOpenGLPixelFormatAttribute attrs[]={NSOpenGLPFAAccelerated,NSOpenGLPFADoubleBuffer,NSOpenGLPFAColorSize,24,0};
    NSOpenGLPixelFormat *format=[[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];
    if(!format)return 2;
    NSWindow *window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,1000,700)
        styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable backing:NSBackingStoreBuffered defer:NO];
    window.title=@"Core Image OpenGL presentation probe";
    CIProbeView *view=[[CIProbeView alloc] initWithFrame:window.contentView.bounds pixelFormat:format];
    view.wantsBestResolutionOpenGLSurface=YES;
    view.image=[CIImage imageWithContentsOfURL:[NSURL fileURLWithPath:@(argv[1])]];
    if(!view.image)return 3;
    window.contentView=view;[window center];[window makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES];
    [NSApp run];return 0;
}}
