// Finite comparison of Tiger's stock Welcome movie presentation. Build with
// the genuine 10.4u SDK/linker and launch as a normal app in the desktop session.
// No account, Setup Assistant state, decoder or graphics policy is modified.
#import <Cocoa/Cocoa.h>
#import <QTKit/QTKit.h>
#import <QuickTime/QuickTime.h>
#include <stdio.h>
#include <unistd.h>

@interface WelcomeViewProbe : NSObject {
    NSWindow *window;
    QTMovieView *view;
    QTMovie *movie;
}
- (void)begin:(NSTimer *)timer;
- (void)finish:(NSTimer *)timer;
@end

@implementation WelcomeViewProbe
- (void)begin:(NSTimer *)timer {
    (void)timer;
    NSBundle *settings=[NSBundle mainBundle];
    BOOL transparent=[[settings objectForInfoDictionaryKey:@"ProbeTransparent"] boolValue];
    BOOL original=[[settings objectForInfoDictionaryKey:@"ProbeOriginalView"] boolValue];
    NSString *resource=@"/System/Library/CoreServices/Setup Assistant.app/Contents/Resources/TransitionSection.bundle";
    Class kind=[QTMovieView class];
    if(original) {
        if(![[NSBundle bundleWithPath:resource] load] || !(kind=NSClassFromString(@"MBIntroMovieView"))) {
            NSLog(@"Welcome probe: cannot load original movie view");[NSApp terminate:nil];return;
        }
    }
    NSError *error=nil;
    movie=[[QTMovie alloc] initWithFile:[resource stringByAppendingPathComponent:@"Contents/Resources/intro.mov"] error:&error];
    if(!movie){NSLog(@"Welcome probe: movie open failed %@",error);[NSApp terminate:nil];return;}
    NSRect frame=NSMakeRect(100,120,800,600);
    window=[[NSWindow alloc] initWithContentRect:frame styleMask:NSBorderlessWindowMask
                                       backing:NSBackingStoreBuffered defer:NO];
    [window setReleasedWhenClosed:NO];
    [window setOpaque:!transparent];
    [window setBackgroundColor:transparent?[NSColor clearColor]:[NSColor blackColor]];
    view=[[kind alloc] initWithFrame:NSMakeRect(0,0,800,600)];
    [view setControllerVisible:NO];[view setPreservesAspectRatio:YES];
    [window setContentView:view];[view setMovie:movie];
    [view gotoBeginning:nil];[window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    NSLog(@"Welcome probe: view=%@ transparent=%d opaqueView=%d native movie playback begins",
          NSStringFromClass(kind),transparent,[view isOpaque]);
    [view play:nil];
    [NSTimer scheduledTimerWithTimeInterval:30 target:self selector:@selector(finish:) userInfo:nil repeats:NO];
}
- (void)finish:(NSTimer *)timer {
    (void)timer;[movie stop];[window orderOut:nil];[NSApp terminate:nil];
}
- (void)dealloc {
    [view release];[movie release];[window release];[super dealloc];
}
@end

int main(void) {
    NSAutoreleasePool *pool=[NSAutoreleasePool new];
    [NSApplication sharedApplication];
    WelcomeViewProbe *probe=[WelcomeViewProbe new];
    [NSTimer scheduledTimerWithTimeInterval:0 target:probe selector:@selector(begin:) userInfo:nil repeats:NO];
    [NSApp run];[probe release];[pool drain];return 0;
}
