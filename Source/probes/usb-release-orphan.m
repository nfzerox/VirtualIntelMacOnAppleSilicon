// Explicit recovery of one external USB device after a crashed capture worker.
// This is a developer diagnostic, not an automatic eject/reset policy.
// Refuses every device that currently publishes any disk, mounted or not.
#import <Foundation/Foundation.h>
#import <IOKit/IOCFPlugIn.h>
#import <IOKit/usb/IOUSBLib.h>
#import "../USBHelper/VIMUSBDevicePolicy.h"
#include <unistd.h>
#include <errno.h>

static BOOL Number(const char *text,unsigned long maximum,uint32_t *result) {
    if(!text||!*text||*text=='-')return NO;
    char *end=NULL;errno=0;unsigned long value=strtoul(text,&end,0);
    if(errno||!end||*end||!value||value>maximum)return NO;
    *result=(uint32_t)value;return YES;
}
static uint32_t Property(io_registry_entry_t service,CFStringRef key) {
    id value=CFBridgingRelease(IORegistryEntryCreateCFProperty(service,key,NULL,0));
    return [value isKindOfClass:NSNumber.class]?[value unsignedIntValue]:0;
}
int main(int argc,const char *argv[]) {@autoreleasepool {
    uint32_t location,vendor,product;
    if(geteuid()!=0)return 77;
    if(argc!=4||!Number(argv[1],UINT32_MAX,&location)||!Number(argv[2],UINT16_MAX,&vendor)||
       !Number(argv[3],UINT16_MAX,&product))return 64;
    alarm(20);io_iterator_t devices=0;
    if(IOServiceGetMatchingServices(MACH_PORT_NULL,IOServiceMatching("IOUSBHostDevice"),&devices))return 69;
    int status=69;
    for(io_service_t device;(device=IOIteratorNext(devices));IOObjectRelease(device)) {
        if(Property(device,CFSTR("locationID"))!=location||Property(device,CFSTR("idVendor"))!=vendor||
           Property(device,CFSTR("idProduct"))!=product)continue;
        if(VIMUSBIsInternal(device)||Property(device,CFSTR("bDeviceClass"))==9){status=77;IOObjectRelease(device);break;}
        io_iterator_t descendants=0;BOOL hasMedia=NO;
        if(IORegistryEntryCreateIterator(device,kIOServicePlane,kIORegistryIterateRecursively,&descendants)){
            status=77;IOObjectRelease(device);break;
        }
        for(io_registry_entry_t entry;(entry=IOIteratorNext(descendants));IOObjectRelease(entry))
            if(IOObjectConformsTo(entry,"IOMedia"))hasMedia=YES;
        IOObjectRelease(descendants);
        if(hasMedia){fprintf(stderr,"Refusing a device that already publishes disk media.\n");status=77;IOObjectRelease(device);break;}
        IOCFPlugInInterface **plugin=NULL;SInt32 score=0;IOUSBDeviceInterface500 **usb=NULL;
        IOReturn result=IOCreatePlugInInterfaceForService(device,kIOUSBDeviceUserClientTypeID,kIOCFPlugInInterfaceID,&plugin,&score);
        if(result==kIOReturnSuccess&&!plugin)result=kIOReturnNoResources;
        if(result==kIOReturnSuccess&&plugin) {
            HRESULT query=(*plugin)->QueryInterface(plugin,CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID500),(void **)&usb);
            if(query||!usb)result=kIOReturnUnsupported;
            else {
                // Normal exclusive open, never Seize: an active owner must win.
                result=(*usb)->USBDeviceOpen(usb);
                if(result==kIOReturnSuccess) {
                    result=(*usb)->USBDeviceReEnumerate(usb,0); // reattach drivers, NOT capture
                    (*usb)->USBDeviceClose(usb);
                }
            }
        }
        if(usb)(*usb)->Release(usb);
        if(plugin)(*plugin)->Release(plugin);
        printf("Selected device normal re-enumeration: 0x%08x\n",result);
        status=result==kIOReturnSuccess?0:1;IOObjectRelease(device);break;
    }
    IOObjectRelease(devices);return status;
}}
