#import "VIMBigSurFirmlinks.h"
#import "VIMBigSurArchive.h"
#import "VIMCatalinaDataLayout.h"
#include <AppleArchive/AppleArchive.h>
#include <sys/stat.h>
#include <sys/xattr.h>

typedef struct {const uint8_t *bytes;size_t remaining;} Memory;
static ssize_t ReadMemory(void *opaque,void *out,size_t size) {
    Memory *memory=opaque;size_t count=MIN(memory->remaining,size);
    memcpy(out,memory->bytes,count);memory->bytes+=count;memory->remaining-=count;return (ssize_t)count;
}
static int CloseMemory(__unused void *opaque) {return 0;}
static id Failure(NSError **error,NSString *message) {
    if(error)*error=[NSError errorWithDomain:@"VirtualIntelMac.BigSurFirmlinks" code:1 userInfo:@{NSLocalizedDescriptionKey:message}];
    return nil;
}
static BOOL RelativePath(NSString *path) {
    if(!path.length||path.isAbsolutePath)return NO;
    for(NSString *part in [path componentsSeparatedByString:@"/"])
        if(!part.length||[part isEqual:@"."]||[part isEqual:@".."])return NO;
    return YES;
}

static NSString *Target(AAArchiveStream archive,AAHeader header) {
    uint64_t size,offset;
    const AAFieldKey attrs={.skey="XAT"};
    if(AAHeaderGetFieldBlobWithKey(header,attrs,&size,&offset)!=1||offset||!size||size>65536)return nil;
    NSMutableData *bytes=[NSMutableData dataWithLength:(NSUInteger)size];
    if(AAArchiveStreamReadBlob(archive,attrs,bytes.mutableBytes,bytes.length)<0)return nil;
    AAEntryXATBlob blob=AAEntryXATBlobCreateWithEncodedData(bytes.bytes,bytes.length);
    if(!blob)return nil;
    NSString *target=nil;BOOL valid=YES;
    for(uint32_t index=0;index<AAEntryXATBlobGetEntryCount(blob);index++) {
        char name[256];uint8_t value[4096];size_t length=0;
        if(AAEntryXATBlobGetEntry(blob,index,sizeof(name),name,NULL,sizeof(value),value,&length)<0){valid=NO;break;}
        if(strcmp(name,"com.apple.fs.firmlink"))continue;
        if(target||!length||value[length-1]||memchr(value,0,length-1)){valid=NO;break;}
        target=[[NSString alloc] initWithBytes:value length:length-1 encoding:NSUTF8StringEncoding];
        if(!RelativePath(target)){valid=NO;break;}
    }
    AAEntryXATBlobDestroy(blob);
    return valid?target:nil;
}

static NSDictionary *ReadManifest(NSURL *zip,NSError **error) {
    NSData *payload=VIMBigSurReadSmallArchiveMember(zip,@"AssetData/payloadv2/firmlinks_payload",error);
    if(!payload)return nil;
    Memory memory={payload.bytes,payload.length};AAByteStream source=AACustomByteStreamOpen();
    if(!source)return Failure(error,@"Could not open the Big Sur firmlink payload.");
    AACustomByteStreamSetData(source,&memory);AACustomByteStreamSetReadProc(source,ReadMemory);
    AACustomByteStreamSetCloseProc(source,CloseMemory);
    AAArchiveStream archive=AADecodeArchiveInputStreamOpen(source,NULL,NULL,0,1);
    AAHeader header=NULL;NSMutableDictionary *rows=[NSMutableDictionary dictionary],*targets=[NSMutableDictionary dictionary];BOOL valid=archive!=NULL;
    const AAFieldKey path={.skey="PAT"},type={.skey="TYP"},flags={.skey="FLG"},user={.skey="UID"},group={.skey="GID"},mode={.skey="MOD"};
    while(valid) {
        int status=AAArchiveStreamReadHeader(archive,&header);
        if(!status)break;
        char name[4096];size_t length;uint64_t t,f,u,g,m;
        if(status<0||AAHeaderGetFieldStringWithKey(header,path,sizeof(name),name,&length)!=1||length>=sizeof(name)||strlen(name)!=length||
            AAHeaderGetFieldUIntWithKey(header,type,&t)!=1||t!='D'||AAHeaderGetFieldUIntWithKey(header,flags,&f)!=1||!(f&0x00800000)||
            AAHeaderGetFieldUIntWithKey(header,user,&u)!=1||u>UINT32_MAX||AAHeaderGetFieldUIntWithKey(header,group,&g)!=1||g>UINT32_MAX||
            AAHeaderGetFieldUIntWithKey(header,mode,&m)!=1||m>07777) {valid=NO;break;}
        NSString *relative=@(name);
        NSString *target=Target(archive,header);
        if(!RelativePath(relative)||rows[relative]||!target){valid=NO;break;}
        rows[relative]=@[@(u),@(g),@(S_IFDIR|m)];
        targets[relative]=target;
    }
    if(header)AAHeaderDestroy(header);
    if(archive&&AAArchiveStreamClose(archive))valid=NO;
    if(AAByteStreamClose(source))valid=NO;
    return valid&&rows.count?@{@"Metadata":rows.copy,@"Targets":targets.copy}:
        Failure(error,@"The original System firmlink directory payload is invalid.");
}

