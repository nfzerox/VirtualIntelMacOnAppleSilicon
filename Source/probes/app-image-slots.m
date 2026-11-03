#import <Foundation/Foundation.h>
#import "../VirtualIntelMac/VIMImageSlots.h"
#include <assert.h>

int main(void) {@autoreleasepool {
    NSDictionary *original=@{@"OpticalMedia":@"/images/original.iso"};
    assert([VIMImageSlotPath(original,0) isEqual:@"/images/original.iso"]);
    assert(!VIMImageSlotPath(original,3).length);
    NSMutableDictionary *settings=[original mutableCopy];
    [settings addEntriesFromDictionary:VIMImageSlotSettings(settings,3,@"/images/last.dmg")];
    assert([VIMImageSlotPath(settings,3) isEqual:@"/images/last.dmg"]);
    assert(!VIMImageSlotPath(settings,2).length);
    [settings addEntriesFromDictionary:VIMImageSlotSettings(settings,2,@"/images/middle.iso")];
    [settings addEntriesFromDictionary:VIMImageSlotSettings(settings,3,@"")];
    assert([settings[@"AdditionalMedia"] count]==2);
    assert([VIMImageSlotPath(settings,2) isEqual:@"/images/middle.iso"]);
    assert([VIMImageSlotPath(settings,0) isEqual:@"/images/original.iso"]);
    assert(!original[@"AdditionalMedia"]);
    NSDictionary *stale=@{@"OpticalMedia":@"/missing/fcp.iso",@"AdditionalMedia":@[@"/keep/one.dmg",@"",@"/missing/last.iso"]};
    NSDictionary *cleared=VIMImageClearedSelections(stale,@{@0:@"/missing/fcp.iso",@3:@"/missing/last.iso"});
    NSMutableDictionary *after=[stale mutableCopy];[after addEntriesFromDictionary:cleared];
    assert(!VIMImageSlotPath(after,0).length&&!VIMImageSlotPath(after,3).length);
    assert([VIMImageSlotPath(after,1) isEqual:@"/keep/one.dmg"]);
    assert([after[@"AdditionalMedia"] count]==1);
    assert(!VIMImageClearedSelections(stale,@{@0:@"/old/previous.iso"}).count);
    assert([VIMImageSlotPath(stale,0) isEqual:@"/missing/fcp.iso"]);
    NSMutableSet *devices=[NSMutableSet new];
    for(NSUInteger slot=0;slot<VIMImageSlotCount;slot++)for(NSString *format in @[@"raw",@"dmg"])
        [devices addObject:VIMImageSlotDevice(slot,format)];
    assert(devices.count==5);
    for(NSString *guest in @[@"tiger",@"leopard",@"snowleopard",@"lion",@"mountainlion",@"mavericks",@"yosemite"]) {
        assert(!VIMImageUsesUSB3(guest));assert([VIMImageUSBBus(guest) isEqual:@"usb.0"]);
        assert(VIMImageUSBPort(guest,0)==3&&VIMImageUSBPort(guest,3)==6);
    }
    for(NSString *guest in @[@"elcapitan",@"sierra",@"highsierra",@"mojave",@"catalina",@"bigsur"]) {
        assert(VIMImageUsesUSB3(guest));assert([VIMImageUSBBus(guest) isEqual:@"xhci.0"]);
        assert(VIMImageUSBPort(guest,0)==1&&VIMImageUSBPort(guest,3)==4);
    }
    puts("PASS independent image slots, sparse settings, legacy primary and unique drive IDs");return 0;
}}
