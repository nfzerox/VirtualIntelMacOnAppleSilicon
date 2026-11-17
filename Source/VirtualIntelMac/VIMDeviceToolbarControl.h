#import <Cocoa/Cocoa.h>

/// Older AppKit's grouped menu items need an explicit click action.
@interface VIMDeviceToolbarControl : NSSegmentedControl
@property(copy,readonly) NSArray<NSToolbarItem *> *items;
- (instancetype)initWithItems:(NSArray<NSToolbarItem *> *)items;
@end
