// A cold VM identity guard, independent of a graphics driver's load state.
// QEMU's VMware SVGA PCI IDs come from our vendored device implementation.
// Installation authorization additionally requires the exact new volume UUID
// and machine token on the separate setup medium. Never use this on its own
// as authorization to replace system files or create an account.
#import <IOKit/IOKitLib.h>
static inline BOOL VIMGuestHasPCIDevice(unsigned short vendorID,unsigned short deviceID){
    CFMutableDictionaryRef match=IOServiceMatching("IOPCIDevice");
    if(!match)return NO;
    const unsigned char vendor[]={vendorID&255,vendorID>>8,0,0},device[]={deviceID&255,deviceID>>8,0,0};
    CFDataRef vendorData=CFDataCreate(NULL,vendor,sizeof(vendor));
    CFDataRef deviceData=CFDataCreate(NULL,device,sizeof(device));
    const void *keys[]={CFSTR("vendor-id"),CFSTR("device-id")};
    const void *values[]={vendorData,deviceData};
    CFDictionaryRef properties=CFDictionaryCreate(NULL,keys,values,2,
        &kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(match,CFSTR("IOPropertyMatch"),properties);
    CFRelease(properties);CFRelease(deviceData);CFRelease(vendorData);
    io_service_t service=IOServiceGetMatchingService(kIOMasterPortDefault,match);
    if(!service)return NO;IOObjectRelease(service);return YES;
}
static inline BOOL VIMOpenGLGuestMachine(void){return VIMGuestHasPCIDevice(0x15ad,0x0405);}
static inline BOOL VIMModernSetupGuestMachine(void){
    // QEMU's existing control transport, independent of VGA/PVG selection.
    // Exact machine/volume authorization is still required before mutation.
    return VIMGuestHasPCIDevice(0x1af4,0x1003)||VIMGuestHasPCIDevice(0x1af4,0x1043);
}
