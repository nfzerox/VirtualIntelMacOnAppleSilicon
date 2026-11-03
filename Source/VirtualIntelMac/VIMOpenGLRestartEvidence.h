#pragma once
#import <Foundation/Foundation.h>

static inline BOOL VIMOpenGLInitialGraphicsCanResume(NSDictionary *report,NSString *machine,
        NSString *volume,BOOL installing,BOOL systemMatches) {
    id graphics=report[@"Graphics"];
    // Old status tools report the identifier from the root-owned setup
    // journal, but only expose a combined ownership/graphics boolean.
    // An explicit failure from a newer tool is never eligible for retry.
    id authorization=report[@"InstallationOwnershipVerified"];
    return installing&&systemMatches&&[report[@"KernelMatchesSystem"] isEqual:@YES]&&
        [report[@"MachineIdentifier"] isEqual:machine]&&[report[@"VolumeUUID"] isEqual:volume]&&
        (!authorization||[authorization isEqual:@YES])&&
        [graphics isKindOfClass:NSDictionary.class]&&![graphics count];
}

static inline BOOL VIMOpenGLBundledSupportMatches(NSString *guest,NSDictionary *report,NSDictionary *expected) {
    NSDictionary *graphics=report[@"Graphics"];
    BOOL pair=[graphics[@"EngineSHA256"] isEqual:expected[@"EngineSHA256"]]&&
        [graphics[@"BackendSHA256"] isEqual:expected[@"BackendSHA256"]];
    // Tiger already uses its separate boot-fenced installer; its graphics
    // repair does not replace these common controls. Do not loop it here.
    return pair&&([guest isEqual:@"tiger"]||[report[@"GraphicsRestartEvidenceVersion"] isEqual:@1]);
}

static inline BOOL VIMOpenGLGraphicsNeedsRestart(NSDictionary *report,NSDictionary *restart,NSString *boot) {
    NSDictionary *graphics=report[@"Graphics"];
    if(![graphics[@"ChangedThisBoot"] boolValue])return NO;
    NSString *installed=graphics[@"InstalledBootSessionUUID"];
    if([installed isKindOfClass:NSString.class]&&installed.length)return [installed isEqual:boot];
    // Older receipts predate the boot field. One acknowledged restart into a
    // different validated boot satisfies only the exact installed pair/build
    // and authorized machine/volume recorded in that transaction. Never turn
    // a stale persistent boolean into a fresh restart on every cold boot.
    NSString *from=restart[@"FromBootSessionUUID"];
    if(![restart[@"RequestState"] isEqual:@"acknowledged"]||
       ![from isKindOfClass:NSString.class]||![[NSUUID alloc] initWithUUIDString:from]||[from isEqual:boot]||
       ![restart[@"MachineIdentifier"] isEqual:report[@"MachineIdentifier"]])return YES;
    NSDictionary *before=restart[@"GuestReport"];
    for(NSString *key in @[@"MachineIdentifier",@"VolumeUUID",@"ProductBuildVersion"])
        if(![before[key] isEqual:report[key]])return YES;
    for(NSString *key in @[@"EngineSHA256",@"BackendSHA256"])
        if(![before[@"Graphics"][key] isEqual:graphics[key]])return YES;
    return NO;
}
