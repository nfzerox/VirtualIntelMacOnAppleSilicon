/* Finite visible WebKit probe using the installed framework. Explicit WebGL
 * preference is process-local/non-autosaving; never changes Safari defaults.
 * Usage: PROGRAM /absolute/opengl-webkit-compositing.html
 * A bounded probe is not a browser-policy bypass or broad GL qualification. */
#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>
#include <unistd.h>
#include <signal.h>

@interface WebPreferences (VIMResearchWebGL)
- (void)setWebGLEnabled:(BOOL)enabled;
@end

@interface VIMWebGLProbe : NSObject <NSApplicationDelegate> {
    NSWindow *_window;
    WebView *_web;
    NSString *_path;
    BOOL _loaded;
    BOOL _waiting, _started;
    dispatch_source_t _signal;
}
- (id)initWithPath:(NSString *)path wait:(BOOL)wait;
@end
@implementation VIMWebGLProbe
- (id)initWithPath:(NSString *)path wait:(BOOL)wait
{ if ((self=[super init])) {_path=[path copy];_waiting=wait;}return self; }
- (void)applicationDidFinishLaunching:(NSNotification *)notification
{
    (void)notification;
    _window=[[NSWindow alloc] initWithContentRect:NSMakeRect(160,100,1000,650)
        styleMask:NSTitledWindowMask|NSClosableWindowMask|NSResizableWindowMask
        backing:NSBackingStoreBuffered defer:NO];
    [_window setTitle:@"Installed WebKit — process-local WebGL probe"];
    _web=[[WebView alloc] initWithFrame:[[_window contentView] bounds] frameName:nil groupName:nil];
    [_web setAutoresizingMask:NSViewWidthSizable|NSViewHeightSizable];
    WebPreferences *preferences=[[WebPreferences alloc] initWithIdentifier:nil];
    [preferences setAutosaves:NO];[preferences setWebGLEnabled:YES];
    [_web setPreferences:preferences];[preferences release];
    [_web setFrameLoadDelegate:self];[_window setContentView:_web];
    [_window makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES];
    if(_waiting) {
        signal(SIGUSR1,SIG_IGN);
        _signal=dispatch_source_create(DISPATCH_SOURCE_TYPE_SIGNAL,SIGUSR1,0,dispatch_get_main_queue());
        dispatch_source_set_event_handler(_signal,^{[self startLoad];});dispatch_resume(_signal);
    } else [self startLoad];
    fprintf(stderr,"READY process-local WebKit probe pid=%d\n",getpid());fflush(stderr);
    [NSTimer scheduledTimerWithTimeInterval:_waiting?25:15 target:self selector:@selector(finish:) userInfo:nil repeats:NO];
}
- (void)startLoad
{
    if(_started)return;_started=YES;
    fprintf(stderr,"LOAD process-local WebKit probe\n");fflush(stderr);
    [[_web mainFrame] loadRequest:[NSURLRequest requestWithURL:[NSURL fileURLWithPath:_path]]];
}
- (void)webView:(WebView *)sender didFinishLoadForFrame:(WebFrame *)frame
{
    if(frame!=[sender mainFrame])return;
    _loaded=YES;
    NSString *result=[sender stringByEvaluatingJavaScriptFromString:
        @"JSON.stringify({title:document.title,result:document.getElementById('webgl-status').textContent,ratio:devicePixelRatio})"];
    fprintf(stderr,"RESULT %s\n",[result UTF8String]);fflush(stderr);
    [sender stringByEvaluatingJavaScriptFromString:@"window.scrollTo(0,650)"];
}
- (void)finish:(NSTimer *)timer
{
    (void)timer;
    fprintf(stderr,"FINISHED loaded=%d (inspect RESULT and visible pixels separately)\n",_loaded);
    if(_signal)dispatch_source_cancel(_signal);
    [_web setFrameLoadDelegate:nil];[_window orderOut:nil];
    [NSApp terminate:nil];
}
@end
int main(int argc,char **argv)
{
    @autoreleasepool {
        if((argc!=2&&argc!=3) || (argc==3&&strcmp(argv[2],"--wait-for-tracer")) || argv[1][0]!='/' || access(argv[1],R_OK))return 2;
        [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        VIMWebGLProbe *delegate=[[VIMWebGLProbe alloc] initWithPath:@(argv[1]) wait:argc==3];
        [NSApp setDelegate:delegate];[NSApp run];
        [NSApp setDelegate:nil];[delegate release];
    }
    return 0;
}
