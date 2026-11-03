/* Owned AppKit table/header + native Core Animation composition fixture.
 * The same binary runs on accelerated physical Mavericks and the guest.
 * No injection, screenshot API, synthetic input or desktop settings change.
 */
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
@interface VIMComposition : NSObject <NSTableViewDataSource>
@property(retain) NSWindow *window;
@property unsigned step;
@end
@implementation VIMComposition
- (NSInteger)numberOfRowsInTableView:(NSTableView *)table { (void)table;return 16; }
- (id)tableView:(NSTableView *)table objectValueForTableColumn:(NSTableColumn *)column row:(NSInteger)row
{ (void)table;return [column.identifier isEqual:@"Name"] ? [NSString stringWithFormat:@"Native item %02ld",(long)row+1] : @"Layer composition"; }
- (void)move:(NSTimer *)timer
{
    if(self.step==6) { [timer invalidate];[NSApp terminate:nil];return; }
    NSRect frame=self.window.frame;frame.origin.x+=(self.step&1)?-32:48;frame.origin.y+=(self.step&1)?16:-24;
    [self.window setFrame:frame display:YES];++self.step;
    fprintf(stderr,"native table/layer window move=%u\n",self.step);
}
@end
int main(void)
{
    @autoreleasepool {
        NSApplication *app=[NSApplication sharedApplication];[app setActivationPolicy:NSApplicationActivationPolicyRegular];
        VIMComposition *probe=[VIMComposition new];
        probe.window=[[NSWindow alloc] initWithContentRect:NSMakeRect(150,150,640,440)
            styleMask:NSTitledWindowMask|NSClosableWindowMask|NSResizableWindowMask
            backing:NSBackingStoreBuffered defer:NO];
        [probe.window setTitle:@"Native table headers and layer composition"];
        NSView *root=[[NSView alloc] initWithFrame:NSMakeRect(0,0,640,440)];
        [root setWantsLayer:getenv("VIM_CA_UNBACKED")==NULL];
        CAGradientLayer *gradient=[CAGradientLayer layer];gradient.frame=NSMakeRect(0,0,640,440);
        gradient.colors=@[(id)[NSColor colorWithCalibratedRed:.2 green:.5 blue:.8 alpha:1].CGColor,
                          (id)[NSColor colorWithCalibratedRed:.8 green:.4 blue:.2 alpha:1].CGColor];
        [root.layer addSublayer:gradient];
        if(getenv("VIM_CA_CONTINUOUS")) {
            // Sustained, server-side work for bounded profiles. Sparse moves
            // alone leave most samples idle and cannot locate frame costs.
            CABasicAnimation *animation=[CABasicAnimation animationWithKeyPath:@"opacity"];
            animation.fromValue=@1;animation.toValue=@.25;animation.duration=1;
            animation.autoreverses=YES;animation.repeatCount=HUGE_VALF;
            [gradient addAnimation:animation forKey:@"Owned continuous composition"];
        }
        CALayer *effect=[CALayer layer];effect.frame=NSMakeRect(50,18,260,65);
        effect.backgroundColor=[NSColor colorWithCalibratedWhite:.5 alpha:.7].CGColor;
        effect.cornerRadius=16;
        const char *blend=getenv("VIM_CA_BLEND_FILTER");
        if(blend) {
            // Matching native QuartzCore _copyRenderLayer explicitly accepts
            // NSString atoms via Filter::shared_filter. This is not the
            // unsupported remote CIFilter fixture tried earlier.
            effect.compositingFilter=[NSString stringWithUTF8String:blend];
            fprintf(stderr,"native CA string compositing filter=%s\n",blend);
        }
        [root.layer addSublayer:effect];
        NSScrollView *scroll=[[NSScrollView alloc] initWithFrame:NSMakeRect(20,110,600,310)];
        scroll.hasVerticalScroller=YES;scroll.borderType=NSBezelBorder;
        NSTableView *table=[[NSTableView alloc] initWithFrame:NSMakeRect(0,0,580,310)];
        for(NSString *name in @[@"Name",@"Kind"]) {
            NSTableColumn *column=[[NSTableColumn alloc] initWithIdentifier:name];
            [column.headerCell setStringValue:name];column.width=280;[table addTableColumn:column];
        }
        table.dataSource=probe;table.usesAlternatingRowBackgroundColors=YES;
        scroll.documentView=table;[root addSubview:scroll];probe.window.contentView=root;
        [probe.window makeKeyAndOrderFront:nil];[app activateIgnoringOtherApps:YES];
        [NSTimer scheduledTimerWithTimeInterval:2 target:probe selector:@selector(move:) userInfo:nil repeats:YES];
        [app run];
    }
    return 0;
}