NSDictionary<NSString *,NSArray<NSNumber *> *> *VIMBigSurFirmlinkMetadata(NSURL *zip,NSError **error) {
    return ReadManifest(zip,error)[@"Metadata"];
}

NSDictionary *VIMBigSurFirmlinkPlan(NSURL *zip,NSString *text,NSError **error) {
    NSDictionary *manifest=ReadManifest(zip,error);
    if(!manifest)return nil;
    NSDictionary *pairs=VIMParseSystemFirmlinks(text,error);
    if(!pairs)return nil;
    NSDictionary *targets=manifest[@"Targets"];
    for(NSString *path in targets)if(![pairs[path] isEqual:targets[path]])
        return Failure(error,@"The original System and installer firmlink targets disagree.");
    NSMutableSet *inactive=[NSMutableSet setWithArray:pairs.allKeys];[inactive minusSet:[NSSet setWithArray:targets.allKeys]];
    return @{@"Metadata":manifest[@"Metadata"],@"Targets":targets,
        @"InactiveManifestPaths":[inactive.allObjects sortedArrayUsingSelector:@selector(compare:)]};
}

NSDictionary *VIMBigSurReserveFirmlinks(NSURL *zip,NSURL *system,NSError **error) {
    NSString *text=[NSString stringWithContentsOfURL:[system URLByAppendingPathComponent:@"usr/share/firmlinks"] encoding:NSUTF8StringEncoding error:error];
    if(!text)return nil;
    NSDictionary *plan=VIMBigSurFirmlinkPlan(zip,text,error);
    if(!plan)return nil;
    NSDictionary *metadata=plan[@"Metadata"],*pairs=plan[@"Targets"];
    NSMutableDictionary *stubs=[NSMutableDictionary dictionary],*permissions=[NSMutableDictionary dictionary];
    NSString *root=[system.URLByResolvingSymlinksInPath.path stringByAppendingString:@"/"];
    for(NSString *path in [pairs.allKeys sortedArrayUsingSelector:@selector(compare:)]) {
        NSURL *directory=[system URLByAppendingPathComponent:path];
        NSString *resolved=directory.URLByDeletingLastPathComponent.URLByResolvingSymlinksInPath.path;
        NSArray *entry=metadata[path];const char *target=[pairs[path] UTF8String];struct stat info;
        if((![resolved hasPrefix:root]&&![resolved isEqual:system.URLByResolvingSymlinksInPath.path])||
            mkdir(directory.fileSystemRepresentation,[entry[2] unsignedShortValue]&07777)||
            lstat(directory.fileSystemRepresentation,&info)||!S_ISDIR(info.st_mode)||
            setxattr(directory.fileSystemRepresentation,"com.apple.fs.firmlinj",target,strlen(target)+1,0,XATTR_NOFOLLOW))
            return Failure(error,[NSString stringWithFormat:@"Could not reserve Big Sur’s System-to-Data link at %@.",path]);
        stubs[@(info.st_ino)]=[NSData dataWithBytes:target length:strlen(target)+1];permissions[@(info.st_ino)]=entry;
    }
    return @{@"FirmlinkInodes":stubs.copy,@"SystemPermissions":permissions.copy};
}
