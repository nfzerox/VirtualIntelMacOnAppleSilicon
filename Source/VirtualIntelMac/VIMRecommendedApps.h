#pragma once
#import <Foundation/Foundation.h>

static inline NSURL *VIMVirtualBuddyReleaseURLForHost(NSOperatingSystemVersion version) {
    NSString *release=version.majorVersion<=12?@"tag/1.4.1":
        (version.majorVersion<=14?@"tag/2.1-326":@"latest");
    return [NSURL URLWithString:[@"https://github.com/insidegui/VirtualBuddy/releases/" stringByAppendingString:release]];
}
static inline NSURL *VIMVirtualBuddyReleaseURL(void) {
    return VIMVirtualBuddyReleaseURLForHost(NSProcessInfo.processInfo.operatingSystemVersion);
}
