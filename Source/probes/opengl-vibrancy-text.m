// Standalone 10.10+ control for native/forwarded AppKit material composition.
// Compare ordinary and vibrant glyphs over the same pixels in one frame.
#import <AppKit/AppKit.h>

@interface VIMMaterialLabel : NSTextField
@property(nonatomic) BOOL vibrant;
@end
@implementation VIMMaterialLabel
- (BOOL)allowsVibrancy { return self.vibrant; }
@end

@interface VIMProbeDelegate : NSObject <NSApplicationDelegate>
@end
@implementation VIMProbeDelegate
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)app {
    (void)app;
    return YES;
}
@end

@interface VIMGradientView : NSView
@end
@implementation VIMGradientView
- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    NSGradient *gradient=[[NSGradient alloc]
        initWithStartingColor:[NSColor colorWithSRGBRed:.15 green:.42 blue:.65 alpha:1]
        endingColor:[NSColor colorWithSRGBRed:.95 green:.76 blue:.56 alpha:1]];
    [gradient drawInRect:self.bounds angle:20];
    [gradient release];
}
@end

static void AddLabel(NSView *parent,CGFloat y,BOOL vibrant,NSString *text) {
    VIMMaterialLabel *label=[[VIMMaterialLabel alloc] initWithFrame:NSMakeRect(16,y,263,30)];
    label.vibrant=vibrant;
    label.stringValue=text;
    label.font=[NSFont systemFontOfSize:19];
    label.textColor=[NSColor labelColor];
    label.bezeled=NO;
    label.drawsBackground=NO;
    label.editable=NO;
    label.selectable=NO;
    [parent addSubview:label];
    [label release];
}

static void AddMaterial(NSView *parent,CGFloat x,CGFloat y,NSVisualEffectMaterial material,
                        NSVisualEffectBlendingMode blending) {
    NSVisualEffectView *view=[[NSVisualEffectView alloc] initWithFrame:NSMakeRect(x,y,295,113)];
    view.material=material;
    view.appearance=[NSAppearance appearanceNamed:material==NSVisualEffectMaterialDark
        ? NSAppearanceNameVibrantDark : NSAppearanceNameVibrantLight];
    view.blendingMode=blending;
    view.state=NSVisualEffectStateActive;
    [parent addSubview:view];
    AddLabel(view,63,NO,@"Ordinary label: AaBb 123");
    AddLabel(view,22,YES,@"Vibrant label: AaBb 123");
    [view release];
}

int main(void) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        VIMProbeDelegate *delegate=[VIMProbeDelegate new];
        [NSApp setDelegate:delegate];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        NSWindow *window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,625,252)
            styleMask:NSTitledWindowMask|NSClosableWindowMask
            backing:NSBackingStoreBuffered defer:NO];
        window.title=@"Material probe: light / dark, within / behind";
        window.releasedWhenClosed=NO;
        VIMGradientView *backdrop=[[VIMGradientView alloc] initWithFrame:window.contentView.bounds];
        backdrop.wantsLayer=YES;
        [window setContentView:backdrop];
        AddMaterial(backdrop,12,128,NSVisualEffectMaterialLight,NSVisualEffectBlendingModeWithinWindow);
        AddMaterial(backdrop,318,128,NSVisualEffectMaterialDark,NSVisualEffectBlendingModeWithinWindow);
        AddMaterial(backdrop,12,11,NSVisualEffectMaterialLight,NSVisualEffectBlendingModeBehindWindow);
        AddMaterial(backdrop,318,11,NSVisualEffectMaterialDark,NSVisualEffectBlendingModeBehindWindow);
        [backdrop release];
        [window center];
        [window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
        [NSApp run];
        [window release];
        [delegate release];
    }
    return 0;
}
