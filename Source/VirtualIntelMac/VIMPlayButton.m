#import "VIMPlayButton.h"
#import <QuartzCore/QuartzCore.h>

@interface VIMPlayMaterial : NSVisualEffectView
@end
@implementation VIMPlayMaterial
- (NSView *)hitTest:(NSPoint)point {return nil;}
@end
@interface VIMPlayGlyph : NSImageView
@end
@implementation VIMPlayGlyph
- (NSView *)hitTest:(NSPoint)point {return nil;}
- (BOOL)allowsVibrancy {return YES;}
@end

@interface VIMPlayButton ()
@property VIMPlayMaterial *materialView;
@property VIMPlayGlyph *glyphView;
@property NSTrackingArea *hoverArea;
@end
@implementation VIMPlayButton
- (instancetype)initWithFrame:(NSRect)frame {
    if((self=[super initWithFrame:frame])) {
        self.bordered=NO;self.title=@"";_showsPlayControl=YES;_glyphScale=1;
        self.materialView=[VIMPlayMaterial new];
        self.materialView.material=NSVisualEffectMaterialHUDWindow;
        self.materialView.blendingMode=NSVisualEffectBlendingModeWithinWindow;
        self.materialView.state=NSVisualEffectStateActive;
        self.materialView.wantsLayer=YES;self.materialView.layer.masksToBounds=YES;
        [self addSubview:self.materialView];
        self.glyphView=[VIMPlayGlyph new];
        self.glyphView.image=[NSImage imageWithSystemSymbolName:@"play.fill" accessibilityDescription:nil];
        self.glyphView.imageScaling=NSImageScaleProportionallyUpOrDown;
        self.glyphView.contentTintColor=NSColor.labelColor;
        [self.materialView addSubview:self.glyphView];
    }return self;
}
- (void)setControlDiameter:(CGFloat)value {_controlDiameter=value;self.needsLayout=YES;}
- (void)setGlyphScale:(CGFloat)value {_glyphScale=value;self.needsLayout=YES;}
- (void)setShowsPlayControl:(BOOL)value {
    _showsPlayControl=value;self.materialView.hidden=!value;self.glyphView.hidden=!value;
}
- (void)setContentTintColor:(NSColor *)color {
    [super setContentTintColor:color];self.glyphView.contentTintColor=color;
    // Follow the system appearance/accent, including the stopped guest control.
    // Do not force dark material merely because a glyph has a tint.
    self.materialView.appearance=nil;
}
- (void)updateTrackingAreas {
    [super updateTrackingAreas];if(self.hoverArea)[self removeTrackingArea:self.hoverArea];
    self.hoverArea=[[NSTrackingArea alloc] initWithRect:NSZeroRect options:NSTrackingMouseEnteredAndExited|NSTrackingActiveInKeyWindow|NSTrackingInVisibleRect owner:self userInfo:nil];
    [self addTrackingArea:self.hoverArea];
}
- (void)mouseEntered:(NSEvent *)event {self.materialView.emphasized=YES;self.materialView.alphaValue=1;}
- (void)mouseExited:(NSEvent *)event {self.materialView.emphasized=NO;self.materialView.alphaValue=.9;}
- (void)layout {
    [super layout];
    CGFloat diameter=MIN(NSWidth(self.bounds),NSHeight(self.bounds));
    if(self.controlDiameter>0)diameter=MIN(diameter,self.controlDiameter);
    NSRect circle=NSMakeRect(NSMidX(self.bounds)-diameter/2,NSMidY(self.bounds)-diameter/2,diameter,diameter);
    self.materialView.frame=circle;self.materialView.layer.cornerRadius=diameter/2;
    CGFloat width=diameter*.36*self.glyphScale,height=diameter*.42*self.glyphScale;
    self.glyphView.frame=NSMakeRect(diameter/2-width/2+diameter*.035,diameter/2-height/2,width,height);
}
@end
