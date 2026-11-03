#pragma once
#import <Foundation/Foundation.h>
#include <sys/stat.h>

// One-time native WindowServer policy, never a resize loop or a renderer hook.
// Small virtual panels otherwise lose synthesized2x modes below the native
// panel-detection size threshold. Respect an existing explicit user policy.
static BOOL VIMPrepareNativeHiDPIPolicy(BOOL *changed) {
    *changed=NO;
    CFStringRef domain=CFSTR("com.apple.windowserver");
    CFTypeRef enabled=CFPreferencesCopyValue(CFSTR("DisplayResolutionEnabled"),domain,kCFPreferencesAnyUser,kCFPreferencesCurrentHost);
    CFTypeRef disabled=CFPreferencesCopyValue(CFSTR("DisplayResolutionDisabled"),domain,kCFPreferencesAnyUser,kCFPreferencesCurrentHost);
    BOOL valid=(!enabled||CFGetTypeID(enabled)==CFBooleanGetTypeID())&&
        (!disabled||CFGetTypeID(disabled)==CFBooleanGetTypeID());
    BOOL explicitPolicy=enabled||(valid&&disabled&&CFBooleanGetValue((CFBooleanRef)disabled));
    if(enabled)CFRelease(enabled);if(disabled)CFRelease(disabled);
    if(!valid)return NO;if(explicitPolicy)return YES;
    mode_t previous=umask(0022);
    CFPreferencesSetValue(CFSTR("DisplayResolutionEnabled"),kCFBooleanTrue,domain,kCFPreferencesAnyUser,kCFPreferencesCurrentHost);
    BOOL saved=CFPreferencesSynchronize(domain,kCFPreferencesAnyUser,kCFPreferencesCurrentHost);umask(previous);
    enabled=CFPreferencesCopyValue(CFSTR("DisplayResolutionEnabled"),domain,kCFPreferencesAnyUser,kCFPreferencesCurrentHost);
    BOOL verified=saved&&enabled&&CFGetTypeID(enabled)==CFBooleanGetTypeID()&&CFBooleanGetValue((CFBooleanRef)enabled);
    if(enabled)CFRelease(enabled);*changed=verified;return verified;
}
