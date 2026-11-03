#import <Foundation/Foundation.h>
#import "../VirtualIntelMac/VIMHostAudio.h"
#import "../VirtualIntelMac/VIMWindowGeometry.h"
#include <assert.h>
#include <stdio.h>

int main(void) { @autoreleasepool {
    assert(NSEqualSizes(VIMRequestedGuestPixelSize(NSMakeSize(1149,719),1,YES,NO),NSMakeSize(1149,719)));
    assert(NSEqualSizes(VIMRequestedGuestPixelSize(NSMakeSize(1149,719),2,YES,NO),NSMakeSize(2298,1438)));
    assert(NSEqualSizes(VIMRequestedGuestPixelSize(NSMakeSize(1149,719),2,YES,YES),NSMakeSize(2298,1438)));
    assert(NSEqualSizes(VIMRequestedGuestPixelSize(NSMakeSize(641,401),1,YES,NO),NSMakeSize(800,600)));
    assert(NSEqualSizes(VIMRequestedGuestPixelSize(NSMakeSize(1149,719),1,NO,NO),NSMakeSize(1148,718)));
    assert(NSEqualSizes(VIMRequestedGuestPixelSize(NSMakeSize(4000,3000),1,YES,NO),NSMakeSize(2560,1600)));
    for(NSString *guest in @[@"tiger",@"leopard",@"snowleopard"]) {
        NSDictionary *base=@{@"Guest":guest};
        assert(!VIMGuestSupportsRetinaDisplay(base));
        assert([VIMDisplayMode(base) isEqual:@"scaled"]);
        assert([VIMDisplayMode(@{@"Guest":guest,@"DisplayMode":@"retina"}) isEqual:@"scaled"]);
        assert([VIMDisplayMode(@{@"Guest":guest,@"DisplayMode":@"space"}) isEqual:@"space"]);
        assert([VIMDisplayScalingFilter(base) isEqual:@"nearest"]);
        assert([VIMDisplayScalingFilter(@{@"Guest":guest,@"DisplayScalingFilter":@"linear"}) isEqual:@"linear"]);
    }
    for(NSString *guest in @[@"lion",@"mountainlion",@"mavericks",@"mojave",@"catalina",@"bigsur"]) {
        NSDictionary *base=@{@"Guest":guest};
        assert(VIMGuestSupportsRetinaDisplay(base));
        assert([VIMDisplayMode(base) isEqual:@"retina"]);
        assert([VIMDisplayScalingFilter(base) isEqual:@"linear"]);
    }
    puts("PASS: pre-HiDPI guests default to Scaled/Nearest; newer guest defaults are unchanged");
    return 0;
}}
