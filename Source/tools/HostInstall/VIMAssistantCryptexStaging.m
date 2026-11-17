#import "VIMAssistantCryptex.h"
#import "VIMBigSurArchive.h"
#include "../../vendor/source/dyld/include/mach-o/dyld_cache_format.h"
#include <fcntl.h>
#include <sys/clonefile.h>
#include <sys/stat.h>
#include <unistd.h>

static BOOL FailCryptex(NSError **error,NSInteger code,NSString *message) {
    if(error)*error=[NSError errorWithDomain:@"VirtualIntelMac.AssistantCryptex" code:code
        userInfo:@{NSLocalizedDescriptionKey:message}];
    return NO;
}

static BOOL IsCancelled(BOOL (^cancelled)(void),NSError **error) {
    if(!cancelled||!cancelled())return NO;
    FailCryptex(error,ECANCELED,@"Installation cancelled.");
    return YES;
}

static BOOL Magic(NSURL *file,off_t offset,const char *magic,size_t length) {
    int fd=open(file.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    char bytes[8];
    struct stat info;
    BOOL valid=fd>=0&&length<=sizeof(bytes)&&!fstat(fd,&info)&&S_ISREG(info.st_mode)&&
        pread(fd,bytes,length,offset)==(ssize_t)length&&!memcmp(bytes,magic,length);
    if(fd>=0)close(fd);
    return valid;
}

static int OpenIntelCache(NSURL *url,struct dyld_cache_header *header) {
    const size_t needed=offsetof(struct dyld_cache_header,cacheSubType);
    int fd=open(url.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    struct stat info;
    if(fd<0)return -1;
    if(fstat(fd,&info)||!S_ISREG(info.st_mode)||
       pread(fd,header,needed,0)!=(ssize_t)needed||memcmp(header->magic,"dyld_v1 x86_64h",16)||
       header->mappingOffset<needed||header->mappingOffset>info.st_size||
       !header->mappingCount||!header->codeSignatureSize||
       header->codeSignatureOffset>(uint64_t)info.st_size||
       header->codeSignatureSize>(uint64_t)info.st_size-header->codeSignatureOffset) {
        close(fd);return -1;
    }
    return fd;
}

BOOL VIMVerifyAssistantIntelCryptexCaches(NSURL *mountedImage,NSError **error) {
    NSString *base=@"dyld_shared_cache_x86_64h";
    NSURL *directory=[mountedImage URLByAppendingPathComponent:@"System/Library/dyld" isDirectory:YES];
    NSURL *main=[directory URLByAppendingPathComponent:base];
    struct dyld_cache_header cache={0};
    int fd=OpenIntelCache(main,&cache);
    if(fd<0)return FailCryptex(error,EBADMSG,@"The original macOS Cryptex has no valid Intel shared cache.");
    struct dyld_subcache_entry entries[32];
    size_t bytes=0;
    BOOL valid=cache.mappingOffset>offsetof(struct dyld_cache_header,cacheSubType)&&
        cache.subCacheArrayOffset>=offsetof(struct dyld_cache_header,cacheSubType)&&
        cache.subCacheArrayOffset<=cache.codeSignatureOffset&&
        cache.subCacheArrayCount&&cache.subCacheArrayCount<=sizeof(entries)/sizeof(entries[0]);
    if(valid) {
        bytes=cache.subCacheArrayCount*sizeof(entries[0]);
        valid=bytes<=cache.codeSignatureOffset-cache.subCacheArrayOffset&&
            pread(fd,entries,bytes,cache.subCacheArrayOffset)==(ssize_t)bytes;
    }
    close(fd);
    NSMutableSet *seen=[NSMutableSet set];
    for(uint32_t i=0;valid&&i<cache.subCacheArrayCount;i++) {
        const char *suffix=entries[i].fileSuffix;
        size_t length=strnlen(suffix,sizeof(entries[i].fileSuffix));
        NSString *leaf=length<sizeof(entries[i].fileSuffix)?
            [[NSString alloc] initWithBytes:suffix length:length encoding:NSASCIIStringEncoding]:nil;
        if(!leaf.length||[leaf rangeOfString:@"^\\.[0-9]+([.][a-z]+)?$" options:NSRegularExpressionSearch].location==NSNotFound||
           [seen containsObject:leaf]){valid=NO;break;}
        [seen addObject:leaf];
        struct dyld_cache_header child={0};
        int sub=OpenIntelCache([directory URLByAppendingPathComponent:[base stringByAppendingString:leaf]],&child);
        valid=sub>=0&&!memcmp(child.uuid,entries[i].uuid,sizeof(child.uuid));
        if(sub>=0)close(sub);
    }
    if(!valid)return FailCryptex(error,EBADMSG,@"A macOS Intel shared-cache fragment is missing or does not match the main cache.");
    NSURL *driverKit=[mountedImage URLByAppendingPathComponent:@"System/DriverKit/System/Library/dyld" isDirectory:YES];
    struct dyld_cache_header driver={0};
    int driverFD=OpenIntelCache([driverKit URLByAppendingPathComponent:base],&driver);
    if(driverFD<0)return FailCryptex(error,EBADMSG,@"The original macOS Cryptex has no valid Intel DriverKit cache.");
    close(driverFD);
    return YES;
}

static BOOL DecodeImage(NSURL *patch,NSURL *output,NSURL *decoder,NSError **error) {
    if(!Magic(patch,0,"RIDIFF10",8))
        return FailCryptex(error,EINVAL,@"The original macOS Cryptex image patch has an unsupported format.");
    NSTask *task=[NSTask new];task.executableURL=decoder;
    task.arguments=@[@"--library",VIMAssistantCryptexDecoderLibrary(decoder).path,patch.path,output.path];
    task.standardInput=NSFileHandle.fileHandleWithNullDevice;
    if(![task launchAndReturnError:error])return NO;
    // No elapsed deadline: cancellation waits for this native file operation
    // to finish before any image is detached or scratch file is removed.
    [task waitUntilExit];
    if(task.terminationStatus||!Magic(output,32,"NXSB",4))
        return FailCryptex(error,EIO,@"Could not decode the original macOS Cryptex APFS image.");
    return YES;
}

BOOL VIMStageAssistantIntelCryptex(NSURL *zip,NSURL *preboot,NSURL *scratch,
    NSUUID *dataUUID,NSString *build,NSURL *decoder,BOOL (^cancelled)(void),NSError **error) {
    if(!dataUUID||!build.length)
        return FailCryptex(error,EINVAL,@"The Intel Cryptex destination has no System build or Data UUID.");
    NSFileManager *fm=NSFileManager.defaultManager;
    NSURL *sourceManifest=[scratch URLByAppendingPathComponent:@"CryptexBuildManifest.plist"];
    NSArray *members=VIMBigSurArchiveMembers(zip,error);
    if(!members||!VIMCopyOriginalBootMembers(zip,@{@"AssetData/boot/BuildManifest.plist":sourceManifest},cancelled,error))return NO;
    NSDictionary *manifest=[NSDictionary dictionaryWithContentsOfURL:sourceManifest];
    NSDictionary *plan=VIMAssistantIntelCryptexPlan(members,manifest,error);
    if(!plan||![fm removeItemAtURL:sourceManifest error:error])return NO;
    NSURL *root=[[preboot URLByAppendingPathComponent:dataUUID.UUIDString] URLByAppendingPathComponent:@"cryptex1/current"];
    NSMutableDictionary<NSString *,NSURL *> *inputs=[NSMutableDictionary dictionary];
    for(NSString *leaf in plan[@"Files"])
        inputs[plan[@"Files"][leaf]]=[root URLByAppendingPathComponent:leaf];
    // Decode one large image at a time. The compressed input stays on the
    // library drive, not the mounted Preboot volume or the host startup disk.
    if(!VIMCopyOriginalBootMembers(zip,inputs,cancelled,error))return NO;
    NSDictionary *version=[NSDictionary dictionaryWithContentsOfURL:[root URLByAppendingPathComponent:@"SystemVersion.plist"]];
    if(![version[@"ProductBuildVersion"] isEqual:build])
        return FailCryptex(error,EINVAL,@"The Intel Cryptex assets do not match the staged System build.");
    NSData *manifestData=[NSPropertyListSerialization dataWithPropertyList:plan[@"BuildManifest"]
        format:NSPropertyListXMLFormat_v1_0 options:0 error:error];
    if(!manifestData||![manifestData writeToURL:[root URLByAppendingPathComponent:@"BuildManifest.plist"]
        options:NSDataWritingWithoutOverwriting error:error])return NO;
    for(NSString *leaf in @[@"app.dmg",@"os.dmg"]) {
        if(IsCancelled(cancelled,error))return NO;
        NSURL *patch=[scratch URLByAppendingPathComponent:[leaf stringByAppendingPathExtension:@"patch"]];
        BOOL copied=VIMCopyOriginalBootMembers(zip,@{plan[@"ImagePatches"][leaf]:patch},cancelled,error);
        BOOL decoded=copied&&!IsCancelled(cancelled,error)&&DecodeImage(patch,[root URLByAppendingPathComponent:leaf],decoder,error);
        if(copied&&![fm removeItemAtURL:patch error:decoded?error:nil]&&decoded)return NO;
        if(!decoded||IsCancelled(cancelled,error))return NO;
    }
    NSURL *original=[root URLByAppendingPathComponent:@"os.dmg"];
    NSURL *clone=[root URLByAppendingPathComponent:@"os.clone.dmg"];
    if(clonefile(original.fileSystemRepresentation,clone.fileSystemRepresentation,CLONE_NOFOLLOW))
        return FailCryptex(error,errno,@"Could not clone the staged Intel Cryptex on its Preboot volume.");
    return YES;
}
