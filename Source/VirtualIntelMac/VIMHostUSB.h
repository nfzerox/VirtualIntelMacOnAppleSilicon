#import <Foundation/Foundation.h>
@import IOKit;
@import IOKit.usb;
#import "../USBHelper/VIMUSBDevicePolicy.h"
#import <IOKit/usb/IOUSBHostFamilyDefinitions.h>

/// Release only whole disks below this exact external USB registry device.
/// Force is caller-confirmed and can discard outstanding host writes.
void VIMHostReleaseUSBDisks(NSDictionary *device,NSArray<NSURL *> *protectedURLs,BOOL force,void (^completion)(NSError *));
/// Current selected-device disks/mounts, re-resolved before every operation.
/// Protected volumes may not be unmounted, even with Force Unmount.
NSDictionary *VIMHostUSBStorageInfo(NSDictionary *device,NSArray<NSURL *> *protectedURLs,NSError **error);
/// Request scoped IOKit storage-interface consent in the authenticated user's
/// session. No raw disk/file access; works before a drive publishes IOMedia.
/// Completes on the main queue. Must precede unmount/capture, not menu hover.
void VIMHostRequestUSBStorageAccess(NSDictionary *device,NSArray<NSURL *> *protectedURLs,void (^completion)(NSError *));
/// Registry hot-plug notifications only; no polling, device open or capture.
void VIMStartHostUSBNotifications(void);

/// Host USB devices that can be offered to a running guest. Bus and Address
/// match libusb's Darwin numbering (locationID >> 24 and "USB Address"), which
/// QEMU's usb-host device uses. Hubs and the Mac's own built-in devices are
/// omitted: they cannot be captured, and capturing them would be harmful.
static inline NSArray<NSDictionary *> *VIMHostUSBDevices(void) {
    NSMutableArray *devices=[NSMutableArray array];
    io_iterator_t iterator=IO_OBJECT_NULL;
    // IOKit documents NULL as the default port; the named synonym is 12+.
    if(IOServiceGetMatchingServices(MACH_PORT_NULL,IOServiceMatching("IOUSBHostDevice"),&iterator)!=KERN_SUCCESS)return devices;
    for(io_service_t service;(service=IOIteratorNext(iterator));IOObjectRelease(service)) {
        CFMutableDictionaryRef raw=NULL;
        if(IORegistryEntryCreateCFProperties(service,&raw,kCFAllocatorDefault,0)!=KERN_SUCCESS||!raw)continue;
        NSDictionary *p=CFBridgingRelease(raw);
        NSNumber *vendor=p[@"idVendor"],*product=p[@"idProduct"],*location=p[@"locationID"],*address=p[@"USB Address"],*cls=p[@"bDeviceClass"];
        if(![vendor isKindOfClass:NSNumber.class]||![product isKindOfClass:NSNumber.class]||
           ![location isKindOfClass:NSNumber.class]||![address isKindOfClass:NSNumber.class])continue;
        if(cls.integerValue==9)continue; // Hub.
        if(VIMUSBIsInternal(service))continue;
        NSString *name=p[@"USB Product Name"]?:p[@"kUSBProductString"];
        NSString *maker=p[@"USB Vendor Name"]?:p[@"kUSBVendorString"];
        if(![name isKindOfClass:NSString.class]||!name.length)
            name=[NSString stringWithFormat:@"USB Device %04X:%04X",vendor.unsignedIntValue,product.unsignedIntValue];
        // Device class is often zero: mass storage is declared by an interface.
        // Read registry metadata only; enumeration must not capture/open it.
        BOOL storage=cls.integerValue==8;io_iterator_t interfaces=IO_OBJECT_NULL;
        if(IORegistryEntryCreateIterator(service,kIOServicePlane,kIORegistryIterateRecursively,&interfaces)==KERN_SUCCESS) {
            for(io_registry_entry_t child;(child=IOIteratorNext(interfaces));IOObjectRelease(child)) {
                if(!IOObjectConformsTo(child,"IOUSBHostInterface"))continue;
                NSNumber *interfaceClass=CFBridgingRelease(IORegistryEntryCreateCFProperty(child,CFSTR("bInterfaceClass"),NULL,0));
                if(interfaceClass.unsignedIntValue==8)storage=YES;
            }IOObjectRelease(interfaces);
        }
        uint32_t loc=location.unsignedIntValue;
        [devices addObject:@{@"Name":name,@"Vendor":[maker isKindOfClass:NSString.class]?maker:@"",
            @"VendorID":vendor,@"ProductID":product,@"LocationID":@(loc),
            @"Bus":@(loc>>24),@"Address":address,
            @"ConnectionSpeed":[p[@"USBSpeed"] isKindOfClass:NSNumber.class]?p[@"USBSpeed"]:@0,
            @"StorageInterface":@(storage),
            @"Identifier":[NSString stringWithFormat:@"usb%08x",loc]}];
    }
    IOObjectRelease(iterator);
    [devices sortUsingDescriptors:@[[NSSortDescriptor sortDescriptorWithKey:@"Name" ascending:YES selector:@selector(localizedCaseInsensitiveCompare:)]]];
    return devices;
}
