/* Real AppKit toolbar/CPU content, with optional layer backing. Compare first
 * paint with the same finite fixture on native Intel; no driver interposition. */
#import <Cocoa/Cocoa.h>
#include <unistd.h>
#if MAC_OS_X_VERSION_MAX_ALLOWED < 1060
// Compile the same Mojave runtime fixture against a period SDK too: AppKit's
// linked-on compatibility can select a different toolbar composition path.
@protocol NSToolbarDelegate <NSObject> @end
@interface NSApplication (ProbeActivation)
- (BOOL)setActivationPolicy:(NSInteger)policy;
@end
#define NSApplicationActivationPolicyRegular 0
#endif
@interface ToolbarProbe : NSObject <NSToolbarDelegate> {
    NSWindow *_window;
}
- (void)finish:(NSTimer *)timer;
@end
@implementation ToolbarProbe
- (NSArray *)toolbarAllowedItemIdentifiers:(NSToolbar *)toolbar
{ (void)toolbar;return @[@"add",@"folder",@"info"]; }
- (NSArray *)toolbarDefaultItemIdentifiers:(NSToolbar *)toolbar
{ return [self toolbarAllowedItemIdentifiers:toolbar]; }
- (NSToolbarItem *)toolbar:(NSToolbar *)toolbar itemForItemIdentifier:(NSString *)identifier willBeInsertedIntoToolbar:(BOOL)inserted
{
    (void)toolbar;(void)inserted;
    NSToolbarItem *item=[[[NSToolbarItem alloc]initWithItemIdentifier:identifier]autorelease];
    NSDictionary *images=@{@"add":NSImageNameAddTemplate,@"folder":@"NSFolder",@"info":NSImageNameInfo};
    item.label=identifier;item.image=[NSImage imageNamed:[images objectForKey:identifier]];item.target=self;item.action=@selector(noop:);
    return item;
}
- (void)noop:(id)sender { (void)sender; }
- (void)finish:(NSTimer *)timer
{
    (void)timer;const char *path=getenv("VIM_TOOLBAR_CAPTURE");
    CGImageRef image=CGWindowListCreateImage(CGRectNull,kCGWindowListOptionIncludingWindow,
        _window.windowNumber,kCGWindowImageBoundsIgnoreFraming);
    if(!image)exit(2);
    if(path) {
        NSBitmapImageRep *bitmap=[[[NSBitmapImageRep alloc]initWithCGImage:image]autorelease];
        NSData *data=[bitmap representationUsingType:NSPNGFileType properties:@{}];
        if(![data writeToFile:[NSString stringWithUTF8String:path] atomically:YES])exit(2);
    }
    fprintf(stderr,"first AppKit paint captured=%zux%zu window=%ld layered=%d\n",CGImageGetWidth(image),CGImageGetHeight(image),
        (long)_window.windowNumber,getenv("VIM_TOOLBAR_LAYERED")!=NULL);
    CGImageRelease(image);exit(0);
}
- (void)run
{
    _window=[[NSWindow alloc]initWithContentRect:NSMakeRect(100,180,560,240)
        styleMask:NSTitledWindowMask|NSClosableWindowMask backing:NSBackingStoreBuffered defer:NO];
    _window.title=@"Initial AppKit toolbar contract";
    NSToolbar *toolbar=[[[NSToolbar alloc]initWithIdentifier:@"VIMNativeToolbarProbe"]autorelease];
    toolbar.delegate=self;toolbar.displayMode=NSToolbarDisplayModeIconAndLabel;_window.toolbar=toolbar;
    NSView *content=_window.contentView;content.wantsLayer=getenv("VIM_TOOLBAR_LAYERED")!=NULL;
    NSTextField *label=[[[NSTextField alloc]initWithFrame:NSMakeRect(28,140,500,34)]autorelease];
    label.stringValue=@"Initial text and toolbar images must be visible.";
    label.editable=NO;label.bordered=NO;label.drawsBackground=NO;[content addSubview:label];
    NSButton *check=[[[NSButton alloc]initWithFrame:NSMakeRect(28,80,300,30)]autorelease];
    check.buttonType=NSSwitchButton;check.title=@"Native AppKit checkbox";check.state=NSOnState;[content addSubview:check];
    [_window makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES];[_window orderFrontRegardless];
    [NSTimer scheduledTimerWithTimeInterval:2 target:self selector:@selector(finish:) userInfo:nil repeats:NO];
    [NSApp run];
}
@end
int main(void)
{
    alarm(15);@autoreleasepool {
        [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        ToolbarProbe *probe=[ToolbarProbe new];[probe run];return 2;
    }
}
