#import "VIMGuestTransactionEvidence.h"
#include <assert.h>

int main(void) {@autoreleasepool {
    NSDictionary *baseline=@{@"PreviousBuild":@"18G103",@"VolumeUUID":@"update-volume"};
    NSMutableDictionary *report=[@{@"Guest":@"mojave",@"InstalledSystemCandidate":@YES,
        @"LiveRoot":@YES,@"KernelMatchesSystem":@NO,@"VolumeUUID":@"update-volume",
        @"KernelBuild":@"18G103",@"ProductBuildVersion":@"18G9323",
        @"BootSessionUUID":@"146BB7F6-F8CB-4D55-A877-BDB917A20945"} mutableCopy];
    NSMutableDictionary *saved=[@{@"PrivilegedAgentVerified":@YES,@"Report":report} mutableCopy];
    assert(VIMPendingInPlaceUpdate(saved,@"mojave",baseline));
    assert(!VIMPendingInPlaceUpdate(saved,@"catalina",baseline));
    assert(!VIMPendingInPlaceUpdate(saved,@"mojave",nil));
    saved[@"PrivilegedAgentVerified"]=@NO;
    assert(!VIMPendingInPlaceUpdate(saved,@"mojave",baseline));
    saved[@"PrivilegedAgentVerified"]=@YES;
    NSDictionary *other=@{@"VolumeUUID":@"other-volume",@"ProductBuildVersion":@"18G103",
        @"KernelBuild":@"18G9323",@"KernelMatchesSystem":@YES,@"BootSessionUUID":@"",
        @"InstalledSystemCandidate":@NO,@"LiveRoot":@NO};
    for(NSString *key in other) {
        id original=report[key];report[key]=other[key];
        assert(!VIMPendingInPlaceUpdate(saved,@"mojave",baseline));
        report[key]=original;
    }
    puts("Verified same-volume in-place update boot evidence.");
    return 0;
}}
