// Readiness parsing only: never invokes a publisher or modifies host settings.
#import <Foundation/Foundation.h>
#import <SystemConfiguration/SystemConfiguration.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

static id fixture;
static SCDynamicStoreRef FixtureCreate(CFAllocatorRef allocator, CFStringRef name,
    SCDynamicStoreCallBack callback, SCDynamicStoreContext *context) {
    (void)allocator;(void)name;(void)callback;(void)context;
    return (SCDynamicStoreRef)CFRetain(CFSTR("fixture"));
}
static CFPropertyListRef FixtureCopy(SCDynamicStoreRef store, CFStringRef key) {
    (void)store;
    NSCAssert(CFEqual(key,CFSTR("com.apple.smb")),@"Read Apple's actual publication key");
    return fixture?CFRetain((__bridge CFTypeRef)fixture):NULL;
}
static void Check(BOOL valid,NSString *message) {NSCAssert(valid,@"%@",message);}
#define SCDynamicStoreCreate FixtureCreate
#define SCDynamicStoreCopyValue FixtureCopy
#include "../tools/guest-initial-preferences.h"
#undef SCDynamicStoreCreate
#undef SCDynamicStoreCopyValue

int main(void) {@autoreleasepool {
    NSArray *missing=@[NSNull.null,@{},@{@"Workgroup":@"WORKGROUP"},
        @{@"NetBIOSName":@1},@{@"NetBIOSName":@""}];
    NSCAssert(!HostInstalledSMBDomainReady(),@"Missing store value isn't ready");
    for(id value in missing) {
        fixture=value;
        NSCAssert(!HostInstalledSMBDomainReady(),@"Incomplete publication isn't ready: %@",value);
    }
    fixture=@{@"NetBIOSName":@"VIRTUAL-MAC"};
    NSCAssert(HostInstalledSMBDomainReady(),@"Native published domain is ready");
    puts("PASS: absent/partial/invalid publication and native NetBIOSName");
    return 0;
}}
