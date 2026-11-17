#import "VIMDeviceToolbarControl.h"
#import "VIMToolbarAppearance.h"

@implementation VIMDeviceToolbarControl
- (instancetype)initWithItems:(NSArray<NSToolbarItem *> *)items {
    if((self=[super initWithFrame:NSZeroRect])) {
        _items=[items copy];self.segmentCount=items.count;
        self.trackingMode=NSSegmentSwitchTrackingMomentary;
        self.segmentStyle=NSSegmentStyleTexturedRounded;self.controlSize=NSControlSizeRegular;
        self.target=self;self.action=@selector(activateSegment:);
        for(NSUInteger i=0;i<items.count;i++) {
            [self setImage:VIMToolbarSymbolImage(items[i].image) forSegment:i];
            [self setImageScaling:NSImageScaleProportionallyDown forSegment:i];
            [self setToolTip:items[i].toolTip?:items[i].label forSegment:i];
            [self setWidth:32 forSegment:i];[self setShowsMenuIndicator:NO forSegment:i];
        }
        self.translatesAutoresizingMaskIntoConstraints=NO;self.clipsToBounds=NO;
        // AppKit includes bezel/alignment outsets in this size. Fixed frame
        // dimensions crop the control when changing the window's toolbar style.
        [self sizeToFit];
    }return self;
}
- (void)activateSegment:(id)sender {
    NSInteger index=self.selectedSegment;if(index<0||index>=self.items.count)return;
    NSToolbarItem *item=self.items[index];
    if([item isKindOfClass:NSMenuToolbarItem.class]) {
        NSPoint anchor=NSMakePoint(NSMinX(self.bounds)+index*32,self.isFlipped?NSMaxY(self.bounds)+3:NSMinY(self.bounds)-3);
        [((NSMenuToolbarItem *)item).menu popUpMenuPositioningItem:nil atLocation:anchor inView:self];
    } else if(item.action)[NSApp sendAction:item.action to:item.target from:item];
    [self setSelected:NO forSegment:index];
}
@end
