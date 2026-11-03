/* Rosetta1's native i386 IOKit shims export MIG entry points but omit some
 * convenience wrappers used by GLI. Reproduce measured period IOKit contracts,
 * not weaker object validation or a different mapping ABI. */
#ifndef VIM_IOKIT_BRIDGE_COMPAT_H
#define VIM_IOKIT_BRIDGE_COMPAT_H
#include <IOKit/IOKitLib.h>
#include <string.h>
#include "tiger-rosetta-platform.h"
#if defined(__i386__) && __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1040
#include "tiger-iokit-compat.h"
#include "tiger-rosetta-iokit.h"
#endif
#if defined(__i386__) && (__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1040 || __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1050)
#include <mach/mach.h>
#ifdef __cplusplus
extern "C" {
#endif
extern kern_return_t io_service_get_matching_services(mach_port_t,const io_string_t,io_iterator_t *);
extern kern_return_t io_iterator_next(io_iterator_t,io_object_t *);
extern kern_return_t io_registry_entry_get_property_bytes(io_registry_entry_t,
    const io_name_t,io_struct_inband_t,mach_msg_type_number_t *);
#ifdef __cplusplus
}
#endif
/* Native9J3050 IOMasterPort obtains the I/O master through host_get_io_master;
 * GetMatchingService serializes IOProviderClass, takes the first iterator
 * result and releases the iterator. Tiger/Leopard Rosetta shims export those MIG
 * operations but not the CF matching wrappers or kIOMasterPortDefault data.
 * These internal graphics-class lookups need only a short ASCII class name,
 * not arbitrary dictionary serialization or an out-of-line match request. */
static inline io_service_t VIMIOServiceForClass(const char *name)
{
    static const char prefix[]="<dict><key>IOProviderClass</key><string>";
    static const char suffix[]="</string></dict>";
    char matching[sizeof(io_string_t)];size_t length=0;
    if(!name)return 0;
    while(name[length]){
        unsigned char ch=(unsigned char)name[length];
        if(!((ch>='A'&&ch<='Z')||(ch>='a'&&ch<='z')||(ch>='0'&&ch<='9')||ch=='_')||++length>128)return 0;
    }
    if(!length)return 0;
    memcpy(matching,prefix,sizeof(prefix)-1);memcpy(matching+sizeof(prefix)-1,name,length);
    memcpy(matching+sizeof(prefix)-1+length,suffix,sizeof(suffix));
    mach_port_t master=0;io_iterator_t iterator=0;io_service_t service=0;
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1040
    kern_return_t error=VIMTigerCopyMasterPort(&master);
#else
    mach_port_t host=mach_host_self();
    kern_return_t error=host_get_io_master(host,&master);
    if(host)mach_port_deallocate(mach_task_self(),host);
#endif
    if(!error)error=io_service_get_matching_services(master,matching,&iterator);
    if(master)mach_port_deallocate(mach_task_self(),master);
    if(!error&&iterator){if(io_iterator_next(iterator,&service))service=0;}
    if(iterator)mach_port_deallocate(mach_task_self(),iterator);
    return service;
}
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1040
extern __typeof__(IOObjectRetain) IOObjectRetain __attribute__((weak_import));
static inline int VIMIOCanRetainObject(void){return IOObjectRetain!=NULL;}
#else
static inline int VIMIOCanRetainObject(void){return 1;}
static inline kern_return_t VIMIOObjectRetain(io_object_t object)
{return mach_port_mod_refs(mach_task_self(),object,MACH_PORT_RIGHT_SEND,1);}
#define IOObjectRetain VIMIOObjectRetain
#endif
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1050
static inline kern_return_t VIMIOConnectCallStructMethod(mach_port_t connection,uint32_t selector,
    const void *input,size_t input_size,void *output,size_t *output_size)
{return IOConnectCallMethod(connection,selector,NULL,0,input,input_size,NULL,NULL,output,output_size);}
static inline kern_return_t VIMIOConnectCallScalarMethod(mach_port_t connection,uint32_t selector,
    const uint64_t *input,uint32_t input_count,uint64_t *output,uint32_t *output_count)
{return IOConnectCallMethod(connection,selector,input,input_count,NULL,0,output,output_count,NULL,NULL);}
#define IOConnectCallStructMethod VIMIOConnectCallStructMethod
#define IOConnectCallScalarMethod VIMIOConnectCallScalarMethod
#endif
#else
static inline int VIMIOCanRetainObject(void){return 1;}
static inline io_service_t VIMIOServiceForClass(const char *name)
{return IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching(name));}
#endif
/* Only fixed-size data properties authored by our own driver: boot namespace
 * and framebuffer layout. Darwin9's native GetProperty is a tail jump to
 * this bytes MIG routine, whose actual kernel OSData branch copies the data
 * length/bytes (not an XML property list). Rosetta's CF shim has no CFData
 * accessors. The caller still validates its exact length and typed schema. */
static inline int VIMIOCopyDataProperty(io_registry_entry_t service,const char *name,void *bytes,size_t size)
{
#if defined(__i386__) && (__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1040 || __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1050)
    io_name_t property={0};size_t length=strlen(name);
    if(length>=sizeof(property)||size>sizeof(io_struct_inband_t))return 0;
    memcpy(property,name,length);mach_msg_type_number_t count=(mach_msg_type_number_t)size;
    return !io_registry_entry_get_property_bytes(service,property,(char *)bytes,&count)&&count==size;
#else
    CFStringRef property=CFStringCreateWithCString(NULL,name,kCFStringEncodingASCII);
    CFTypeRef value=property?IORegistryEntryCreateCFProperty(service,property,kCFAllocatorDefault,0):NULL;
    int valid=value&&CFGetTypeID(value)==CFDataGetTypeID()&&CFDataGetLength((CFDataRef)value)==(CFIndex)size;
    if(valid)memcpy(bytes,CFDataGetBytePtr((CFDataRef)value),size);
    if(value)CFRelease(value);if(property)CFRelease(property);return valid;
#endif
}
#if defined(__i386__)
extern kern_return_t io_object_conforms_to(io_object_t,const io_name_t,boolean_t *);
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1050
extern kern_return_t io_connect_map_memory_into_task(io_connect_t,uint32_t,
    task_t,mach_vm_address_t *,mach_vm_size_t *,uint32_t);
extern kern_return_t io_connect_unmap_memory_from_task(io_connect_t,uint32_t,
    task_t,mach_vm_address_t);
#endif
static boolean_t VIMIOObjectConformsTo(io_object_t object,const io_name_t name)
{
    boolean_t result=FALSE;
    return io_object_conforms_to(object,name,&result)==KERN_SUCCESS ? result : FALSE;
}
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1040
#define VIMIOConnectMapMemory64 VIMTigerMapMemory
#define VIMIOConnectUnmapMemory64 VIMTigerUnmapMemory
#else
#define VIMIOConnectMapMemory64 io_connect_map_memory_into_task
#define VIMIOConnectUnmapMemory64 io_connect_unmap_memory_from_task
#endif
#else
#define VIMIOObjectConformsTo IOObjectConformsTo
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
// In Leopard's actual SDK the LP64 spelling is IOConnectMapMemory, with
// mach_vm_address_t/mach_vm_size_t. Only the i386 export has the 64 suffix.
#define VIMIOConnectMapMemory64 IOConnectMapMemory
#define VIMIOConnectUnmapMemory64 IOConnectUnmapMemory
#else
#define VIMIOConnectMapMemory64 IOConnectMapMemory64
#define VIMIOConnectUnmapMemory64 IOConnectUnmapMemory64
#endif
#endif
#endif
