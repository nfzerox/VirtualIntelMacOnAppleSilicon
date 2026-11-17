// Finite offscreen AppKit rendering fixture; no VM or screen-capture permission.
#import <Cocoa/Cocoa.h>
#import "../VirtualIntelMac/VIMToolbarAppearance.h"

int main(int argc,const char *argv[]) {@autoreleasepool {
    if(argc!=2)return 64;
    [NSApplication sharedApplication];
    NSView *view=[[NSView alloc] initWithFrame:NSMakeRect(0,0,640,180)];
    NSArray *names=@[@"cpu",@"externaldrive",@"opticaldisc",@"network",@"folder",@"power",@"gearshape"];
    for(NSInteger row=0;row<2;row++) {
        NSSegmentedControl *control=[[NSSegmentedControl alloc] initWithFrame:NSZeroRect];
        control.segmentStyle=NSSegmentStyleTexturedRounded;control.controlSize=NSControlSizeRegular;
        control.segmentCount=names.count;
        for(NSInteger i=0;i<names.count;i++) {
            NSImage *symbol=[NSImage imageWithSystemSymbolName:names[i] accessibilityDescription:nil];
            NSImage *baseline=[[symbol imageWithSymbolConfiguration:[NSImageSymbolConfiguration configurationWithPointSize:16 weight:NSFontWeightRegular]] copy];
            baseline.alignmentRect=NSMakeRect(0,0,baseline.size.width,baseline.size.height);
            [control setImage:row?VIMToolbarSymbolImage(symbol):baseline forSegment:i];
            [control setWidth:32 forSegment:i];
        }
        [control sizeToFit];control.frameOrigin=NSMakePoint(20,25+row*75);[view addSubview:control];
        NSButton *button=VIMToolbarButton([NSImage imageWithSystemSymbolName:@"plus" accessibilityDescription:nil],@"New",nil,NULL);
        button.frameOrigin=NSMakePoint(340,25+row*75);[view addSubview:button];
    }
    NSBitmapImageRep *rep=[[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL pixelsWide:1280 pixelsHigh:360 bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSCalibratedRGBColorSpace bytesPerRow:0 bitsPerPixel:0];
    rep.size=view.bounds.size;
    NSGraphicsContext *context=[NSGraphicsContext graphicsContextWithBitmapImageRep:rep];
    [NSGraphicsContext saveGraphicsState];NSGraphicsContext.currentContext=context;
    [NSColor.windowBackgroundColor setFill];NSRectFill(view.bounds);
    [view displayRectIgnoringOpacity:view.bounds inContext:context];
    [NSGraphicsContext restoreGraphicsState];
    return [[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:@(argv[1]) atomically:YES]?0:1;
}}
