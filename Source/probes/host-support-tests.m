#import <Foundation/Foundation.h>
#import "VIMHostSupport.h"
int main(void) {
    @autoreleasepool {
        NSCAssert(!VIMHostUpdateNotice((NSOperatingSystemVersion){26,6,2}),@"Tahoe supported");
        NSCAssert(!VIMHostUpdateNotice((NSOperatingSystemVersion){27,0,1}),@"27 supported");
        NSCAssert([VIMHostUpdateNotice((NSOperatingSystemVersion){28,0,0}) isEqual:
            @"Virtual Intel Mac requires an update for macOS 28."],@"28 gate");
        NSCAssert([VIMHostUpdateNotice((NSOperatingSystemVersion){28,1,2}) isEqual:
            @"Virtual Intel Mac requires an update for macOS 28.1.2."],@"full version");
        NSCAssert(VIMHostUpdateNotice((NSOperatingSystemVersion){29,0,0}),@"future gate");
        puts("PASS: host 26/27 support and 28+ update notices");
    }
}
