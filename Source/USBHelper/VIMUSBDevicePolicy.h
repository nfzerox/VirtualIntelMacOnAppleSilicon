#import <Foundation/Foundation.h>
#import <IOKit/IOKitLib.h>

/// Device/port properties and the T2's internal virtual USB controller, not a
/// name/VID denylist. Never infer that a physical controller's external ports
/// are internal merely because the controller itself is built into the Mac.
static inline BOOL VIMUSBIsInternal(io_registry_entry_t device) {
    io_registry_entry_t current=device;IOObjectRetain(current);BOOL internal=NO;
    for(unsigned depth=0;current&&depth<64;depth++) {
        if(IOObjectConformsTo(current,"AppleUSBVHCIBCE"))internal=YES;
        if(IOObjectConformsTo(current,"IOUSBHostDevice")||IOObjectConformsTo(current,"AppleUSBHostPort")) {
            for(NSString *name in @[@"Built-In",@"built-in",@"non-removable",@"UsbConnector"]) {
                id value=CFBridgingRelease(IORegistryEntryCreateCFProperty(current,(__bridge CFStringRef)name,NULL,0));
                if([name isEqual:@"UsbConnector"]) {if([value isKindOfClass:NSNumber.class]&&[value integerValue]==255)internal=YES;}
                else if(([value isKindOfClass:NSNumber.class]&&[value boolValue])||
                        ([value isKindOfClass:NSString.class]&&[value caseInsensitiveCompare:@"yes"]==NSOrderedSame))internal=YES;
            }
        }
        io_registry_entry_t parent=0;
        if(!internal)IORegistryEntryGetParentEntry(current,kIOServicePlane,&parent);
        IOObjectRelease(current);current=parent;if(internal)break;
    }
    if(current)IOObjectRelease(current);return internal;
}
