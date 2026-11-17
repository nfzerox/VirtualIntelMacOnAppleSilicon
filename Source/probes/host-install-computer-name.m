#import <Foundation/Foundation.h>
#import <SystemConfiguration/SystemConfiguration.h>
#include "../compat/VIMGuestComputerName.h"

int main(int argc,const char *argv[]) { @autoreleasepool {
    if(argc!=2)return 64;
    NSString *path=@(argv[1]);
    // This private fixture path, never the host's system preferences.
    SCPreferencesRef session=SCPreferencesCreate(NULL,CFSTR("VirtualIntelMacNameFixture"),(__bridge CFStringRef)path);
    if(!session)return 1;
    BOOL valid=SCPreferencesSetComputerName(session,CFSTR("test_user’s Mac"),kCFStringEncodingUTF8)&&
        SCPreferencesSetLocalHostName(session,CFSTR("test-users-Mac"));
    // Compare the native pending dictionary. Committing system preferences
    // requires authorization even for an alternate path, and is unnecessary
    // for this schema test. Never ask for it or apply host configuration.
    NSDictionary *native=valid?@{@"System":(__bridge NSDictionary *)SCPreferencesGetValue(session,kSCPrefSystem)}:nil;
    CFRelease(session);
    if(!valid)return 2;
    NSDictionary *staged=VIMGuestComputerNamePreferences(nil,@"test_user");
    if(![native[@"System"] isEqual:staged[@"System"]]) {
        NSLog(@"Native: %@; staged: %@",native,staged);return 3;
    }
    NSDictionary *original=@{@"CurrentSet":@"/Sets/Existing",@"System":@{
        @"System":@{@"ComputerName":@"Keep this name"},
        @"Network":@{@"HostNames":@{@"LocalHostName":@"keep-this-name"},@"Other":@YES}}};
    NSDictionary *preserved=VIMGuestComputerNamePreferences(original,@"other");
    if(![preserved isEqual:original])return 4;
    puts("PASS: offline guest names match native SystemConfiguration and preserve existing settings");
    return 0;
} }
