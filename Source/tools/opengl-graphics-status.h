#pragma once
#import <Foundation/Foundation.h>

// Graphics journals can inherit host locale data during offline installation.
// Send the public scalar graphics evidence, not the entire private journal.
static inline NSDictionary *VIMOpenGLGraphicsStatus(NSDictionary *journal) {
    NSMutableDictionary *status=[NSMutableDictionary dictionary];
    NSArray *keys=[NSArray arrayWithObjects:@"FormatVersion",@"Guest",@"Phase",
        @"MachineIdentifier",@"VolumeUUID",@"ProductBuildVersion",@"OriginalSourceBuild",
        @"GraphicsSupportRevision",@"EngineSHA256",@"BackendSHA256",@"GASHA256",@"GAInfoSHA256",
        @"OriginalSHA256",@"PreviousEngineSHA256",@"NativeDispatchCount",
        @"ChangedThisBoot",@"InstalledBootSessionUUID",@"PrebootInstall",@"DesktopQualified",nil];
    for(NSUInteger index=0;index<[keys count];index++) {
        NSString *key=[keys objectAtIndex:index];id value=[journal objectForKey:key];
        if([value isKindOfClass:[NSString class]]||[value isKindOfClass:[NSNumber class]])
            [status setObject:value forKey:key];
    }
    return status;
}
