// Finite diagnosis of Apple's own USB plug-in stages. No capture, interface
// claim, detach, unmount, endpoint transfer or disk I/O. Select one external
// device by location/vendor/product; never enumerate/open internal interfaces.
#import <Foundation/Foundation.h>
#import <IOKit/IOKitLib.h>
#import <IOKit/IOCFPlugIn.h>
#import <IOKit/usb/IOUSBLib.h>
#import "../USBHelper/VIMUSBPluginProbe.h"
#include <unistd.h>

static unsigned Value(io_registry_entry_t entry, CFStringRef key) {
    NSNumber *value=CFBridgingRelease(IORegistryEntryCreateCFProperty(entry,key,NULL,0));
    return value.unsignedIntValue;
}
int main(int argc,const char **argv) {@autoreleasepool {
    BOOL compatibility=argc==5&&!strcmp(argv[4],"--compatibility-iterator");
    if(argc!=4&&!compatibility)return 64;
    unsigned location=(unsigned)strtoul(argv[1],NULL,0), vendor=(unsigned)strtoul(argv[2],NULL,0), product=(unsigned)strtoul(argv[3],NULL,0);
    if(!location||!vendor||!product)return 64;
    NSURL *url=[NSURL fileURLWithPath:@"/System/Library/Extensions/IOUSBHostFamily.kext/Contents/PlugIns/IOUSBLib.bundle"];
    CFPlugInRef bundle=CFPlugInCreate(NULL,(__bridge CFURLRef)url);
    CFArrayRef factories=bundle?CFPlugInFindFactoriesForPlugInTypeInPlugIn(kIOUSBInterfaceUserClientTypeID,bundle):NULL;
    printf("uid=%u euid=%u bundle=%d factories=%ld\n",getuid(),geteuid(),bundle!=NULL,factories?CFArrayGetCount(factories):0L);
    io_iterator_t devices=0;IOServiceGetMatchingServices(MACH_PORT_NULL,
        IOServiceMatching(compatibility?"IOUSBDevice":"IOUSBHostDevice"),&devices);
    for(io_service_t device;(device=IOIteratorNext(devices));IOObjectRelease(device)) {
        if(Value(device,CFSTR("locationID"))!=location||Value(device,CFSTR("idVendor"))!=vendor||Value(device,CFSTR("idProduct"))!=product)continue;
        if(Value(device,CFSTR("Built-In")))continue;
        io_iterator_t interfaces=0;IOUSBDeviceInterface500 **deviceAPI=NULL;
        if(compatibility) {
            IOCFPlugInInterface **plugin=NULL;SInt32 score=0;
            IOReturn status=IOCreatePlugInInterfaceForService(device,kIOUSBDeviceUserClientTypeID,kIOCFPlugInInterfaceID,&plugin,&score);
            printf("device-plugin=0x%08x\n",status);
            if(plugin) {
                (*plugin)->QueryInterface(plugin,CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID500),(LPVOID *)&deviceAPI);
                (*plugin)->Release(plugin);
            }
            if(!deviceAPI)continue;
            IOUSBFindInterfaceRequest request={8,kIOUSBFindInterfaceDontCare,kIOUSBFindInterfaceDontCare,kIOUSBFindInterfaceDontCare};
            status=(*deviceAPI)->CreateInterfaceIterator(deviceAPI,&request,&interfaces);
            printf("interface-iterator=0x%08x\n",status);
        }else IORegistryEntryCreateIterator(device,kIOServicePlane,kIORegistryIterateRecursively,&interfaces);
        for(io_service_t service;(service=IOIteratorNext(interfaces));IOObjectRelease(service)) {
            if(!IOObjectConformsTo(service,compatibility?"IOUSBInterface":"IOUSBHostInterface")||Value(service,CFSTR("bInterfaceClass"))!=8)continue;
            printf("storage-interface=%u host=%d classic=%d\n",Value(service,CFSTR("bInterfaceNumber")),
                IOObjectConformsTo(service,"IOUSBHostInterface"),IOObjectConformsTo(service,"IOUSBInterface"));
            printf("compatibility-alias=%d\n",Value(service,CFSTR("AppleUSBAlternateServiceRegistryID"))!=0);
            for(CFIndex i=0;factories&&i<CFArrayGetCount(factories);++i) {
                CFUUIDRef factory=(CFUUIDRef)CFArrayGetValueAtIndex(factories,i);
                IUnknownVTbl **unknown=(IUnknownVTbl **)CFPlugInInstanceCreate(NULL,factory,kIOUSBInterfaceUserClientTypeID);
                printf("factory=%ld instance=%d\n",i,unknown!=NULL);
                if(!unknown)continue;
                IOCFPlugInInterface **plugin=NULL;
                HRESULT query=(*unknown)->QueryInterface(unknown,CFUUIDGetUUIDBytes(kIOCFPlugInInterfaceID),(LPVOID *)&plugin);
                (*unknown)->Release(unknown);
                printf("query=0x%08x interface=%d\n",(unsigned)query,plugin!=NULL);
                if(!plugin)continue;
                SInt32 score=0;IOReturn probed=(*plugin)->Probe(plugin,NULL,service,&score);
                printf("probe=0x%08x score=%d\n",probed,(int)score);
                if(probed==kIOReturnSuccess) {
                    IOReturn started=(*plugin)->Start(plugin,NULL,service);
                    printf("start=0x%08x\n",started);
                }
                (*plugin)->Release(plugin);
            }
            IOCFPlugInInterface **checked=NULL;SInt32 checkedScore=0;
            IOReturn checkedResult=VIMUSBProbeStoragePlugin(service,&checked,&checkedScore);
            printf("helper-preserved-status=0x%08x plugin=%d\n",checkedResult,checked!=NULL);
            if(checked)(*checked)->Release(checked);
        }
        if(interfaces)IOObjectRelease(interfaces);
        if(deviceAPI)(*deviceAPI)->Release(deviceAPI);
    }
    if(devices)IOObjectRelease(devices);
    if(factories)CFRelease(factories);
    if(bundle)CFRelease(bundle);
    return 0;
} }
