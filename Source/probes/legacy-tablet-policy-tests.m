#import <Foundation/Foundation.h>
#import "../VirtualIntelMac/VIMLegacyTabletPolicy.h"
#include <assert.h>

int main(void) {
    @autoreleasepool {
        for(NSString *guest in @[@"leopard",@"snowleopard",@"lion",@"mountainlion",@"mavericks"]) {
            assert(VIMInitialTabletTrim(guest,NO)==0);
            assert(VIMInitialTabletTrim(guest,YES)==15);
        }
        assert(VIMInitialTabletTrim(@"tiger",NO)==15);
        assert(VIMInitialTabletTrim(@"tiger",YES)==15);
        assert(VIMInitialTabletTrim(@"yosemite",NO)==0);
        assert(VIMInitialTabletTrim(@"yosemite",YES)==0);
        NSString *keyboard=@"+-o AppleHIDKeyboardEventDriver <class AppleHIDKeyboardEventDriver, id 1>\n"
            @"      \"Product\" = \"QEMU USB Keyboard\"\n"
            @"      \"AbsoluteAxisBoundsRemovalPercentage\" = 5\n";
        NSString *tablet=@"+-o IOHIDEventDriver <class IOHIDEventDriver, id 2>\n"
            @"      \"Product\" = \"QEMU USB Tablet\"\n";
        NSString *noTrim=[tablet stringByAppendingString:@"      \"AbsoluteAxisBoundsRemovalPercentage\" = 0\n"];
        assert([VIMObservedTabletTrim([keyboard stringByAppendingString:noTrim]) isEqual:@0]);
        assert([VIMObservedTabletTrim([tablet stringByAppendingString:@" \"AbsoluteAxisBoundsRemovalPercentage\" = 15\n"]) isEqual:@15]);
        assert(!VIMObservedTabletTrim(tablet));
        assert(!VIMObservedTabletTrim(keyboard));
        assert(!VIMObservedTabletTrim([noTrim stringByAppendingString:noTrim]));
        assert(!VIMObservedTabletTrim([tablet stringByAppendingString:@" \"AbsoluteAxisBoundsRemovalPercentage\" = 100\n"]));
    }
}
