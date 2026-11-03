#pragma once
#import <Foundation/Foundation.h>

// Retain the existing primary optical/disk slot. Additional ISO/DMG images
// use independent USB Bulk-Only devices, created only when used. Three extra
// drives fit on either existing controller without a slow USB1 hub or a new
// controller. El Capitan introduced the native USBHost xHCI driver used here;
// keep older guests on the proven EHCI path.
static const NSUInteger VIMImageSlotCount=4;
static inline BOOL VIMImageUsesUSB3(NSString *guest) {
    return [@[@"elcapitan",@"sierra",@"highsierra",@"mojave",@"catalina",@"bigsur"] containsObject:guest];
}
static inline NSString *VIMImageUSBBus(NSString *guest) {
    return VIMImageUsesUSB3(guest)?@"xhci.0":@"usb.0";
}
static inline NSUInteger VIMImageUSBPort(NSString *guest,NSUInteger slot) {
    return (VIMImageUsesUSB3(guest)?1:3)+slot;
}
static inline NSString *VIMImageSlotPath(NSDictionary *configuration,NSUInteger slot) {
    if(!slot)return configuration[@"OpticalMedia"]?:@"";
    NSArray *more=configuration[@"AdditionalMedia"];
    return slot<=more.count?more[slot-1]:@"";
}
static inline NSDictionary *VIMImageSlotSettings(NSDictionary *configuration,NSUInteger slot,NSString *path) {
    if(!slot)return @{@"OpticalMedia":path};
    NSMutableArray *more=[configuration[@"AdditionalMedia"] mutableCopy]?:[NSMutableArray new];
    while(more.count<slot)[more addObject:@""];
    more[slot-1]=path;
    while(more.count&&![more.lastObject length])[more removeLastObject];
    return @{@"AdditionalMedia":more};
}
// Forget only the selections actually skipped/ejected. A newer queued choice
// (possibly from another configuration window) must not be overwritten.
static inline NSDictionary *VIMImageClearedSelections(NSDictionary *configuration,NSDictionary<NSNumber *,NSString *> *removed) {
    NSMutableDictionary *current=[configuration mutableCopy],*changes=[NSMutableDictionary new];
    for(NSNumber *index in removed) {
        NSUInteger slot=index.unsignedIntegerValue;
        if(slot>=VIMImageSlotCount||![VIMImageSlotPath(current,slot) isEqual:removed[index]])continue;
        NSDictionary *setting=VIMImageSlotSettings(current,slot,@"");
        [current addEntriesFromDictionary:setting];[changes addEntriesFromDictionary:setting];
    }
    return changes;
}
static inline NSString *VIMImageSlotDevice(NSUInteger slot,NSString *format) {
    BOOL disk=[format isEqual:@"dmg"];
    if(!slot)return disk?@"image-device":@"cdrom-device";
    return [NSString stringWithFormat:@"image-disk-%lu",(unsigned long)slot];
}
