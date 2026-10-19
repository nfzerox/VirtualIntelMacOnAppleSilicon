// Bounded, root-only diagnosis of the normal IOKit storage-interface policy.
// No capture, unmount, USBInterfaceOpen, transfer, disk I/O or security writes.
#import <IOKit/IOCFPlugIn.h>
#import <IOKit/usb/IOUSBLib.h>
#include <bsm/libbsm.h>
#include <errno.h>
#include <stdlib.h>

static BOOL VIMUSBProbeNumber(const char *text,unsigned long limit,uint32_t *value) {
    if(!text||!text[0]||text[0]=='-')return NO;
    char *end=NULL;errno=0;unsigned long number=strtoul(text,&end,0);
    if(errno||!end||*end||!number||number>limit)return NO;
    *value=(uint32_t)number;return YES;
}
static int VIMUSBRunPolicyProbe(int argc,const char *argv[]) {
    uint32_t location,vendor,product;
    if(geteuid()!=0)return 77;
    if(argc!=5||!VIMUSBProbeNumber(argv[2],UINT32_MAX,&location)||
       !VIMUSBProbeNumber(argv[3],UINT16_MAX,&vendor)||!VIMUSBProbeNumber(argv[4],UINT16_MAX,&product))return 64;
    // Leave reasonable time to read the normal first-use privacy prompt.
    // Only this short-lived worker is bounded; no daemon-global alarm.
    alarm(120);
    auditinfo_addr_t audit={0};int auditResult=getaudit_addr(&audit,sizeof(audit));
    NSMutableArray *interfaces=[NSMutableArray new];
    io_iterator_t devices=0;BOOL found=NO;
    if(IOServiceGetMatchingServices(MACH_PORT_NULL,IOServiceMatching("IOUSBHostDevice"),&devices)==KERN_SUCCESS) {
        for(io_service_t device;(device=IOIteratorNext(devices));IOObjectRelease(device)) {
            if([Property(device,CFSTR("locationID")) unsignedIntValue]!=location||
               [Property(device,CFSTR("idVendor")) unsignedIntValue]!=vendor||
               [Property(device,CFSTR("idProduct")) unsignedIntValue]!=product)continue;
            if(VIMUSBIsInternal(device)||[Property(device,CFSTR("bDeviceClass")) integerValue]==9)continue;
            found=YES;io_iterator_t children=0;
            if(IORegistryEntryCreateIterator(device,kIOServicePlane,kIORegistryIterateRecursively,&children)!=KERN_SUCCESS)continue;
            for(io_registry_entry_t child;(child=IOIteratorNext(children));IOObjectRelease(child)) {
                if(!IOObjectConformsTo(child,"IOUSBHostInterface")||[Property(child,CFSTR("bInterfaceClass")) unsignedIntValue]!=8)continue;
                IOCFPlugInInterface **plugin=NULL;SInt32 score=0;
                IOReturn result=IOCreatePlugInInterfaceForService(child,kIOUSBInterfaceUserClientTypeID,
                    kIOCFPlugInInterfaceID,&plugin,&score);
                [interfaces addObject:@{@"Interface":Property(child,CFSTR("bInterfaceNumber"))?:@(-1),
                    @"IOReturn":@((uint32_t)result),@"PluginCreated":@(result==kIOReturnSuccess&&plugin!=NULL)}];
                if(plugin)(*plugin)->Release(plugin);
            }IOObjectRelease(children);
        }IOObjectRelease(devices);
    }
    NSDictionary *result=@{@"Found":@(found),@"UID":@(getuid()),@"EffectiveUID":@(geteuid()),
        @"AuditAvailable":@(auditResult==0),@"AuditUID":@(audit.ai_auid),@"AuditSession":@(audit.ai_asid),
        @"LocationID":@(location),@"StorageInterfaces":interfaces,@"CaptureAttempted":@NO};
    NSData *data=[NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingPrettyPrinted error:NULL];
    if(data){[[NSFileHandle fileHandleWithStandardOutput] writeData:data];
        [[NSFileHandle fileHandleWithStandardOutput] writeData:[@"\n" dataUsingEncoding:NSUTF8StringEncoding]];}
    alarm(0);return found&&interfaces.count?0:69;
}
