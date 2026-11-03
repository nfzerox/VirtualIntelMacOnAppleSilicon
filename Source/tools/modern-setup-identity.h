#pragma once
#import <Foundation/Foundation.h>

// Apple's APFS conversion can replace the HFS volume UUID and split System
// and Data, but retains the GPT partition backing the new container. This is
// a cold installation identity check, never a graphics or normal-boot path.
static inline BOOL VIMModernAPFSStoreMatches(NSDictionary *layout,NSString *volume,NSString *partition) {
    NSUInteger matches=0;
    for(NSDictionary *container in layout[@"Containers"]){
        BOOL contains=NO;
        for(NSDictionary *entry in container[@"Volumes"])
            if([entry[@"APFSVolumeUUID"] isEqual:volume])contains=YES;
        if(!contains)continue;
        NSArray *stores=container[@"PhysicalStores"];
        if(stores.count==1&&[stores[0][@"DiskUUID"] isEqual:partition])matches++;
    }
    return matches==1;
}
static inline NSDictionary *VIMModernDiskMetadata(NSArray *arguments) {
    NSTask *task=[[NSTask alloc] init];NSPipe *pipe=[NSPipe pipe];
    [task setLaunchPath:@"/usr/sbin/diskutil"];[task setArguments:arguments];
    [task setStandardInput:[NSFileHandle fileHandleWithNullDevice]];
    [task setStandardOutput:pipe];[task setStandardError:[NSFileHandle fileHandleWithNullDevice]];
    NSDictionary *value=nil;
    @try{
        [task launch];NSData *data=[[pipe fileHandleForReading] readDataToEndOfFile];[task waitUntilExit];
        id decoded=[task terminationStatus]==0&&data.length<1024*1024?
            [NSPropertyListSerialization propertyListWithData:data options:0 format:NULL error:NULL]:nil;
        if([decoded isKindOfClass:[NSDictionary class]])value=decoded;
    }@catch(NSException *exception){(void)exception;}
#if !__has_feature(objc_arc)
    [task release];
#endif
    return value;
}
static inline BOOL VIMModernRootStoreMatches(NSString *volume,NSString *partition) {
    NSDictionary *root=VIMModernDiskMetadata(@[@"info",@"-plist",@"/"]);
    if([root[@"VolumeUUID"] isEqual:volume]&&[root[@"DiskUUID"] isEqual:partition])return YES;
    return VIMModernAPFSStoreMatches(VIMModernDiskMetadata(@[@"apfs",@"list",@"-plist"]),volume,partition);
}
static NSString *const VIMModernInstallerOwnership=@"/Library/Application Support/Virtual Intel Mac/ModernInstallerSetup/Ownership.plist";
