#pragma once
#import <Foundation/Foundation.h>

/// Every guest the app can create, oldest first. One source of truth for the
/// creation grid, the installer catalog and per-family policy below.
NS_INLINE NSArray<NSString *> *VIMSupportedGuests(void) {
    return @[@"tiger",@"leopard",@"snowleopard",@"lion",@"mountainlion",@"mavericks",@"yosemite",
             @"elcapitan",@"sierra",@"highsierra",@"mojave",@"catalina",@"bigsur",
             @"monterey",@"ventura",@"sonoma",@"sequoia",@"tahoe"];
}

/// Mojave and later: Recovery/startosinstall installation, native account
/// setup and a Metal desktop (rather than the 10.4–10.13 OpenGL backport).
NS_INLINE BOOL VIMIsModernGuest(NSString *guest) {
    return [@[@"mojave",@"catalina",@"bigsur",@"monterey",@"ventura",@"sonoma",@"sequoia",@"tahoe"] containsObject:guest?:@""];
}

/// Big Sur and later ship as one signed InstallAssistant package, resolved
/// live from Apple's software update catalog.
NS_INLINE BOOL VIMIsAssistantGuest(NSString *guest) {
    return [@[@"bigsur",@"monterey",@"ventura",@"sonoma",@"sequoia",@"tahoe"] containsObject:guest?:@""];
}

/// Monterey and later: added in 2.1.1, following the Big Sur recipe.
NS_INLINE BOOL VIMIsPostBigSurGuest(NSString *guest) {
    return [@[@"monterey",@"ventura",@"sonoma",@"sequoia",@"tahoe"] containsObject:guest?:@""];
}

// Keep newer guest bring-up available without advertising it in the release
// wizard. This is a creation policy, not a restriction on existing VM boots.
NS_INLINE NSArray<NSString *> *VIMCreationGuestChoices(BOOL experimental) {
    if(experimental)return VIMSupportedGuests();
    NSMutableArray<NSString *> *guests=[NSMutableArray array];
    for(NSString *guest in VIMSupportedGuests())
        if(!VIMIsPostBigSurGuest(guest))[guests addObject:guest];
    return guests;
}

// Budget populated System/Data bytes, their sparse QCOW2 export and working
// headroom, not the virtual disk's capacity. Downloads are counted separately.
// Includes room for the original System extraction before compression/sealing.
NS_INLINE uint64_t VIMAssistantHostScratchGiB(NSString *guest) {
    NSNumber *budget=@{@"bigsur":@64,@"monterey":@72,@"ventura":@72,
        @"sonoma":@80,@"sequoia":@88,@"tahoe":@96}[guest?:@""];
    return budget?budget.unsignedLongLongValue:96;
}

/// macOS 13 and later: no Intel 8254x Ethernet driver; VirtIO networking.
NS_INLINE BOOL VIMUsesVirtIONetwork(NSString *guest) {
    return VIMIsPostBigSurGuest(guest)&&![guest isEqual:@"monterey"];
}

/// Preserve the installed hardware profile across OS updates. Original
/// Leopard media can select its stock Realtek driver instead of the early
/// 8254X driver's incompatible PHY-status interpretation. Existing Macs keep
/// their current adapter unless the profile explicitly selects this device.
NS_INLINE NSString *VIMNetworkDeviceModel(NSString *guest,NSDictionary *configuration) {
    if([guest isEqual:@"leopard"]&&[configuration[@"NetworkAdapter"] isEqual:@"rtl8139"])return @"rtl8139";
    return VIMUsesVirtIONetwork(guest)?@"virtio-net-pci":@"e1000-82545em";
}

/// Ventura and later ship only the Haswell-subtype Intel Cryptex cache.
/// Penryn selects x86_64 instead of x86_64h, leaving even launchd without
/// libSystem. Present a real emulated Haswell feature set, not just a label
/// or a forced dyld subtype. Older guests retain their qualified CPU model.
/// QEMU's noTSX alias defaults to stepping 1, which XNU rejects as pre-C0.
NS_INLINE NSString *VIMEmulatedCPUModel(NSString *guest,BOOL appleSilicon) {
    BOOL cryptex=[@[@"ventura",@"sonoma",@"sequoia",@"tahoe"] containsObject:guest?:@""];
    NSString *model=cryptex?
        @"Haswell-noTSX,stepping=4,vendor=GenuineIntel,+pcid,-tsc-deadline,-invpcid":
        @"Penryn,vendor=GenuineIntel,+sse4.2,+popcnt,+aes,+pclmulqdq,+pcid";
    // ARM TCG has an explicit virtual timestamp rate. Preserve older guests'
    // clock-discovery path only there; Intel HVF must retain its existing
    // frequency leaves and hardware counter.
    if(appleSilicon)model=[model stringByAppendingString:cryptex?
        @",tsc-frequency=1200000000":@",tsc-frequency=1200000000,vmware-cpuid-freq=off"];
    return model;
}

