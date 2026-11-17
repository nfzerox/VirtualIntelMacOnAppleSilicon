#import "VIMBigSurFinalize.h"
#import "VIMAPFSFirmlinks.h"
#import "VIMAPFSJumpstart.h"
#import "VIMBigSurSealTree.h"
#import "VIMBigSurSnapshot.h"
#import "VIMHFSCatalog.h"
#include <unistd.h>

static NSUUID *Volume(NSDictionary *group,NSString *name) {
    NSString *uuid=group[name][@"APFSVolumeUUID"];
    return uuid?[[NSUUID alloc] initWithUUIDString:uuid]:nil;
}

static NSDictionary *Failure(NSError **error,NSInteger code,NSString *description) {
    if(error)*error=[NSError errorWithDomain:@"VirtualIntelMac.BigSurFinalization" code:code
        userInfo:@{NSLocalizedDescriptionKey:description}];
    return nil;
}

NSDictionary *VIMFinalizeBigSurDisk(NSURL *raw,NSURL *zip,NSDictionary *group,
    NSDictionary *staged,NSURL *sealInputs,NSData *driver,NSURL *scratchTree,
    VIMAssistantCacheLayout cacheLayout,
    BOOL (^repair)(NSArray<NSString *> *,NSError **),
    BOOL (^cancelled)(void),void (^progress)(NSString *),NSError **error) {
    NSUUID *system=Volume(group,@"Macintosh HD"),*data=Volume(group,@"Macintosh HD - Data");
    NSUUID *preboot=Volume(group,@"Preboot"),*recovery=Volume(group,@"Recovery");
    NSUUID *snapshot=staged[@"SnapshotUUID"]?[[NSUUID alloc] initWithUUIDString:staged[@"SnapshotUUID"]]:nil;
    NSData *root=staged[@"RootHash"];
    NSDictionary *firmlinkRecords=staged[@"Firmlinks"];
    NSDictionary<NSNumber *,NSData *> *firmlinks=[firmlinkRecords isKindOfClass:NSDictionary.class]?
        firmlinkRecords[@"FirmlinkInodes"]:nil;
    NSDictionary *dataModes=staged[@"DataPermissions"];
    NSDictionary *prebootModes=staged[@"PrebootPermissions"],*recoveryModes=staged[@"RecoveryPermissions"];
    if(!system||!data||!preboot||!recovery||!snapshot||root.length!=32||!driver.length||
       ![firmlinks isKindOfClass:NSDictionary.class]||!firmlinks.count||!dataModes.count||
       !prebootModes.count||!recoveryModes.count||
       (cacheLayout!=VIMAssistantCachesInline&&
        (cacheLayout!=VIMAssistantCachesIntelCryptex||![staged[@"IntelCryptexVerified"] boolValue])))
        return Failure(error,EINVAL,@"The detached Big Sur volume group is incomplete.");

    uint64_t offset=0,size=0;
    if(!VIMGetAPFSPartition(raw,&offset,&size,error))return nil;
    if(progress)progress(@"metadata");
    if(cancelled&&cancelled())return Failure(error,ECANCELED,@"Installation cancelled.");
    if(!VIMFinalizeBigSurSystemMetadata(raw,offset,size,system,zip,firmlinks,repair,error))return nil;
    NSArray *volumes=@[data,preboot,recovery],*roles=@[@(VIMAPFSDataRole),@0x10,@0x4];
    NSArray *permissions=@[dataModes,prebootModes,recoveryModes];
    for(NSUInteger i=0;i<volumes.count;i++) {
        if(cancelled&&cancelled())return Failure(error,ECANCELED,@"Installation cancelled.");
        if(!VIMFinalizeAPFSVolume(raw,offset,size,volumes[i],[roles[i] unsignedShortValue],nil,permissions[i],error))return nil;
    }
    if(cancelled&&cancelled())return Failure(error,ECANCELED,@"Installation cancelled.");
    if(progress)progress(@"seal");
    NSURL *mtree=[sealInputs URLByAppendingPathComponent:@"mtree.txt"];
    NSDictionary *tree=VIMBuildBigSurSealedFilesystem(raw,offset,size,system,
        mtree,[sealInputs URLByAppendingPathComponent:@"digest.db"],
        zip,root,scratchTree,cancelled,error);
    if(!tree)return nil;
    @try {
        if(cancelled&&cancelled())return Failure(error,ECANCELED,@"Installation cancelled.");
        if(progress)progress(@"snapshot");
        if(!VIMPublishAPFSJumpstart(raw,driver,error)||
           !VIMPublishBigSurSnapshot(raw,system,snapshot,scratchTree,[tree[@"NodeCount"] unsignedLongLongValue],
               root,cacheLayout==VIMAssistantCachesInline?mtree:nil,tree[@"ObjectIDs"],cancelled,error))return nil;
        return @{@"SnapshotUUID":snapshot.UUIDString,@"RootHash":root,@"NodeCount":tree[@"NodeCount"],
                 @"RecordCount":tree[@"RecordCount"]};
    } @finally {unlink(scratchTree.fileSystemRepresentation);}
}
