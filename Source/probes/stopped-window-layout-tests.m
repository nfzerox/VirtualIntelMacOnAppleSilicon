// Link with the production VIMStoppedMacView implementation; no VM is started.
#import <Cocoa/Cocoa.h>
@interface VIMStoppedMacView : NSView
@property NSImageView *wallpaper;
@property NSButton *playButton;
- (instancetype)initWithGuest:(NSString *)guest name:(NSString *)name target:(id)target action:(SEL)action;
@end
int main(void) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        NSImage *image=[[NSImage alloc] initWithSize:NSMakeSize(1920,1200)];
        [image setName:@"fixtureWallpaper"];
        NSImage *mark=[[NSImage alloc] initWithSize:NSMakeSize(56,56)];[mark setName:@"fixtureProductIcon"];
        VIMStoppedMacView *view=[[VIMStoppedMacView alloc] initWithGuest:@"fixture" name:@"A deliberately long virtual Mac name to exercise caption truncation" target:nil action:NULL];
        NSWindow *window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,800,600)
            styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskResizable backing:NSBackingStoreBuffered defer:NO];
        window.contentView=view;window.minSize=NSMakeSize(440,320);window.maxSize=NSMakeSize(100000,100000);
        const NSSize shapes[]={{500,1000},{1600,400},{440,268},{2200,1200}};
        for(NSUInteger i=0;i<sizeof(shapes)/sizeof(shapes[0]);i++) {
            [window setContentSize:shapes[i]];
            [view layoutSubtreeIfNeeded];
            NSSize fit=view.fittingSize;
            printf("%.0fx%.0f viewport: background fitting size %.0fx%.0f\n",shapes[i].width,shapes[i].height,fit.width,fit.height);
            NSCAssert(fit.width<440&&fit.height<268,@"Wallpaper must not constrain window dimensions");
            NSCAssert(window.maxSize.width>2200&&window.maxSize.height>1200,@"Full stopped presentation must not cap window size");
            NSCAssert(fabs(NSWidth(view.bounds)-shapes[i].width)<1&&fabs(NSHeight(view.bounds)-shapes[i].height)<1,@"Real window accepts independent dimensions");
            for(NSLayoutConstraint *constraint in view.constraints)
                NSCAssert(!((constraint.firstItem==view.playButton&&constraint.secondItem==view)||
                            (constraint.firstItem==view&&constraint.secondItem==view.playButton)),
                          @"Stopped controls must not couple a capped Play diameter to the window width");
            NSCAssert(NSWidth(view.playButton.frame)<=260,@"Play artwork stays capped without capping its window");
            NSCAssert(NSContainsRect(NSInsetRect(view.wallpaper.frame,-.01,-.01),view.bounds),@"Wallpaper covers both wide and tall windows");
        }
        puts("PASS: complete stopped window accepts wide/tall sizing, including widths beyond 1296");
    }
}