/// Widest guest display, in pixels, that the guest's WindowServer survives on
/// Apple's host framework (0: no limit). x86-64 Sonoma and later SkyLight tiles
/// wide windows 1024 pixels at a time and its PVG plugin refuses every bottom-row
/// tile after the first, so 3+ tiles across crashes WindowServer (measured on
/// 14.8.9 and 15.8.2; reims-vgpu's x86 guest report measured it on 26.6). The
/// prepared Metal plugin's helper avoids it (PVGTiledBackingCompat.h); a
/// machine lifts this once verified (-[VIMMachine guestDisplayMaxWidth]).
NS_INLINE CGFloat VIMGuestDisplayMaxWidth(NSString *guest) {
    return [@[@"sonoma",@"sequoia",@"tahoe"] containsObject:guest?:@""]?2048:0;
}

/// The macOS major version an assistant guest installs (11–15, then 26).
NS_INLINE NSString *VIMMajorVersionForGuest(NSString *guest) {
    return @{@"bigsur":@"11",@"monterey":@"12",@"ventura":@"13",@"sonoma":@"14",@"sequoia":@"15",@"tahoe":@"26"}[guest?:@""];
}

/// macOS 26 and later install only onto APFS ("This volume is not formatted as
/// APFS"); earlier releases convert the qualified HFS+ target themselves.
NS_INLINE BOOL VIMRequiresAPFSTarget(NSString *guest) {
    NSInteger major=VIMMajorVersionForGuest(guest).integerValue;
    return major>=26;
}

/// Release name without the "macOS" prefix, e.g. "Monterey".
NS_INLINE NSString *VIMReleaseNameForGuest(NSString *guest) {
    return @{@"tiger":@"Tiger",@"leopard":@"Leopard",@"snowleopard":@"Snow Leopard",@"lion":@"Lion",
             @"mountainlion":@"Mountain Lion",@"mavericks":@"Mavericks",@"yosemite":@"Yosemite",
             @"elcapitan":@"El Capitan",@"sierra":@"Sierra",@"highsierra":@"High Sierra",@"mojave":@"Mojave",
             @"catalina":@"Catalina",@"bigsur":@"Big Sur",@"monterey":@"Monterey",@"ventura":@"Ventura",
             @"sonoma":@"Sonoma",@"sequoia":@"Sequoia",@"tahoe":@"Tahoe"}[guest?:@""];
}

/// Full marketing title, e.g. "macOS Monterey 12".
NS_INLINE NSString *VIMSystemTitleForGuest(NSString *guest) {
    return @{@"tiger":@"Mac OS X Tiger 10.4",@"leopard":@"Mac OS X Leopard 10.5",@"snowleopard":@"Mac OS X Snow Leopard 10.6",
             @"lion":@"Mac OS X Lion 10.7",@"mountainlion":@"OS X Mountain Lion 10.8",@"mavericks":@"OS X Mavericks 10.9",
             @"yosemite":@"OS X Yosemite 10.10",@"elcapitan":@"OS X El Capitan 10.11",@"sierra":@"macOS Sierra 10.12",
             @"highsierra":@"macOS High Sierra 10.13",@"mojave":@"macOS Mojave 10.14",@"catalina":@"macOS Catalina 10.15",
             @"bigsur":@"macOS Big Sur 11",@"monterey":@"macOS Monterey 12",@"ventura":@"macOS Ventura 13",
             @"sonoma":@"macOS Sonoma 14",@"sequoia":@"macOS Sequoia 15",@"tahoe":@"macOS Tahoe 26"}[guest?:@""];
}

/// Apple's installer application bundle identifier for an assistant guest.
NS_INLINE NSString *VIMInstallAssistantBundleIdentifier(NSString *guest) {
    return @{@"bigsur":@"com.apple.InstallAssistant.macOSBigSur",@"monterey":@"com.apple.InstallAssistant.macOSMonterey",
             @"ventura":@"com.apple.InstallAssistant.macOSVentura",@"sonoma":@"com.apple.InstallAssistant.macOSSonoma",
             @"sequoia":@"com.apple.InstallAssistant.macOSSequoia",@"tahoe":@"com.apple.InstallAssistant.macOSTahoe"}[guest?:@""];
}

/// Apple's installer application name, e.g. "Install macOS Monterey.app".
NS_INLINE NSString *VIMInstallAssistantAppName(NSString *guest) {
    NSString *release=VIMReleaseNameForGuest(guest);
    return release&&(VIMIsAssistantGuest(guest)||[@[@"highsierra",@"mojave",@"catalina"] containsObject:guest])?
        [NSString stringWithFormat:@"Install macOS %@.app",release]:nil;
}
