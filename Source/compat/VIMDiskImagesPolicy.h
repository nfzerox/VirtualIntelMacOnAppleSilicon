#pragma once
#import <Foundation/Foundation.h>

// Prefer Apple's newer diskimagesiod backend when the host provides its CLI.
// Probe once, not by host architecture or library filesystem. Older hosts
// retain hdiutil; failed real attachments are never retried on another backend.
NS_INLINE BOOL VIMModernDiskImagesAvailable(void) {
    static BOOL available;static dispatch_once_t once;
    dispatch_once(&once,^{
        NSTask *task=[NSTask new];task.executableURL=[NSURL fileURLWithPath:@"/usr/sbin/diskutil"];
        task.arguments=@[@"image",@"attach",@"--help"];
        task.standardInput=task.standardOutput=task.standardError=NSFileHandle.fileHandleWithNullDevice;
        if([task launchAndReturnError:nil]){[task waitUntilExit];available=task.terminationStatus==0;}
    });
    return available;
}

NS_INLINE NSArray<NSString *> *VIMModernWritableImageArguments(NSArray<NSString *> *legacy) {
    if(![legacy.lastObject.pathExtension.lowercaseString isEqual:@"sparsebundle"]||
       [legacy containsObject:@"-readonly"]||[legacy containsObject:@"-imagekey"])return nil;
    NSMutableArray *arguments=[@[@"image",@"attach",@"--plist",@"--nobrowse"] mutableCopy];
    for(NSUInteger i=0;i+1<legacy.count;i++) {
        NSString *option=legacy[i];
        if([option isEqual:@"-nomount"])[arguments addObject:@"--noMount"];
        else if([option isEqual:@"-mountpoint"]&&i+2<legacy.count)
            [arguments addObjectsFromArray:@[@"--mountPoint",legacy[++i]]];
        else if([option isEqual:@"-owners"]&&i+2<legacy.count)
            [arguments addObjectsFromArray:@[@"--mountOptions",[legacy[++i] isEqual:@"off"]?@"noowners":@"owners"]];
        else if(![option isEqual:@"-noverify"])return nil;
    }
    [arguments addObject:legacy.lastObject];return arguments;
}

NS_INLINE NSArray<NSDictionary *> *VIMNormalizeDiskImageEntities(NSArray<NSDictionary *> *entities) {
    NSMutableArray *result=[NSMutableArray arrayWithCapacity:entities.count];
    for(NSDictionary *entity in entities) {
        NSMutableDictionary *row=[entity mutableCopy];NSString *device=row[@"dev-entry"];
        if([device hasPrefix:@"disk"])row[@"dev-entry"]=[@"/dev/" stringByAppendingString:device];
        [result addObject:row];
    }
    return result;
}
