/* Darwin8's Rosetta GL namespace intentionally exports a small native API.
 * Optional Intel-image fixup/diagnostic APIs must not be mandatory loader
 * imports. Native Intel still takes its original guarded implementation.
 * No framework donation, global injection or change to the GL wire ABI. */
#ifndef VIM_TIGER_ROSETTA_PLATFORM_H
#define VIM_TIGER_ROSETTA_PLATFORM_H
#include <stdlib.h>
#include <string.h>
#if defined(__i386__) && __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1040
#include <dlfcn.h>
#include <syslog.h>
#include <sys/sysctl.h>
#include <sys/mman.h>
#include <mach/mach.h>
#include <mach-o/dyld.h>
#include <mach-o/nlist.h>
#include <pthread.h>
extern __typeof__(strlen) strlen __attribute__((weak_import));
extern __typeof__(setenv) setenv __attribute__((weak_import));
extern __typeof__(syslog) syslog __attribute__((weak_import));
extern __typeof__(dladdr) dladdr __attribute__((weak_import));
extern __typeof__(mprotect) mprotect __attribute__((weak_import));
extern __typeof__(nlist) nlist __attribute__((weak_import));
extern __typeof__(vm_region) vm_region __attribute__((weak_import));
extern __typeof__(vm_read_overwrite) vm_read_overwrite __attribute__((weak_import));
extern __typeof__(vm_protect) vm_protect __attribute__((weak_import));
extern __typeof__(_dyld_get_image_vmaddr_slide) _dyld_get_image_vmaddr_slide __attribute__((weak_import));
static pthread_once_t vim_tiger_rosetta_once=PTHREAD_ONCE_INIT;
static int vim_tiger_rosetta_active;
static void VIMTigerDetectRosetta(void)
{
    int native=1;size_t size=sizeof(native);
    vim_tiger_rosetta_active=!sysctlbyname("sysctl.proc_native",&native,&size,NULL,0)&&
        size==sizeof(native)&&!native&&dlsym(RTLD_DEFAULT,"bdl_lookup_subject_symbol")!=NULL;
}
static int VIMTigerIsRosetta(void)
{pthread_once(&vim_tiger_rosetta_once,VIMTigerDetectRosetta);return vim_tiger_rosetta_active;}
static size_t VIMTigerStringLength(const char *text)
{
    if(strlen)return strlen(text);
    /* Volatile prevents a compiler from replacing this fallback with the
     * very libc entry that is unavailable in the measured Rosetta shim. */
    const volatile char *cursor=text;while(*cursor)++cursor;
    return (size_t)(cursor-text);
}
#define strlen VIMTigerStringLength
#endif
/* These four product features previously required setenv at GLI startup.
 * Tiger's native Rosetta libSystem has getenv but no setenv. Read the same
 * process-local override without allocating/mutating the environment. */
static inline int VIMGLProductFeature(const char *name)
{
    const char *value=getenv(name);
#if defined(__i386__) && __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1040
    return !value||strcmp(value,"0");
#else
    return value!=NULL;
#endif
}
#endif
