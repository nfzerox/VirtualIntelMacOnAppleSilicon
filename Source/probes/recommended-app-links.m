#import "../VirtualIntelMac/VIMRecommendedApps.h"
int main(void) {@autoreleasepool {
    for(NSNumber *major in @[@11,@12,@13,@14,@15,@26,@27]) {
        NSOperatingSystemVersion version={major.integerValue,9,1};
        NSString *expected=major.integerValue<=12?@"tag/1.4.1":(major.integerValue<=14?@"tag/2.1-326":@"latest");
        NSString *URL=[@"https://github.com/insidegui/VirtualBuddy/releases/" stringByAppendingString:expected];
        if(![VIMVirtualBuddyReleaseURLForHost(version).absoluteString isEqual:URL])return 1;
    }
    puts("VirtualBuddy host-compatible release links: 11/12, 13/14, 15/26/27 passed");
    return 0;
}}
