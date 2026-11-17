#pragma once
#import <Foundation/Foundation.h>

// These installer apps contain the same InstallESD as the catalog's outer
// wrapper. High Sierra–Catalina keep Recovery beside InstallESD in SharedSupport,
// rather than inside the ESD; the worker retains that original split layout.
NS_INLINE NSString *VIMHostInstallESDAppFamily(NSString *guest) {
    return @{@"lion":@"10.7",@"mountainlion":@"10.8",@"mavericks":@"10.9",
        @"yosemite":@"10.10",@"elcapitan":@"10.11",@"sierra":@"10.12",
        @"highsierra":@"10.13",@"mojave":@"10.14",@"catalina":@"10.15"}[guest?:@""];
}
NS_INLINE BOOL VIMHostInstallUsesESDApp(NSString *guest,NSString *path) {
    return VIMHostInstallESDAppFamily(guest)!=nil&&[path.pathExtension.lowercaseString isEqual:@"app"];
}
NS_INLINE NSString *VIMHostInstallDVDFamily(NSString *guest) {
    return @{@"tiger":@"10.4",@"leopard":@"10.5",@"snowleopard":@"10.6"}[guest?:@""];
}
NS_INLINE BOOL VIMHostInstallUsesDVD(NSString *guest,NSString *path) {
    return VIMHostInstallDVDFamily(guest)!=nil&&
        [@[@"dmg",@"iso",@"cdr",@"img"] containsObject:path.pathExtension.lowercaseString?:@""];
}
NS_INLINE BOOL VIMHostInstallVersionMatchesFamily(NSString *family,NSDictionary *version) {
    NSString *product=version[@"ProductVersion"],*build=version[@"ProductBuildVersion"];
    if(!family||![product isKindOfClass:NSString.class]||![build isKindOfClass:NSString.class]||!build.length)return NO;
    if(![product isEqual:family]&&![product hasPrefix:[family stringByAppendingString:@"."]])return NO;
    for(NSString *part in [product componentsSeparatedByString:@"."])
        if(!part.length||[part rangeOfCharacterFromSet:[NSCharacterSet characterSetWithCharactersInString:@"0123456789"].invertedSet].location!=NSNotFound)return NO;
    // Build names become private backup-directory names, never paths.
    return [build rangeOfCharacterFromSet:[NSCharacterSet characterSetWithCharactersInString:
        @"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"].invertedSet].location==NSNotFound;
}
NS_INLINE BOOL VIMHostInstallESDVersionMatches(NSString *guest,NSDictionary *version) {
    return VIMHostInstallVersionMatchesFamily(VIMHostInstallESDAppFamily(guest),version);
}
NS_INLINE BOOL VIMHostInstallDVDVersionMatches(NSString *guest,NSDictionary *version) {
    return VIMHostInstallVersionMatchesFamily(VIMHostInstallDVDFamily(guest),version);
}

// The initial Leopard 8254X2.0.1a2 driver tests a different PHY link bit from
// QEMU's Marvell82545 model. Use the original media's working RTL8139 driver
// when both known drivers are actually present. This is an installed hardware
// profile, not a different guest payload or a later donor driver. Persisting
// it keeps the same NIC/MAC across subsequent native OS updates.
NS_INLINE NSString *VIMHostInstallNetworkAdapter(NSString *guest,NSDictionary *intel,NSDictionary *realtek) {
    if(![guest isEqual:@"leopard"]||
       ![intel[@"CFBundleIdentifier"] isEqual:@"com.apple.driver.AppleIntel8254XEthernet"]||
       ![intel[@"CFBundleVersion"] isEqual:@"2.0.1a2"]||
       ![realtek[@"CFBundleIdentifier"] isEqual:@"com.apple.driver.AppleRTL8139Ethernet"])return nil;
    NSDictionary *personalities=realtek[@"IOKitPersonalities"];
    if(![personalities isKindOfClass:NSDictionary.class])return nil;
    for(id personality in personalities.allValues) {
        if(![personality isKindOfClass:NSDictionary.class])continue;
        id match=personality[@"IOPCIMatch"];
        if([match isKindOfClass:NSString.class]&&
           [[match componentsSeparatedByCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet]
               containsObject:@"0x813910ec"])return @"rtl8139";
    }
    return nil;
}
