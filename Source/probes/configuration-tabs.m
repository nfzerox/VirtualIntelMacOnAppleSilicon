// Actual production tab view, rendered by AppKit. No VM, synthetic host input
// or screenshot permission. --simulate-old restores the removed solid backing
// and must fail the no-backing assertion.
#import <Cocoa/Cocoa.h>
#import "../VirtualIntelMac/VIMConfigurationTabs.h"

static NSData *Pixels(NSView *view,NSRect rect) {
    NSBitmapImageRep *bitmap=[view bitmapImageRepForCachingDisplayInRect:rect];
    [view cacheDisplayInRect:rect toBitmapImageRep:bitmap];
    return bitmap?[NSData dataWithBytes:bitmap.bitmapData length:bitmap.bytesPerRow*bitmap.pixelsHigh]:nil;
}
static NSUInteger Differences(NSData *a,NSData *b) {
    if(!a.length||a.length!=b.length)return NSUIntegerMax;
    const unsigned char *p=a.bytes,*q=b.bytes;NSUInteger count=0;
    for(NSUInteger i=0;i<a.length;i++)count+=p[i]!=q[i];
    return count;
}
int main(int argc,const char **argv) { @autoreleasepool {
    BOOL old=argc==2&&!strcmp(argv[1],"--simulate-old");
    if(argc!=1&&!old)return 64;
    [NSApplication sharedApplication];unsigned failures=0;
    NSWindow *window=[[NSWindow alloc] initWithContentRect:NSMakeRect(120,120,500,320)
        styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
    window.title=@"Native configuration tabs regression";window.releasedWhenClosed=NO;
    for(NSString *appearance in @[NSAppearanceNameAqua,NSAppearanceNameDarkAqua]) {
        window.appearance=[NSAppearance appearanceNamed:appearance];
        NSBox *root=[NSBox new];root.boxType=NSBoxCustom;root.borderWidth=0;
        root.fillColor=NSColor.windowBackgroundColor;window.contentView=root;
        NSTabView *tabs=[NSTabView new];
        for(NSString *name in @[@"This Mac",@"Virtual Mac"]) {
            NSTabViewItem *item=[[NSTabViewItem alloc] initWithIdentifier:name];
            item.label=name;item.view=[NSView new];[tabs addTabViewItem:item];
        }
        VIMConfigurationTabs *view=[[VIMConfigurationTabs alloc] initWithTabs:tabs];
        view.frame=NSMakeRect(20,20,460,280);[root addSubview:view];
        [window orderFront:nil];[view layoutSubtreeIfNeeded];
        NSBox *frame=nil,*backing=nil;NSSegmentedControl *selection=nil;
        for(NSView *child in view.subviews) {
            if([child.identifier isEqual:@"ConfigurationTabFrame"])frame=(NSBox *)child;
            if([child.identifier isEqual:@"ConfigurationTabSelectionBacking"])backing=(NSBox *)child;
            if([child isKindOfClass:NSSegmentedControl.class])selection=(NSSegmentedControl *)child;
        }
        if(!frame||!selection)return 2;
        if(old) {
            backing=[NSBox new];backing.boxType=NSBoxCustom;backing.borderWidth=0;
            backing.fillColor=NSColor.windowBackgroundColor;backing.cornerRadius=6;
            backing.frame=NSInsetRect(selection.frame,-2,0);
            [view addSubview:backing positioned:NSWindowBelow relativeTo:selection];
        }
        if(backing){fputs("FAIL: unexpected solid selection backing\n",stderr);failures++;}
        if(tabs.drawsBackground||tabs.tabViewType!=NSNoTabsNoBorder||frame.fillColor.alphaComponent!=0)return 4;
        for(NSInteger segment=0;segment<2;segment++) {
            selection.selectedSegment=segment;[selection sendAction:selection.action to:selection.target];
            [view layoutSubtreeIfNeeded];
            if(tabs.selectedTabViewItem!=tabs.tabViewItems[segment])return 3;
            NSRect control=NSInsetRect(selection.frame,2,2);
            NSRect outside=NSMakeRect(4,NSMaxY(frame.frame)-3,NSMinX(selection.frame)-10,6);
            frame.hidden=NO;NSData *with=Pixels(view,control),*border=Pixels(view,outside);
            frame.hidden=YES;NSData *without=Pixels(view,control),*noBorder=Pixels(view,outside);
            frame.hidden=NO;
            NSUInteger inside=Differences(with,without),edges=Differences(border,noBorder);
            // Native translucent segments may reveal the page's outline.
            // Do not hide it with another painted/opaque control background.
            BOOL okay=edges>0&&edges!=NSUIntegerMax;
            printf("%s appearance=%s selected=%ld frame_under_control=%lu frame_outside=%lu\n",
                okay?"PASS":"FAIL",appearance.UTF8String,(long)segment,(unsigned long)inside,(unsigned long)edges);
            failures+=!okay;
        }
    }
    [window close];return failures?1:0;
} }
