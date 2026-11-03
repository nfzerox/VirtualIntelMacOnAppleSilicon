#pragma once
// Native display-mode ABI boundary. Leopard exposes retained dictionaries,
// not Snow Leopard's opaque CGDisplayModeRef or HiDPI enumeration APIs.
#include <ApplicationServices/ApplicationServices.h>
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
typedef CFDictionaryRef VIMGuestDisplayMode;
static inline CFArrayRef VIMGuestDisplayModes(CGDirectDisplayID display){
    CFArrayRef modes=CGDisplayAvailableModes(display);return modes?CFRetain(modes):NULL;
}
static inline size_t VIMGuestModeDimension(VIMGuestDisplayMode mode,CFStringRef key){
    CFTypeRef value=mode?CFDictionaryGetValue(mode,key):NULL;int dimension=0;
    return value&&CFGetTypeID(value)==CFNumberGetTypeID()&&
        CFNumberGetValue(value,kCFNumberIntType,&dimension)&&dimension>0?(size_t)dimension:0;
}
static inline int VIMGuestModeMatches(VIMGuestDisplayMode mode,size_t width,size_t height,size_t scale){
    return scale==1&&VIMGuestModeDimension(mode,kCGDisplayWidth)==width&&
        VIMGuestModeDimension(mode,kCGDisplayHeight)==height;
}
static inline VIMGuestDisplayMode VIMGuestCurrentMode(CGDirectDisplayID display){
    CFDictionaryRef mode=CGDisplayCurrentMode(display);return mode?CFRetain(mode):NULL;
}
static inline CGError VIMGuestSetMode(CGDirectDisplayID display,VIMGuestDisplayMode mode){
    return CGDisplaySwitchToMode(display,mode);
}
#else
#include "guest-display-options.h"
typedef CGDisplayModeRef VIMGuestDisplayMode;
static inline CFArrayRef VIMGuestDisplayModes(CGDirectDisplayID display){
    CFDictionaryRef options=VIMCopyDisplayModeOptions();
    CFArrayRef modes=CGDisplayCopyAllDisplayModes(display,options);
    if(options)CFRelease(options);return modes;
}
static inline int VIMGuestModeMatches(VIMGuestDisplayMode mode,size_t width,size_t height,size_t scale){
    return mode&&CGDisplayModeGetPixelWidth(mode)==width&&CGDisplayModeGetPixelHeight(mode)==height&&
        CGDisplayModeGetWidth(mode)==width/scale&&CGDisplayModeGetHeight(mode)==height/scale;
}
static inline VIMGuestDisplayMode VIMGuestCurrentMode(CGDirectDisplayID display){return CGDisplayCopyDisplayMode(display);}
static inline CGError VIMGuestSetMode(CGDirectDisplayID display,VIMGuestDisplayMode mode){return VIMSetDisplayMode(display,mode);}
#endif
