/* Read-only own-process survey of the Darwin8 resource identity lookup. */
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>
#include <sys/sysctl.h>
#include <uuid/uuid.h>
#include <stdio.h>
#include <errno.h>
#include "../PassthroughOpenGL/guest-resource-namespace.h"
int main(void)
{
    char release[80]={0};size_t size=sizeof(release);
    int error=sysctlbyname("kern.osrelease",release,&size,NULL,0);
    printf("release error=%d errno=%d bytes=%lu value=%s\n",error,errno,(unsigned long)size,release);
    io_service_t service=VIMIOServiceForClass("VMsvga2Accel");
    printf("service=%u\n",service);
    if(!service)return 1;
    CFStringRef name=CFStringCreateWithCString(NULL,VIM_GL_BOOT_NAMESPACE_PROPERTY,kCFStringEncodingASCII);
    CFTypeRef property=IORegistryEntryCreateCFProperty(service,name,kCFAllocatorDefault,0);
    printf("property=%d type=%lu data_type=%lu\n",property!=NULL,property?(unsigned long)CFGetTypeID(property):0,(unsigned long)CFDataGetTypeID());
    if(property&&CFGetTypeID(property)==CFDataGetTypeID())printf("data bytes=%ld\n",(long)CFDataGetLength(property));
    uuid_t value;int copied=VIMIOCopyDataProperty(service,VIM_GL_BOOT_NAMESPACE_PROPERTY,value,sizeof(value));
    printf("direct copy=%d null=%d\n",copied,copied?uuid_is_null(value):-1);
    int valid=VIMGetResourceNamespace(value);
    printf("namespace=%d\n",valid);
    if(property)CFRelease(property);CFRelease(name);IOObjectRelease(service);
    return valid?0:1;
}
