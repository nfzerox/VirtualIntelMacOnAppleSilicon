// Lion's native CGDisplayCopyAllDisplayModes filters out 2x modes unless
// kCGDisplayResolution is present. Newer releases export the duplicate-mode
// option instead. Use the native enumeration contract, not invented modes.
#include <CoreFoundation/CoreFoundation.h>
#include <ApplicationServices/ApplicationServices.h>
#include <dlfcn.h>
#include <sys/sysctl.h>
#include <string.h>

static inline int VIMLionDisplayModeAPI(void)
{
    char release[80]={0};size_t length=sizeof(release);
    return !sysctlbyname("kern.osrelease",release,&length,NULL,0)&&
        length<sizeof(release)&&!strncmp(release,"11.",3);
}

static inline CFDictionaryRef VIMCopyDisplayModeOptions(void)
{
    const CFStringRef *exported=dlsym(RTLD_DEFAULT,"kCGDisplayShowDuplicateLowResolutionModes");
    CFStringRef key=exported?*exported:NULL;
    if (!key) {
        if (!VIMLionDisplayModeAPI()) return NULL;
        key=CFSTR("kCGDisplayResolution");
    }
    const void *keys[]={key},*values[]={kCFBooleanTrue};
    return CFDictionaryCreate(NULL,keys,values,1,
        &kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
}

static inline CGError VIMSetDisplayMode(CGDirectDisplayID display,CGDisplayModeRef mode)
{
    // 11G63's configuration API re-enumerates with NULL options and rejects
    // every HiDPI mode (1000), ignoring its options parameter. Its other
    // public setter consumes the actual selected mode dictionary directly.
    // Newer guests keep the existing session-scoped configuration path.
    if (VIMLionDisplayModeAPI()) return CGDisplaySetDisplayMode(display,mode,NULL);
    CGDisplayConfigRef configuration=NULL;
    CGError error=CGBeginDisplayConfiguration(&configuration);
    if (error==kCGErrorSuccess) {
        error=CGConfigureDisplayWithDisplayMode(configuration,display,mode,NULL);
        if (error==kCGErrorSuccess) error=CGCompleteDisplayConfiguration(configuration,kCGConfigureForSession);
        else CGCancelDisplayConfiguration(configuration);
    }
    return error;
}
