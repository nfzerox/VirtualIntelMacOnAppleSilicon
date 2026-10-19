#import <Cocoa/Cocoa.h>

/// Small, native settings artwork drawn from SF Symbols at the display's scale.
/// No proprietary bitmaps, cached screenshots, or per-frame rendering work.
@interface VIMSettingsIcon : NSView
- (instancetype)initWithSymbol:(NSString *)symbol color:(NSColor *)color;
- (instancetype)initWithSymbol:(NSString *)symbol color:(NSColor *)color glyphSize:(CGFloat)glyphSize;
@end
