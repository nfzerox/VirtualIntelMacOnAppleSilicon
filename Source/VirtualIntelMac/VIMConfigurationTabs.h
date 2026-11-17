#import <Cocoa/Cocoa.h>

// Older AppKit's tab bezel includes a filled page. Keep the native tab model
// and native segmented selection while matching the preference window's page.
@interface VIMConfigurationTabs : NSView
- (instancetype)initWithTabs:(NSTabView *)tabs;
@end
