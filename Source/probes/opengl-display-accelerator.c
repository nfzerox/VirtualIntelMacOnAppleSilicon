/* Read-only native framebuffer-to-accelerator admission. No user-client,
 * surface, context, screen readback or display-mode mutation. */
#include <ApplicationServices/ApplicationServices.h>
#include <IOKit/graphics/IOGraphicsLib.h>
#include <stdint.h>
#include <stdio.h>
int main(void)
{
    io_service_t framebuffer=CGDisplayIOServicePort(CGMainDisplayID());
    io_service_t accelerator=0;UInt32 index=0;
    IOReturn status=framebuffer?IOAccelFindAccelerator(framebuffer,&accelerator,&index):kIOReturnNotFound;
    io_name_t name={0};if(accelerator)IORegistryEntryGetName(accelerator,name);
    printf("find=%x framebuffer=%u accelerator=%s index=%u pointer-bits=%u\n",
        status,framebuffer,name,(unsigned)index,(unsigned)(sizeof(void*)*8));
    if(accelerator){
        CFTypeRef caps=IORegistryEntryCreateCFProperty(accelerator,CFSTR("AccelCaps"),kCFAllocatorDefault,0);
        int value=0;if(caps&&CFGetTypeID(caps)==CFNumberGetTypeID())CFNumberGetValue((CFNumberRef)caps,kCFNumberIntType,&value);
        printf("accelerator-caps=%x\n",value);if(caps)CFRelease(caps);
        unsigned raw=0;uint32_t raw_size=sizeof(raw);
        IOReturn raw_status=IORegistryEntryGetProperty(accelerator,"AccelCaps",(char *)&raw,&raw_size);
        printf("native-raw-caps-status=%x size=%u value=%x\n",raw_status,(unsigned)raw_size,raw);
        uint64_t wide=0;raw_size=sizeof(wide);
        raw_status=IORegistryEntryGetProperty(accelerator,"AccelCaps",(char *)&wide,&raw_size);
        printf("native-wide-caps-status=%x size=%u value=%llx\n",raw_status,(unsigned)raw_size,(unsigned long long)wide);
        IOObjectRelease(accelerator);
    }
    return status?1:0;
}
