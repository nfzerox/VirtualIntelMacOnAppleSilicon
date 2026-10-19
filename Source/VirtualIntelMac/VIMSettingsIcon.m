#import "VIMSettingsIcon.h"

@interface VIMSettingsIcon ()
@property NSColor *color;
@end
@implementation VIMSettingsIcon
- (instancetype)initWithSymbol:(NSString *)symbol color:(NSColor *)color {
    return [self initWithSymbol:symbol color:color glyphSize:20];
}
- (instancetype)initWithSymbol:(NSString *)symbol color:(NSColor *)color glyphSize:(CGFloat)glyphSize {
    if((self=[super initWithFrame:NSMakeRect(0,0,32,32)])) {
        _color=color;
        NSImage *symbolImage=[NSImage imageWithSystemSymbolName:symbol accessibilityDescription:nil]?:[NSImage imageWithSystemSymbolName:@"gearshape" accessibilityDescription:nil];
        NSImage *art=[symbolImage
            imageWithSymbolConfiguration:[NSImageSymbolConfiguration configurationWithPointSize:17 weight:NSFontWeightMedium]];
        NSImageView *glyph=[NSImageView imageViewWithImage:art];
        glyph.contentTintColor=NSColor.whiteColor;glyph.imageScaling=NSImageScaleProportionallyUpOrDown;
        glyph.translatesAutoresizingMaskIntoConstraints=NO;[self addSubview:glyph];
        [NSLayoutConstraint activateConstraints:@[[glyph.centerXAnchor constraintEqualToAnchor:self.centerXAnchor],
            [glyph.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
            [glyph.widthAnchor constraintEqualToConstant:glyphSize],[glyph.heightAnchor constraintEqualToConstant:glyphSize]]];
        self.accessibilityElement=NO;glyph.accessibilityElement=NO;
    }
    return self;
}
- (NSSize)intrinsicContentSize {return NSMakeSize(32,32);}
- (BOOL)allowsVibrancy {return NO;}
- (void)drawRect:(NSRect)dirtyRect {
    NSRect box=NSInsetRect(self.bounds,2,2);
    NSBezierPath *shape=[NSBezierPath bezierPathWithRoundedRect:box xRadius:6 yRadius:6];
    [NSGraphicsContext saveGraphicsState];
    NSShadow *shadow=[NSShadow new];shadow.shadowOffset=NSMakeSize(0,-1);shadow.shadowBlurRadius=1.5;
    shadow.shadowColor=[NSColor.blackColor colorWithAlphaComponent:.23];[shadow set];
    [self.color setFill];[shape fill];[NSGraphicsContext restoreGraphicsState];
    NSColor *light=[self.color blendedColorWithFraction:.20 ofColor:NSColor.whiteColor];
    NSColor *dark=[self.color blendedColorWithFraction:.08 ofColor:NSColor.blackColor];
    [[[NSGradient alloc] initWithStartingColor:light endingColor:dark] drawInBezierPath:shape angle:-90];
    [[NSColor.whiteColor colorWithAlphaComponent:.20] setStroke];shape.lineWidth=.5;[shape stroke];
}
@end
