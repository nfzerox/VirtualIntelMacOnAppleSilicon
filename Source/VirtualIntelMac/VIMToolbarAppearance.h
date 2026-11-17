#pragma once
#import <Cocoa/Cocoa.h>

static inline BOOL VIMCompactToolbarEnabled(void) {
    NSNumber *saved=[NSUserDefaults.standardUserDefaults objectForKey:@"VIMCompactToolbar"];
    return saved?saved.boolValue:YES;
}
static inline BOOL VIMUsesUnifiedToolbar(NSWindow *window) {
    return [@[@"VIMLibraryToolbar",@"GuestRunning",@"GuestStopped"] containsObject:window.toolbar.identifier?:@""];
}
static inline void VIMApplyToolbarAppearance(NSWindow *window) {
    // Native preference toolbars have their own layout, not a unified style.
    if(VIMUsesUnifiedToolbar(window)) {
        window.toolbarStyle=VIMCompactToolbarEnabled()?NSWindowToolbarStyleUnifiedCompact:NSWindowToolbarStyleUnified;
        for(NSToolbarItem *item in window.toolbar.items)if(item.view) {
            [item.view invalidateIntrinsicContentSize];
            if([item.view isKindOfClass:NSControl.class])[(NSControl *)item.view sizeToFit];
        }
        window.contentView.superview.needsLayout=YES;
        [window.contentView.superview layoutSubtreeIfNeeded];
    }
}
static inline NSImage *VIMToolbarSymbolImage(NSImage *image) {
    if(!image)return nil;
    NSImage *symbol=[image imageWithSymbolConfiguration:[NSImageSymbolConfiguration configurationWithPointSize:16 weight:NSFontWeightRegular]]?:image;
    // Older AppKit image-only cells still apply SF Symbol baseline placement
    // even after resetting alignmentRect. A plain template canvas removes that
    // baseline without an OS-specific offset or a fixed-resolution bitmap.
    NSImage *canvas=[NSImage imageWithSize:symbol.size flipped:NO drawingHandler:^BOOL(NSRect rect) {
        [symbol drawInRect:rect fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1 respectFlipped:YES hints:nil];
        return YES;
    }];
    canvas.template=YES;canvas.accessibilityDescription=image.accessibilityDescription;
    return canvas;
}
static inline NSButton *VIMToolbarButton(NSImage *image,NSString *label,id target,SEL action) {
    NSButton *button=[NSButton buttonWithImage:VIMToolbarSymbolImage(image) target:target action:action];
    button.toolTip=label;button.accessibilityLabel=label;
    button.bezelStyle=NSBezelStyleTexturedRounded;button.controlSize=NSControlSizeRegular;
    button.imageScaling=NSImageScaleProportionallyDown;button.translatesAutoresizingMaskIntoConstraints=NO;
    button.clipsToBounds=NO;[button sizeToFit];
    return button;
}
