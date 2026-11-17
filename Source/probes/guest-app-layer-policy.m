#import <Foundation/Foundation.h>
#include <assert.h>
static NSMutableDictionary *store;
static unsigned writes,saves;
static BOOL failSave;
static NSArray *Scope(CFStringRef domain,CFStringRef owner,CFStringRef host) {
    return @[(__bridge NSString *)domain,(__bridge NSString *)owner,(__bridge NSString *)host];
}
static CFPropertyListRef Copy(CFStringRef key,CFStringRef domain,CFStringRef owner,CFStringRef host) {
    return CFBridgingRetain(store[Scope(domain,owner,host)][(__bridge NSString *)key]);
}
static void Set(CFStringRef key,CFPropertyListRef value,CFStringRef domain,CFStringRef owner,CFStringRef host) {
    assert(!value&&CFEqual(key,CFSTR("NSViewAllowsRootLayerBacking")));writes++;
    [store[Scope(domain,owner,host)] removeObjectForKey:(__bridge NSString *)key];
}
static Boolean Sync(CFStringRef domain,CFStringRef owner,CFStringRef host) {
    (void)domain;(void)owner;(void)host;saves++;return !failSave;
}
#define CFPreferencesCopyValue Copy
#define CFPreferencesSetValue Set
#define CFPreferencesSynchronize Sync
#include "../tools/guest-app-layer-policy.h"
#undef CFPreferencesCopyValue
#undef CFPreferencesSetValue
#undef CFPreferencesSynchronize
int main(void) {@autoreleasepool {
    store=[NSMutableDictionary new];
    NSArray *scopes=@[Scope(CFSTR("com.apple.Aperture"),kCFPreferencesAnyUser,kCFPreferencesAnyHost),
        Scope(CFSTR("com.apple.iPhoto"),CFSTR("mac"),kCFPreferencesAnyHost),
        Scope(CFSTR("com.apple.iWork.Pages"),CFSTR("mac"),kCFPreferencesCurrentHost),
        Scope(CFSTR("com.apple.iWork.Keynote"),CFSTR("second"),kCFPreferencesCurrentHost)];
    for(NSArray *scope in scopes)store[scope]=[@{@"NSViewAllowsRootLayerBacking":@NO,@"Keep":@"unchanged"} mutableCopy];
    NSArray *other=Scope(CFSTR("org.example.other"),CFSTR("mac"),kCFPreferencesAnyHost);
    store[other]=[@{@"NSViewAllowsRootLayerBacking":@YES} mutableCopy];
    NSDictionary *result=RemoveMojaveAppLayerOverrides(@[@"mac",@"second"]);
    assert([result[@"RemovedOverrides"] intValue]==4&&![result[@"Failures"] intValue]&&writes==4&&saves==4);
    for(NSArray *scope in scopes)assert([store[scope] isEqual:@{@"Keep":@"unchanged"}]);
    assert([store[other][@"NSViewAllowsRootLayerBacking"] isEqual:@YES]);
    result=RemoveMojaveAppLayerOverrides(@[@"mac",@"second"]);
    assert(![result[@"RemovedOverrides"] intValue]&&writes==4&&saves==4);
    store[scopes[0]][@"NSViewAllowsRootLayerBacking"]=@YES;failSave=YES;
    result=RemoveMojaveAppLayerOverrides(@[@"mac"]);
    assert([result[@"Failures"] intValue]==1&&![result[@"RestartRequired"] boolValue]);
    puts("PASS scoped system/user/ByHost removal, preservation, absent-key no-op and save failure; no host preferences touched");
}return 0;}
