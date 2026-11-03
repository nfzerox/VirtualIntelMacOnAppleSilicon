#pragma once
#import <Foundation/Foundation.h>

static inline BOOL VIMUsesLegacyTabletCalibration(NSString *guest) {
    return [@[@"tiger",@"leopard",@"snowleopard",@"lion",@"mountainlion",@"mavericks"] containsObject:guest];
}

static inline NSUInteger VIMInitialTabletTrim(NSString *guest, BOOL installer) {
    // The installed 10.5–10.9 guests bind the tablet with a zero override.
    // Tiger was measured trimming on the installed desktop. Legacy
    // installer/recovery boots retain the separate native driver fallback.
    return [guest isEqual:@"tiger"]||(installer&&VIMUsesLegacyTabletCalibration(guest))?15:0;
}

// Period ioreg has no plist output. Only consume the override on the bound
// QEMU tablet's event driver, never a similarly named property on another HID.
static inline NSNumber *VIMObservedTabletTrim(NSString *registry) {
    if(registry.length>32768)return nil;
    NSNumber *observed=nil;
    for(NSString *entry in [registry componentsSeparatedByString:@"+-o "]) {
        NSArray<NSString *> *lines=[entry componentsSeparatedByCharactersInSet:NSCharacterSet.newlineCharacterSet];
        if(![lines.firstObject containsString:@"<class IOHIDEventDriver,"])continue;
        BOOL tablet=NO;NSNumber *trim=nil;
        for(NSString *line in lines) {
            NSString *field=[line stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
            if([field isEqual:@"\"Product\" = \"QEMU USB Tablet\""])tablet=YES;
            NSString *prefix=@"\"AbsoluteAxisBoundsRemovalPercentage\" = ";
            if([field hasPrefix:prefix]) {
                NSScanner *scanner=[NSScanner scannerWithString:[field substringFromIndex:prefix.length]];
                int value=0;
                if([scanner scanInt:&value]&&scanner.isAtEnd&&value>=0&&value<100)trim=@(value);
            }
        }
        if(tablet&&trim){if(observed)return nil;observed=trim;}
    }
    return observed;
}
