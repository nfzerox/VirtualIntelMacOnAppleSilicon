/* Read-only native Darwin8 comparison: public IOKit vs its exact MIG seam. */
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFSerialize.h>
#include <CoreFoundation/CoreFoundation.h>
#include <mach/mach.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "../PassthroughOpenGL/iokit-bridge-compat.h"
#include "../vendor/source/VMsvga2/vminclude/vim_gl_surface_service.h"
extern kern_return_t io_service_get_matching_services(mach_port_t,const io_string_t,io_iterator_t *);
extern kern_return_t io_iterator_next(io_iterator_t,io_object_t *);
extern kern_return_t io_registry_entry_get_property_bytes(io_registry_entry_t,const io_name_t,
    io_struct_inband_t,mach_msg_type_number_t *);
int main(void)
{
    mach_port_t master=0; kern_return_t error=IOMasterPort(MACH_PORT_NULL,&master);
    printf("master error=%x port=%x\n",error,master);if(error||!master)return 1;
    mach_port_t helper=0;error=VIMTigerCopyMasterPort(&helper);
    printf("helper master error=%x port=%x native=%p\n",error,helper,(void *)IOMasterPort);
    if(helper)mach_port_deallocate(mach_task_self(),helper);
    io_service_t discovered=VIMIOServiceForClass("VMsvga2Accel");
    printf("helper service=%x\n",discovered);if(discovered)IOObjectRelease(discovered);
    CFMutableDictionaryRef match=IOServiceMatching("VMsvga2Accel");
    CFDataRef data=match?IOCFSerialize(match,0):NULL;
    if(!data||CFDataGetLength(data)>=(CFIndex)sizeof(io_string_t))return 1;
    io_string_t xml={0};memcpy(xml,CFDataGetBytePtr(data),(size_t)CFDataGetLength(data));
    printf("native XML=%s\n",xml);
    const char owned[]="<dict><key>IOProviderClass</key><string>VMsvga2Accel</string></dict>";
    for(unsigned i=0;i<2;i++){
        io_iterator_t iterator=0;io_service_t service=0;
        if(i){memset(xml,0,sizeof(xml));memcpy(xml,owned,sizeof(owned));}
        error=io_service_get_matching_services(master,xml,&iterator);
        kern_return_t next=iterator?io_iterator_next(iterator,&service):KERN_FAILURE;
        printf("match owned=%u error=%x iterator=%x next=%x service=%x\n",i,error,iterator,next,service);
        if(service)IOObjectRelease(service);if(iterator)IOObjectRelease(iterator);
    }
    io_service_t resources=IOServiceGetMatchingService(master,IOServiceMatching("VMsvga2Accel"));
    if(resources){
        io_struct_inband_t bytes={0};mach_msg_type_number_t size=sizeof(bytes);
        error=io_registry_entry_get_property_bytes(resources,VIM_GL_BOOT_NAMESPACE_PROPERTY,bytes,&size);
        printf("namespace MIG error=%x bytes=%u\n",error,size);
        CFTypeRef value=IORegistryEntryCreateCFProperty(resources,CFSTR(VIM_GL_BOOT_NAMESPACE_PROPERTY),NULL,0);
        printf("namespace CF present=%d data=%d length=%ld\n",value!=NULL,
            value&&CFGetTypeID(value)==CFDataGetTypeID(),
            value&&CFGetTypeID(value)==CFDataGetTypeID()?(long)CFDataGetLength(value):-1L);
        unsigned char namespace_bytes[16]={0};
        printf("namespace helper valid=%d\n",VIMIOCopyDataProperty(resources,VIM_GL_BOOT_NAMESPACE_PROPERTY,namespace_bytes,sizeof(namespace_bytes)));
        if(value)CFRelease(value);IOObjectRelease(resources);
    }
    CFRelease(data);CFRelease(match);mach_port_deallocate(mach_task_self(),master);return 0;
}
