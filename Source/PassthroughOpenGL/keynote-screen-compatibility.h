/* Keynote '09 calls an AppKit method removed in High Sierra. Keep this
 * process-local compatibility repair separate from GL dispatch. No category,
 * interposition, launch environment or change to the installed app is needed.
 */
#ifndef VIM_KEYNOTE_SCREEN_COMPATIBILITY_H
#define VIM_KEYNOTE_SCREEN_COMPATIBILITY_H
#include <CoreFoundation/CoreFoundation.h>
#include <objc/objc.h>
#include <dlfcn.h>
#include <pthread.h>

static int VIMIsKeynote09Bundle(CFStringRef identifier,CFStringRef version)
{
    return identifier && CFGetTypeID(identifier)==CFStringGetTypeID() &&
        CFEqual(identifier,CFSTR("com.apple.iWork.Keynote")) &&
        version && CFGetTypeID(version)==CFStringGetTypeID() &&
        CFStringHasPrefix(version,CFSTR("5."));
}
static void VIMKeynoteResetScreens(id receiver,SEL command)
{ (void)receiver;(void)command; }

static int VIMInstallKeynoteMissingScreenReset(CFStringRef identifier,CFStringRef version,double appkit)
{
    if(!VIMIsKeynote09Bundle(identifier,version) || appkit<1561.0)return 0;
    // Resolve dynamically so older guest builds gain no newer ObjC runtime
    // dependency. AppKit is already loaded by Keynote before it initializes GL.
    Class (*get_class)(const char *)=dlsym(RTLD_DEFAULT,"objc_getClass");
    Class (*get_meta)(const char *)=dlsym(RTLD_DEFAULT,"objc_getMetaClass");
    SEL (*selector)(const char *)=dlsym(RTLD_DEFAULT,"sel_registerName");
    void *(*get_method)(Class,SEL)=dlsym(RTLD_DEFAULT,"class_getClassMethod");
    BOOL (*add_method)(Class,SEL,IMP,const char *)=dlsym(RTLD_DEFAULT,"class_addMethod");
    if(!get_class || !get_meta || !selector || !get_method || !add_method)return 0;
    Class screen=get_class("NSScreen"),meta=get_meta("NSScreen");
    SEL reset=selector("_resetScreens");
    if(!screen || !meta || get_method(screen,reset))return 0;
    // The method outlives CGL contexts. Keep its implementation mapped even
    // if OpenGL/NSBundle releases its ordinary reference to this driver image.
    Dl_info own={0};
    if(!dladdr((const void *)&VIMKeynoteResetScreens,&own) || !own.dli_fname)return 0;
    void *keep=dlopen(own.dli_fname,RTLD_NOW|RTLD_LOCAL);
    if(!keep)return 0;
    if(!add_method(meta,reset,(IMP)VIMKeynoteResetScreens,"v@:")) {
        dlclose(keep);return 0;
    }
    return 1; // Deliberately retain the image for this process's lifetime.
}
static void VIMKeynoteScreenCompatibilityOnce(void)
{
    CFBundleRef main=CFBundleGetMainBundle();
    if(!main)return;
    CFStringRef identifier=CFBundleGetIdentifier(main);
    CFStringRef version=CFBundleGetValueForInfoDictionaryKey(main,CFSTR("CFBundleShortVersionString"));
    if(!VIMIsKeynote09Bundle(identifier,version))return;
    const double *appkit=dlsym(RTLD_DEFAULT,"NSAppKitVersionNumber");
    if(appkit)VIMInstallKeynoteMissingScreenReset(identifier,version,*appkit);
}
static void VIMApplyKeynoteScreenCompatibility(void)
{
    static pthread_once_t once=PTHREAD_ONCE_INIT;
    pthread_once(&once,VIMKeynoteScreenCompatibilityOnce);
}
#endif
