#pragma once
#include <CoreFoundation/CoreFoundation.h>

/* First-login preference writer, not a daemon or a read-only status probe.
 * Resolve CurrentHost inside the guest instead of guessing a ByHost filename
 * from the VM identifier. Call only for an explicitly pending new account. */
static Boolean VIMPrepareScreenSaverPreferences(CFStringRef domain,CFStringRef user) {
    int value=0;
    CFNumberRef zero=CFNumberCreate(NULL,kCFNumberIntType,&value);
    if(!zero)return false;
    CFStringRef keys[]={CFSTR("idleTime"),CFSTR("askForPassword"),CFSTR("askForPasswordDelay")};
    CFTypeRef values[]={zero,kCFBooleanFalse,zero};
    CFStringRef hosts[]={kCFPreferencesCurrentHost,kCFPreferencesAnyHost};
    unsigned h,k;
    for(h=0;h<2;h++)for(k=0;k<3;k++)
        CFPreferencesSetValue(keys[k],values[k],domain,user,hosts[h]);
    Boolean current=CFPreferencesSynchronize(domain,user,kCFPreferencesCurrentHost);
    Boolean any=CFPreferencesSynchronize(domain,user,kCFPreferencesAnyHost);
    Boolean saved=current&&any;
    for(h=0;h<2;h++)for(k=0;k<3;k++) {
        CFPropertyListRef actual=CFPreferencesCopyValue(keys[k],domain,user,hosts[h]);
        if(!actual||!CFEqual(actual,values[k]))saved=false;
        if(actual)CFRelease(actual);
    }
    CFRelease(zero);return saved;
}
