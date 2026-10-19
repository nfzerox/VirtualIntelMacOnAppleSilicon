#import <Cocoa/Cocoa.h>

/// Native button with an oval backdrop material and a separate SF play glyph.
/// The hit area can cover a thumbnail while only its center shows the control.
@interface VIMPlayButton : NSButton
@property(nonatomic) CGFloat controlDiameter; // zero fills the smaller dimension
@property(nonatomic) CGFloat glyphScale; // one by default; does not enlarge the material
@property(nonatomic) BOOL showsPlayControl;
@end
