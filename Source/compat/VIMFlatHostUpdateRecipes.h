#import <Foundation/Foundation.h>

// Inventoried Apple OS-update operations, not executable catalog content.
// URLs and catalog product locations remain remotely configurable.
NS_INLINE NSArray<NSDictionary *> *VIMFlatHostUpdateRecipe(NSString *guest) {
    if([guest isEqual:@"leopard"])return @[
        @{@"Role":@"Combo",@"Product":@"zzz041-88057",@"Name":@"MacOSXUpdCombo10.5.8",
          @"PackageID":@"com.apple.pkg.update.os.10.5.8.combo"},
        @{@"Role":@"Security",@"Product":@"zzz041-87998",@"Name":@"SecUpd2011-004",
          @"PackageID":@"com.apple.pkg.update.security.2011.004.leopard"}];
    if([guest isEqual:@"lion"])return @[
        @{@"Role":@"Security",@"Product":@"zzz041-84761",@"Name":@"SecUpd2014-004Lion",
          @"PackageID":@"com.apple.pkg.update.security.10.7.5.11G1120.2014.004"}];
    if([guest isEqual:@"mountainlion"])return @[
        @{@"Role":@"Security",@"Product":@"zzz041-88224",@"Name":@"SecUpd2015-006MtLion",
          @"PackageID":@"com.apple.pkg.update.security.10.8.5.12F2560.2015.006"}];
    if([guest isEqual:@"mavericks"])return @[
        @{@"Role":@"Security",@"Product":@"zzz041-88446",@"Name":@"SecUpd2016-004Mavericks",
          @"PackageID":@"com.apple.pkg.update.security.2016-004Mavericks.13F1911"}];
    if([guest isEqual:@"yosemite"])return @[
        @{@"Role":@"Security",@"Product":@"041-89045",@"Name":@"SecUpd2017-003Yosemite",
          @"PackageID":@"com.apple.pkg.update.security.2017-003Yosemite.14F2511"}];
    if([guest isEqual:@"elcapitan"])return @[
        @{@"Role":@"Security",@"Product":@"041-90802",@"Name":@"SecUpd2018-004ElCapitan",
          @"PackageID":@"com.apple.pkg.update.os.SecUpd2018-004ElCapitan.15G22010"},
        @{@"Role":@"FullBundle",@"Product":@"041-90802",@"Name":@"FullBundleUpdate",
          @"PackageID":@"com.apple.update.fullbundleupdate.15G22010"}];
    if([guest isEqual:@"sierra"])return @[
        @{@"Role":@"Security",@"Product":@"061-41803",@"Name":@"SecUpd2019-005Sierra",
          @"PackageID":@"com.apple.pkg.update.os.SecUpd2019-005Sierra.16G2136"},
        @{@"Role":@"FullBundle",@"Product":@"061-41803",@"Name":@"FullBundleUpdate",
          @"PackageID":@"com.apple.update.fullbundleupdate.16G2136"}];
    if([guest isEqual:@"highsierra"])return @[
        @{@"Role":@"HopSecurity",@"Product":@"001-48382",@"Name":@"SecUpd2020-005HighSierra",
          @"PackageID":@"com.apple.pkg.update.os.SecUpd2020-005HighSierra.17G14033"},
        @{@"Role":@"HopFullBundle",@"Product":@"001-48382",@"Name":@"FullBundleUpdate",
          @"PackageID":@"com.apple.update.fullbundleupdate.17G14033"},
        @{@"Role":@"Security",@"Product":@"001-72525",@"Name":@"SecUpd2020-006HighSierra",
          @"PackageID":@"com.apple.pkg.update.os.SecUpd2020-006HighSierra.17G14042"},
        @{@"Role":@"FullBundle",@"Product":@"001-72525",@"Name":@"FullBundleUpdate",
          @"PackageID":@"com.apple.update.fullbundleupdate.17G14042"},
        @{@"Role":@"Boot",@"Product":@"001-72525",@"Name":@"SecureBoot",
          @"PackageID":@"com.apple.pkg.SecureBoot"},
        @{@"Role":@"UpdateEngine",@"Product":@"001-72525",@"Name":@"macOSBrain",
          @"PackageID":@"com.apple.pkg.macOSBrain"},
        @{@"Role":@"Recovery",@"Product":@"001-72525",@"Name":@"SecUpd2020-006HighSierra.RecoveryHDUpdate",
          @"PackageID":@"com.apple.pkg.SecUpd2020-006HighSierra.RecoveryHDUpdate.17G14042",@"Kind":@"RecoveryImage"}];
    if([guest isEqual:@"mojave"])return @[
        @{@"Role":@"Security",@"Product":@"071-44390",@"Name":@"SecUpd2021-005Mojave",
          @"PackageID":@"com.apple.pkg.update.os.SecUpd2021-005Mojave.18G9323"},
        @{@"Role":@"FullBundle",@"Product":@"071-44390",@"Name":@"FullBundleUpdate",
          @"PackageID":@"com.apple.update.fullbundleupdate.18G9323"},
        @{@"Role":@"Boot",@"Product":@"071-44390",@"Name":@"SecureBoot",@"PackageID":@"com.apple.pkg.SecureBoot"},
        @{@"Role":@"UpdateEngine",@"Product":@"071-44390",@"Name":@"macOSBrain",@"PackageID":@"com.apple.pkg.macOSBrain"},
        @{@"Role":@"Recovery",@"Product":@"071-44390",@"Name":@"SecUpd2021-005Mojave.RecoveryHDUpdate",
          @"PackageID":@"com.apple.pkg.SecUpd2021-005Mojave.RecoveryHDUpdate.18G9323",@"Kind":@"RecoveryImage"}];
    if([guest isEqual:@"catalina"])return @[
        @{@"Role":@"Security",@"Product":@"047-49516",@"Name":@"SecUpd2026-001Catalina",
          @"PackageID":@"com.apple.pkg.update.os.SecUpd2026-001Catalina.19H2036"},
        @{@"Role":@"FullBundle",@"Product":@"047-49516",@"Name":@"FullBundleUpdate",
          @"PackageID":@"com.apple.update.fullbundleupdate.19H2036"},
        @{@"Role":@"Boot",@"Product":@"047-49516",@"Name":@"SecureBoot",@"PackageID":@"com.apple.pkg.SecureBoot"},
        @{@"Role":@"UpdateEngine",@"Product":@"047-49516",@"Name":@"macOSBrain",@"PackageID":@"com.apple.pkg.macOSBrain"},
        @{@"Role":@"Recovery",@"Product":@"047-49516",@"Name":@"SecUpd2026-001Catalina.RecoveryHDUpdate",
          @"PackageID":@"com.apple.pkg.SecUpd2026-001Catalina.RecoveryHDUpdate.19H2036",@"Kind":@"RecoveryImage"}];
    if(![guest isEqual:@"snowleopard"])return nil;
    NSMutableArray *steps=[NSMutableArray new];
    for(unsigned part=0;part<=12;part++)[steps addObject:@{
        @"Role":[NSString stringWithFormat:@"ComboPart%u",part],@"Product":@"zzz041-98179",
        @"Name":[NSString stringWithFormat:@"MacOSXUpdCombo10.6.8.Part%u",part],
        @"PackageID":[NSString stringWithFormat:@"com.apple.pkg.update.os.10.6.8.combo.part%u",part]}];
    [steps addObjectsFromArray:@[
        @{@"Role":@"ComboBase",@"Product":@"zzz041-98179",@"Name":@"SUBaseSystemCombo10.6.8",
          @"PackageID":@"com.apple.pkg.update.subasesystem.10.6.8.combo"},
        @{@"Role":@"ComboX11",@"Product":@"zzz041-98179",@"Name":@"X11ForCombo10.6.8",
          @"PackageID":@"com.apple.pkg.update.X11.10.6.8.combo"},
        @{@"Role":@"ComboRosetta",@"Product":@"zzz041-98179",@"Name":@"RosettaForCombo10.6.8",
          @"PackageID":@"com.apple.pkg.update.rosetta.10.6.8.combo"},
        @{@"Role":@"Security",@"Product":@"zzz041-91751",@"Name":@"SecUpd2013-004",
          @"PackageID":@"com.apple.pkg.update.security.10.6.8.10K1136.2013.004"}]];
    // The base recipe installs X11 and Rosetta, but not QuickTime Player 7.
    // Apple's distribution conditionally selects those component updates.
    // Its unreferenced rollup .pkg is not an additional installation step.
    return steps;
}
