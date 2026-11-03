/* One cold resource namespace lookup per renderer image. This is a lifetime
 * identity, not authentication. Never use a machine UUID or wall clock. */
#ifndef VIM_OPENGL_GUEST_RESOURCE_NAMESPACE_H
#define VIM_OPENGL_GUEST_RESOURCE_NAMESPACE_H
#include <IOKit/IOKitLib.h>
#include "iokit-bridge-compat.h"
#include <pthread.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>
#include <uuid/uuid.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../vendor/source/VMsvga2/vminclude/vim_gl_surface_service.h"
static pthread_once_t vim_resource_namespace_once=PTHREAD_ONCE_INIT;
static uuid_t vim_resource_namespace;
static int vim_resource_namespace_valid;
static void VIMReadResourceNamespace(void)
{
#if !defined(__i386__) || __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ != 1040
    char boot[80]={0};size_t bytes=sizeof(boot);
    if(!sysctlbyname("kern.bootsessionuuid",boot,&bytes,NULL,0)&&
       bytes<=sizeof(boot)&&memchr(boot,0,sizeof(boot))&&
       !uuid_parse(boot,vim_resource_namespace)){
        vim_resource_namespace_valid=!uuid_is_null(vim_resource_namespace);return;
    }
    char release[80]={0};bytes=sizeof(release);
    if(sysctlbyname("kern.osrelease",release,&bytes,NULL,0)||bytes>=sizeof(release)){
        // Native8R4088 returns ENOENT for this by-name lookup even though
        // uname supplies its real8.10.3 release. The registry namespace is
        // present and readable. Use the native kernel-version API only for
        // deciding which old-kernel fallback is permitted; never invent the
        // resource identity or substitute a machine ID for its boot UUID.
        struct utsname kernel;
        if(uname(&kernel))return;
        const char *end=memchr(kernel.release,0,sizeof(kernel.release));
        if(!end||(size_t)(end-kernel.release)>=sizeof(release))return;
        memcpy(release,kernel.release,(size_t)(end-kernel.release)+1);
    }
    long major=strtol(release,NULL,10);if(major!=8&&major!=9&&major!=10&&major!=11&&major!=12)return;
#endif
    // These kernels have no kern.bootsessionuuid. Our kernel driver creates
    // a random namespace before publishing its service. Every GL process,
    // including WindowServer before login/guest-agent startup, reads the
    // same read-only registry property. Driver reload gives a new namespace.
    io_service_t service=VIMIOServiceForClass("VMsvga2Accel");
    if(!service)return;
    int valid=VIMIOCopyDataProperty(service,VIM_GL_BOOT_NAMESPACE_PROPERTY,vim_resource_namespace,sizeof(uuid_t));
    IOObjectRelease(service);
    if(valid){
        for(unsigned i=0;i<sizeof(vim_resource_namespace);++i)
            if(vim_resource_namespace[i]){vim_resource_namespace_valid=1;break;}
    }
}
static int VIMGetResourceNamespace(uuid_t value)
{
    pthread_once(&vim_resource_namespace_once,VIMReadResourceNamespace);
    if(!vim_resource_namespace_valid)return 0;
    memcpy(value,vim_resource_namespace,sizeof(uuid_t));return 1;
}
#endif
