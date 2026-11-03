/* Owned WindowServer backdrop-filter reference, not the offscreen CI/OpenCL
 * route. The filter ABI, five keys and flags are from matching Mavericks
 * Dock DBRippleInfo disassembly. Never inject into Dock or alter preferences.
 * A fixed phase makes native/guest screen evidence comparable. Exit after a
 * finite interval and remove only our own filter/windows.
 */
#import <Cocoa/Cocoa.h>
#include <dlfcn.h>
#include <math.h>
#include <string.h>
typedef int (*VIMNewFilter)(uint32_t,CFStringRef,uint32_t *);
typedef int (*VIMFilterValues)(uint32_t,uint32_t,CFDictionaryRef);
typedef int (*VIMWindowFilter)(uint32_t,uint32_t,uint32_t,uint32_t);
typedef int (*VIMRemoveFilter)(uint32_t,uint32_t,uint32_t);
typedef int (*VIMReleaseFilter)(uint32_t,uint32_t);
static uint32_t connection,filterID;
static NSWindow *backdrop,*overlay;
static VIMRemoveFilter removeFilter;
static VIMReleaseFilter releaseFilter;
@interface VIMRippleGrid:NSView
@end
@implementation VIMRippleGrid
- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    for(unsigned y=0;y<20;++y)for(unsigned x=0;x<30;++x){
        [[NSColor colorWithCalibratedRed:x/29.0 green:y/19.0
            blue:((x+y)&1)?.95:.1 alpha:1] setFill];
        NSRectFill(NSMakeRect(x*20,y*20,20,20));
    }
    [[NSColor whiteColor] setStroke];NSBezierPath *path=[NSBezierPath bezierPathWithRoundedRect:
        NSMakeRect(220,140,160,120) xRadius:12 yRadius:12];path.lineWidth=2;[path stroke];
}
@end
@interface VIMRippleOwner:NSObject<NSApplicationDelegate>
@property double phase;
@property BOOL backdropOnly;
@end
@implementation VIMRippleOwner
- (void)applicationDidFinishLaunching:(NSNotification *)notice
{
    /* CGS backdrop-filter coordinates are screen-relative. Keep the same
     * TOP-left on different-height native/guest Retina panels, rather than
     * accidentally comparing different parts of the expanding wave. */
    (void)notice;CGFloat screenHeight=NSScreen.mainScreen.frame.size.height;
    if(screenHeight<700)exit(2);
    NSRect rect=NSMakeRect(160,screenHeight-640,600,400);
    backdrop=[[NSWindow alloc] initWithContentRect:rect styleMask:NSTitledWindowMask
        backing:NSBackingStoreBuffered defer:NO];
    backdrop.title=@"Owned native WindowServer ripple reference";
    backdrop.contentView=[[VIMRippleGrid alloc] initWithFrame:NSMakeRect(0,0,600,400)];
    [backdrop makeKeyAndOrderFront:nil];[backdrop display];
    if(self.backdropOnly){
        /* A contrast source under a REAL Dock-controlled widget insertion,
         * not another filter competing with the effect under observation. */
        backdrop.title=@"Owned contrast backdrop (no filter)";
        [NSApp activateIgnoringOtherApps:YES];
        fprintf(stderr,"READY contrast backdrop: no CGS filter, 12-second lifetime\n");fflush(stderr);
        [NSTimer scheduledTimerWithTimeInterval:12 target:self selector:@selector(finish:) userInfo:nil repeats:NO];
        return;
    }
    overlay=[[NSWindow alloc] initWithContentRect:rect styleMask:NSBorderlessWindowMask
        backing:NSBackingStoreBuffered defer:NO];
    overlay.opaque=NO;overlay.backgroundColor=NSColor.clearColor;overlay.hasShadow=NO;
    overlay.ignoresMouseEvents=YES;
    [backdrop addChildWindow:overlay ordered:NSWindowAbove];[overlay display];
    VIMNewFilter create=dlsym(RTLD_DEFAULT,"CGSNewCIFilterByName");
    VIMFilterValues values=dlsym(RTLD_DEFAULT,"CGSSetCIFilterValuesFromDictionary");
    VIMWindowFilter add=dlsym(RTLD_DEFAULT,"CGSAddWindowFilter");
    uint32_t (*mainConnection)(void)=dlsym(RTLD_DEFAULT,"CGSMainConnectionID");
    if(!mainConnection)mainConnection=dlsym(RTLD_DEFAULT,"_CGSDefaultConnection");
    removeFilter=dlsym(RTLD_DEFAULT,"CGSRemoveWindowFilter");releaseFilter=dlsym(RTLD_DEFAULT,"CGSReleaseCIFilter");
    if(!create||!values||!add||!mainConnection||!removeFilter||!releaseFilter)exit(2);
    connection=mainConnection();
    NSDictionary *parameters=@{@"inputPoint0":@"[220 140]",@"inputPoint1":@"[380 260]",
        /* Dock really spells this key Cornder, unlike the public filter. */
        @"inputCornderRadius":@12,@"inputPhase":@(self.phase)};
    int a=create(connection,CFSTR("CIShapedWaterRipple"),&filterID);
    int b=a?a:values(connection,filterID,(CFDictionaryRef)parameters);
    int c=b?b:add(connection,(uint32_t)overlay.windowNumber,filterID,0x1001);
    fprintf(stderr,"ripple window=%ld connection=%x filter=%u phase=%g create=%d values=%d add=%d\n",
        (long)overlay.windowNumber,connection,filterID,self.phase,a,b,c);
    if(c)exit(2);
    [NSApp activateIgnoringOtherApps:YES];
    [overlay orderWindow:NSWindowAbove relativeTo:backdrop.windowNumber];
    [NSTimer scheduledTimerWithTimeInterval:12 target:self selector:@selector(finish:) userInfo:nil repeats:NO];
}
- (void)finish:(NSTimer *)timer
{
    (void)timer;if(filterID){removeFilter(connection,(uint32_t)overlay.windowNumber,filterID);
        releaseFilter(connection,filterID);}
    [overlay orderOut:nil];[backdrop orderOut:nil];[NSApp terminate:nil];
}
@end
int main(int argc,char **argv){@autoreleasepool {
    if(argc!=2)return 2;BOOL backdropOnly=!strcmp(argv[1],"--backdrop-only");
    char *end=NULL;double phase=backdropOnly?0:strtod(argv[1],&end);
    if(!backdropOnly&&(!end||end==argv[1]||*end||!isfinite(phase)||phase<0||phase>1000))return 2;
    [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    VIMRippleOwner *owner=[VIMRippleOwner new];owner.phase=phase;owner.backdropOnly=backdropOnly;
    [NSApp setDelegate:owner];[NSApp run];return 0;
}}
