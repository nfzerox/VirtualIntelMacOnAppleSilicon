/* Finite, owned native WindowServer backdrop-filter comparison on Tiger.
 * A constant phase avoids comparing unrelated frames of Dock's animation.
 * No injection, native system changes or replacement effect. */
#import <Cocoa/Cocoa.h>
#include <dlfcn.h>
#include <math.h>
#include <stdlib.h>
#include <sys/sysctl.h>

typedef int (*NewFilter)(uint32_t,CFStringRef,uint32_t *);
typedef int (*FilterValues)(uint32_t,uint32_t,CFDictionaryRef);
typedef int (*WindowFilter)(uint32_t,uint32_t,uint32_t,uint32_t);
typedef int (*RemoveFilter)(uint32_t,uint32_t,uint32_t);
typedef int (*ReleaseFilter)(uint32_t,uint32_t);
static double phase;
static NSWindow *backdrop,*overlay;
static uint32_t connection,filterID;
static RemoveFilter removeFilter;
static ReleaseFilter releaseFilter;

@interface TigerRippleGrid:NSView
@end
@implementation TigerRippleGrid
- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    for(unsigned y=0;y<20;++y)for(unsigned x=0;x<30;++x){
        [[NSColor colorWithCalibratedRed:x/29.0 green:y/19.0 blue:((x+y)&1)?.95:.1 alpha:1] setFill];
        NSRectFill(NSMakeRect(x*20,y*20,20,20));
    }
    [[NSColor whiteColor] setStroke];NSBezierPath *path=[NSBezierPath bezierPathWithRect:NSMakeRect(220,140,160,120)];
    [path setLineWidth:2];[path stroke];
}
@end

@interface TigerRippleOwner:NSObject
@end
@implementation TigerRippleOwner
- (void)applicationDidFinishLaunching:(NSNotification *)notice
{
    (void)notice;
    NSRect rect=NSMakeRect(160,NSHeight([[NSScreen mainScreen] frame])-640,600,400);
    backdrop=[[NSWindow alloc] initWithContentRect:rect styleMask:NSTitledWindowMask backing:NSBackingStoreBuffered defer:NO];
    [backdrop setTitle:@"Owned native WindowServer ripple reference"];
    [backdrop setLevel:NSFloatingWindowLevel];
    [backdrop setContentView:[[[TigerRippleGrid alloc] initWithFrame:NSMakeRect(0,0,600,400)] autorelease]];
    [backdrop makeKeyAndOrderFront:nil];[backdrop display];
    overlay=[[NSWindow alloc] initWithContentRect:rect styleMask:NSBorderlessWindowMask backing:NSBackingStoreBuffered defer:NO];
    [overlay setOpaque:NO];[overlay setBackgroundColor:[NSColor clearColor]];[overlay setHasShadow:NO];
    [overlay setLevel:NSFloatingWindowLevel];
    [overlay setIgnoresMouseEvents:YES];[backdrop addChildWindow:overlay ordered:NSWindowAbove];[overlay display];
    NewFilter create=(NewFilter)dlsym(RTLD_DEFAULT,"CGSNewCIFilterByName");
    FilterValues values=(FilterValues)dlsym(RTLD_DEFAULT,"CGSSetCIFilterValuesFromDictionary");
    WindowFilter add=(WindowFilter)dlsym(RTLD_DEFAULT,"CGSAddWindowFilter");
    uint32_t (*mainConnection)(void)=dlsym(RTLD_DEFAULT,"CGSMainConnectionID");
    removeFilter=(RemoveFilter)dlsym(RTLD_DEFAULT,"CGSRemoveWindowFilter");
    releaseFilter=(ReleaseFilter)dlsym(RTLD_DEFAULT,"CGSReleaseCIFilter");
    if(!create||!values||!add||!mainConnection||!removeFilter||!releaseFilter)exit(77);
    connection=mainConnection();
    NSDictionary *parameters=[NSDictionary dictionaryWithObjectsAndKeys:
        @"[220 140]",@"inputPoint0",@"[380 260]",@"inputPoint1",
        [NSNumber numberWithDouble:12],@"inputCornderRadius",[NSNumber numberWithDouble:phase],@"inputPhase",nil];
    int a=create(connection,CFSTR("CIShapedWaterRipple"),&filterID);
    int b=a?a:values(connection,filterID,(CFDictionaryRef)parameters);
    int c=b?b:add(connection,(uint32_t)[overlay windowNumber],filterID,0x1001);
    fprintf(stderr,"Tiger native ripple phase=%g create=%d values=%d add=%d\n",phase,a,b,c);fflush(stderr);
    if(c)exit(1);
    [NSApp activateIgnoringOtherApps:YES];[overlay orderWindow:NSWindowAbove relativeTo:[backdrop windowNumber]];
    [NSTimer scheduledTimerWithTimeInterval:20 target:self selector:@selector(finish:) userInfo:nil repeats:NO];
}
- (void)finish:(NSTimer *)timer
{
    (void)timer;
    if(filterID){removeFilter(connection,(uint32_t)[overlay windowNumber],filterID);releaseFilter(connection,filterID);}
    [overlay orderOut:nil];[backdrop orderOut:nil];[NSApp terminate:nil];
}
@end
int main(int argc,char **argv)
{
    if(argc!=2)return 2;
    char *end=NULL;phase=strtod(argv[1],&end);if(!end||end==argv[1]||*end||!isfinite(phase)||phase<0||phase>1000)return 2;
    int mib[]={CTL_KERN,KERN_OSRELEASE};char release[64]={0};size_t size=sizeof(release);
    if(sysctl(mib,2,release,&size,NULL,0)||strncmp(release,"8.",2))return 77;
    NSAutoreleasePool *pool=[[NSAutoreleasePool alloc] init];
    [NSApplication sharedApplication];TigerRippleOwner *owner=[[TigerRippleOwner alloc] init];
    [NSApp setDelegate:owner];[NSApp run];[owner release];[pool drain];return 0;
}
