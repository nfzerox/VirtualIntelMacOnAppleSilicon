#import <Foundation/Foundation.h>
#import "VIMHostSupport.h"
#import <IOKit/pwr_mgt/IOPMLib.h>
#include <unistd.h>

static BOOL HasActivityAssertion(NSString *reason,CFStringRef type) {
    CFDictionaryRef raw=NULL;
    NSCAssert(IOPMCopyAssertionsByProcess(&raw)==kIOReturnSuccess,@"Can inspect the process power assertions");
    NSDictionary *processes=CFBridgingRelease(raw);
    for(NSDictionary *record in processes[@(getpid())])
        if([record[(__bridge NSString *)kIOPMAssertionNameKey] isEqual:reason]&&
           [record[(__bridge NSString *)kIOPMAssertionTypeKey] isEqual:(__bridge NSString *)type])return YES;
    return NO;
}
int main(void) {
    @autoreleasepool {
        NSString *reason=@"Virtual Intel Mac installation activity probe";
        @autoreleasepool {
            __attribute__((objc_precise_lifetime)) VIMInstallationActivity *activity=
                [VIMInstallationActivity beginWithReason:reason];
            NSCAssert(activity&&HasActivityAssertion(reason,kIOPMAssertionTypePreventUserIdleSystemSleep),@"Installation prevents host idle system sleep");
            NSCAssert(!HasActivityAssertion(reason,kIOPMAssertionTypePreventUserIdleDisplaySleep),@"Display idle sleep remains allowed");
        }
        NSCAssert(!HasActivityAssertion(reason,kIOPMAssertionTypePreventUserIdleSystemSleep),@"Releasing installation activity restores idle sleep");
        puts("PASS: process-scoped installation activity acquires/releases host idle-sleep assertion without blocking display sleep");
        for(NSString *guest in @[@"tiger",@"leopard",@"snowleopard",@"lion",@"mountainlion",@"mavericks",
                                  @"yosemite",@"elcapitan",@"sierra",@"highsierra"]) {
            NSCAssert(!VIMGuestRequiresPVGHostSupport(guest),@"Older guests do not need the Intel PVG helper");
            NSCAssert(!VIMHostUpdateNoticeForGuest((NSOperatingSystemVersion){28,0,0},guest),@"Older guests remain usable on 28");
        }
        for(NSString *guest in @[@"mojave",@"catalina",@"bigsur",@"monterey",@"ventura",@"sonoma",@"sequoia",@"tahoe"]) {
            NSCAssert(VIMGuestRequiresPVGHostSupport(guest),@"PVG guest");
            NSCAssert(!VIMHostUpdateNoticeForGuest((NSOperatingSystemVersion){26,6,2},guest),@"Tahoe supported");
            NSCAssert(!VIMHostUpdateNoticeForGuest((NSOperatingSystemVersion){27,0,1},guest),@"27 supported");
            NSCAssert(VIMHostUpdateNoticeForGuest((NSOperatingSystemVersion){29,0,0},guest),@"future gate");
        }
        NSCAssert([VIMHostUpdateNoticeForGuest((NSOperatingSystemVersion){28,0,0},@"mojave") isEqual:
            @"Virtual Intel Mac requires an update for macOS 28."],@"28 gate");
        NSCAssert([VIMHostUpdateNoticeForGuest((NSOperatingSystemVersion){28,1,2},@"bigsur") isEqual:
            @"Virtual Intel Mac requires an update for macOS 28.1.2."],@"full version");
        puts("PASS: host compatibility is scoped to PVG-backed guests");
        for(NSString *guest in @[@"mavericks",@"yosemite",@"highsierra",@"mojave",@"catalina"])
            for(NSNumber *selected in @[@NO,@YES])
                NSCAssert(VIMHostInstallerRequiresRosetta(guest,selected.boolValue),@"Period startup/APFS tools are required for either media source");
        for(NSString *guest in @[@"elcapitan",@"sierra"]) {
            NSCAssert(VIMHostInstallerRequiresRosetta(guest,NO),@"Catalog updates rebuild the startup cache");
            NSCAssert(!VIMHostInstallerRequiresRosetta(guest,YES),@"Original selected media can use its supplied startup cache");
        }
        for(NSString *guest in @[@"tiger",@"leopard",@"snowleopard",@"lion",@"mountainlion",@"bigsur",@"monterey",@"ventura",@"sonoma",@"sequoia",@"tahoe"])
            for(NSNumber *selected in @[@NO,@YES])
                NSCAssert(!VIMHostInstallerRequiresRosetta(guest,selected.boolValue),@"Native install tools do not require translation on this route");
        puts("PASS: native-install translation prerequisites cover selected media and catalog/resumed routes");
    }
}
