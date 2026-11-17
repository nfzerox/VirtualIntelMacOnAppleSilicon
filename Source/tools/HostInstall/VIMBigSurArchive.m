#import "VIMBigSurArchive.h"
#import "VIMBigSurBootAssets.h"
#import "../../compat/VIMEmptySourceRecoveryPatch.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

// macOS ships libarchive and its link stub but not its public headers.
// This is the small public libarchive ZIP-reader ABI we use.
// https://github.com/libarchive/libarchive/tree/master/libarchive
struct archive;
struct archive_entry;
extern struct archive *archive_read_new(void);
extern int archive_read_support_format_zip_seekable(struct archive *);
extern int archive_read_open_fd(struct archive *,int,size_t);
extern int archive_read_next_header(struct archive *,struct archive_entry **);
extern int archive_read_data_skip(struct archive *);
extern ssize_t archive_read_data(struct archive *,void *,size_t);
extern int archive_read_free(struct archive *);
extern const char *archive_error_string(struct archive *);
extern const char *archive_entry_pathname(struct archive_entry *);
extern unsigned int archive_entry_filetype(struct archive_entry *);
extern int64_t archive_entry_size(struct archive_entry *);

enum { ArchiveOK=0, ArchiveEOF=1, ReadBuffer=1024*1024 };
static NSString *const KernelCollection=@"System/Library/KernelCollections/BootKernelExtensions.kc";
static NSString *const SystemVersion=@"System/Library/CoreServices/SystemVersion.plist";
static NSString *const Payload=@"AssetData/payloadv2/";

static BOOL Fail(NSError **error,NSInteger code,NSString *message) {
    if(error)*error=[NSError errorWithDomain:@"VirtualIntelMac.BigSurArchive" code:code
        userInfo:@{NSLocalizedDescriptionKey:message}];
    return NO;
}

static BOOL ArchiveFailure(struct archive *archive,NSError **error) {
    const char *message=archive?archive_error_string(archive):NULL;
    return Fail(error,EIO,message?(@(message)?:@"Could not read the original Big Sur archive."):@"Could not open the original Big Sur archive.");
}

static BOOL Cancelled(BOOL (^cancelled)(void),NSError **error) {
    if(!cancelled||!cancelled())return NO;
    Fail(error,ECANCELED,@"Installation cancelled.");return YES;
}

typedef BOOL (^ArchiveMember)(NSString *name,struct archive *reader,struct archive_entry *entry);

static BOOL Visit(NSURL *zip,ArchiveMember member,NSError **error) {
    int fd=open(zip.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
    struct stat info;
    struct archive *reader=NULL;
    BOOL ok=fd>=0&&!fstat(fd,&info)&&S_ISREG(info.st_mode);
    if(!ok)Fail(error,EINVAL,@"The Big Sur archive is not a readable regular file.");
    @try {
        if(!ok)return NO;
        reader=archive_read_new();
        if(!reader||archive_read_support_format_zip_seekable(reader)!=ArchiveOK||
            archive_read_open_fd(reader,fd,64*1024)!=ArchiveOK)return ArchiveFailure(reader,error);
        for(;;) @autoreleasepool {
            struct archive_entry *entry=NULL;
            int status=archive_read_next_header(reader,&entry);
            if(status==ArchiveEOF)return YES;
            if(status!=ArchiveOK)return ArchiveFailure(reader,error);
            const char *path=archive_entry_pathname(entry);
            NSString *name=path?@(path):nil;
            if(!name.length)return Fail(error,EINVAL,@"An original Big Sur archive member has no usable path.");
            if(!member(name,reader,entry))return NO;
            if(archive_read_data_skip(reader)!=ArchiveOK)return ArchiveFailure(reader,error);
        }
    } @finally {
        if(reader)archive_read_free(reader);
        if(fd>=0)close(fd);
    }
}

NSArray<NSString *> *VIMBigSurArchiveMembers(NSURL *zip,NSError **error) {
    NSMutableArray *paths=[NSMutableArray array];
    return Visit(zip,^BOOL(NSString *name,__unused struct archive *reader,__unused struct archive_entry *entry) {
        [paths addObject:name];return YES;
    },error)?paths.copy:nil;
}

static NSData *ReadMember(struct archive *reader,struct archive_entry *entry,NSError **error) {
    int64_t size=archive_entry_size(entry);
    if(archive_entry_filetype(entry)!=S_IFREG||size<=0||size>1024*1024) {
        Fail(error,EINVAL,@"An original Big Sur manifest has an invalid size or type.");return nil;
    }
    NSMutableData *data=[NSMutableData dataWithLength:(NSUInteger)size];
    uint8_t *cursor=data.mutableBytes;
    for(int64_t left=size;left>0;) {
        ssize_t n=archive_read_data(reader,cursor,(size_t)left);
        if(n<=0){ArchiveFailure(reader,error);return nil;}
        cursor+=n;left-=n;
    }
    uint8_t end;
    if(archive_read_data(reader,&end,1)!=0){ArchiveFailure(reader,error);return nil;}
    return data;
}

NSData *VIMBigSurReadSmallArchiveMember(NSURL *zip,NSString *member,NSError **error) {
    __block NSData *result=nil;__block NSError *issue=nil;
    BOOL ok=Visit(zip,^BOOL(NSString *name,struct archive *reader,struct archive_entry *entry){
        if(![name isEqual:member])return YES;
        if(result)return Fail(&issue,EINVAL,@"The original Big Sur archive contains a duplicate member.");
        result=ReadMember(reader,entry,&issue);return result!=nil;
    },error);
    if(!ok){if(error&&issue)*error=issue;return nil;}
    if(!result)Fail(error,ENOENT,@"The original Big Sur archive is missing a required member.");
    return result;
}

NSDictionary *VIMAssistantAssetForArchive(NSArray *assets,NSURL *archive,NSInteger major,NSError **error) {
    if(![assets isKindOfClass:NSArray.class]){
        Fail(error,EINVAL,@"The signed installer’s asset list is missing.");return nil;
    }
    NSString *relative=[archive.URLByDeletingLastPathComponent.lastPathComponent stringByAppendingPathComponent:archive.lastPathComponent];
    NSString *prefix=[NSString stringWithFormat:@"%ld.",(long)major];
    NSDictionary *selected=nil;
    for(id asset in assets) {
        if(![asset isKindOfClass:NSDictionary.class])continue;
        NSString *version=asset[@"OSVersion"],*build=asset[@"Build"];
        if(![version isKindOfClass:NSString.class]||![version hasPrefix:prefix]||
           ![build isKindOfClass:NSString.class]||!build.length)continue;
        id path=asset[@"__RelativePath"];
        if(path?![path isEqual:relative]:(major!=11||assets.count!=1))continue;
        // Monterey's hardware-specific MobileAsset records can all describe
        // the same ZIP. Their shared archive identity matters, not the count.
        if(selected&&(![selected[@"OSVersion"] isEqual:version]||![selected[@"Build"] isEqual:build])) {
            Fail(error,EINVAL,@"The signed installer contains conflicting identities for its System archive.");return nil;
        }
        if(!selected)selected=asset;
    }
    if(!selected)Fail(error,EINVAL,@"The signed installer does not identify this System archive for the selected release.");
    return selected;
}

NSDictionary *VIMBigSurPayloadInventory(NSURL *zip,NSDictionary *signedAsset,NSError **error) {
    return VIMAssistantPayloadInventory(zip,signedAsset,11,error);
}
NSDictionary *VIMAssistantPayloadInventory(NSURL *zip,NSDictionary *signedAsset,NSInteger major,NSError **error) {
    NSMutableArray<NSString *> *members=[NSMutableArray array];
    NSMutableDictionary<NSString *,NSNumber *> *sizes=[NSMutableDictionary dictionary];
    NSString *listPath=[Payload stringByAppendingString:@"payload_chunks.txt"];
    __block NSData *info=nil,*list=nil;
    __block NSError *streamError=nil;
    BOOL ok=Visit(zip,^BOOL(NSString *name,struct archive *reader,struct archive_entry *entry) {
        [members addObject:name];
        if([name isEqual:@"AssetData/Info.plist"]) {
            if(info)return Fail(&streamError,EINVAL,@"The original Big Sur archive has duplicate build metadata.");
            return (info=ReadMember(reader,entry,&streamError))!=nil;
        }
        if([name isEqual:listPath]) {
            if(list)return Fail(&streamError,EINVAL,@"The original Big Sur archive has duplicate payload lists.");
            return (list=ReadMember(reader,entry,&streamError))!=nil;
        }
        if([name hasPrefix:Payload]&&archive_entry_filetype(entry)==S_IFREG&&archive_entry_size(entry)>0)
            sizes[name]=@(archive_entry_size(entry));
        return YES;
    },error);
    if(!ok){if(error&&streamError)*error=streamError;return nil;}
    id metadata=info?[NSPropertyListSerialization propertyListWithData:info options:0 format:NULL error:error]:nil;
    id version=[metadata isKindOfClass:NSDictionary.class]?metadata[@"ProductVersion"]:nil;
    id build=[metadata isKindOfClass:NSDictionary.class]?metadata[@"Build"]:nil;
    NSString *release=[NSString stringWithFormat:@"%ld.",(long)major];
    if(major<11||![version isKindOfClass:NSString.class]||![version hasPrefix:release]||![version isEqual:signedAsset[@"OSVersion"]]||
       ![build isKindOfClass:NSString.class]||![build length]||![build isEqual:signedAsset[@"Build"]]) {
        Fail(error,EINVAL,@"The Apple package and its archive disagree about the selected release or build.");return nil;
    }
    NSString *text=list?[[NSString alloc] initWithData:list encoding:NSASCIIStringEncoding]:nil;
    NSMutableArray *chunks=[NSMutableArray array];NSCountedSet *names=[NSCountedSet setWithArray:members];
    __block BOOL sequence=YES;
    __block uint64_t total=0;
    [text enumerateLinesUsingBlock:^(NSString *line,BOOL *stop){
        NSArray<NSString *> *fields=[line componentsSeparatedByString:@":"];
        if(fields.count!=2||[line rangeOfString:@"^[0-9]+:[ \t]*[0-9]+$" options:NSRegularExpressionSearch].location==NSNotFound||
           fields[0].longLongValue!=(long long)chunks.count||fields[1].longLongValue<=0){sequence=NO;*stop=YES;return;}
        NSString *name=[Payload stringByAppendingFormat:@"payload.%03lu",(unsigned long)chunks.count];
        uint64_t encoded=sizes[name].unsignedLongLongValue;
        if([names countForObject:name]!=1||!encoded||encoded>UINT64_MAX-total){sequence=NO;*stop=YES;return;}
        total+=encoded;
        [chunks addObject:@{@"Member":name,@"TargetSpaceHint":@(fields[1].longLongValue),@"EncodedLength":@(encoded)}];
    }];
    if(!sequence||!chunks.count){Fail(error,EINVAL,@"The original Big Sur System payload is incomplete or out of sequence.");return nil;}
    return @{@"Version":version,@"Build":build,@"Chunks":chunks.copy,@"TotalEncodedLength":@(total)};
}

static ssize_t ReadArchiveBytes(void *context,void *bytes,size_t count) {
    ssize_t n=archive_read_data(context,bytes,count);
    if(n<0)errno=EBADMSG;
    return n;
}

static BOOL DecodePBZXMember(NSURL *zip,NSString *member,int output,VIMArchiveProgress progress,void *context,NSError **error) {
    if(output<0)return Fail(error,EINVAL,@"Invalid Big Sur archive output.");
    __block BOOL found=NO;__block NSError *streamError=nil;
    BOOL ok=Visit(zip,^BOOL(NSString *name,struct archive *reader,struct archive_entry *entry) {
        if(![name isEqual:member])return YES;
        if(found||archive_entry_filetype(entry)!=S_IFREG)
            return Fail(&streamError,EINVAL,@"A Big Sur System payload member is ambiguous or not a regular file.");
        found=YES;
        if(!VIMDecodePBZXFromReader(ReadArchiveBytes,reader,output,progress,context))return YES;
        int problem=errno;
        return Fail(&streamError,problem,problem==ECANCELED?@"Installation cancelled.":
            [NSString stringWithFormat:@"Could not decode the Big Sur payload %@: %s.",member.lastPathComponent,strerror(problem)]);
    },error);
    if(!ok){if(error&&streamError)*error=streamError;return NO;}
    return found||Fail(error,ENOENT,@"A planned Big Sur System payload member is missing.");
}

BOOL VIMBigSurDecodePayloadMember(NSURL *zip,NSString *member,int output,VIMArchiveProgress progress,void *context,NSError **error) {
    if(!member.length||[member rangeOfString:@"^AssetData/payloadv2/payload\\.[0-9]{3,}$" options:NSRegularExpressionSearch].location==NSNotFound)
        return Fail(error,EINVAL,@"Invalid Big Sur System payload member.");
    return DecodePBZXMember(zip,member,output,progress,context,error);
}

BOOL VIMBigSurDecodeFixupManifest(NSURL *zip,int output,VIMArchiveProgress progress,void *context,NSError **error) {
    return DecodePBZXMember(zip,[Payload stringByAppendingString:@"fixup.manifest"],output,progress,context,error);
}

static BOOL WriteBuffer(int fd,const uint8_t *data,size_t count) {
    while(count) {
        ssize_t n=write(fd,data,count);
        if(n<0&&errno==EINTR)continue;
        if(n<=0)return NO;
        data+=n;count-=(size_t)n;
    }
    return YES;
}

static BOOL Directory(NSURL *url,NSError **error) {
    return [NSFileManager.defaultManager createDirectoryAtURL:url withIntermediateDirectories:YES attributes:nil error:error];
}

static BOOL WriteAsset(NSURL *url,int64_t size,ssize_t (^readBytes)(void *,size_t),BOOL (^cancelled)(void),NSError **error) {
    if(size<=0)return Fail(error,EINVAL,@"An original Big Sur boot asset is empty.");
    if(!Directory(url.URLByDeletingLastPathComponent,error))return NO;
    int fd=open(url.fileSystemRepresentation,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0644);
    if(fd<0)return Fail(error,errno,[NSString stringWithFormat:@"Could not stage the Big Sur boot asset %@: %s.",url.lastPathComponent,strerror(errno)]);
    NSMutableData *buffer=[NSMutableData dataWithLength:ReadBuffer];
    BOOL ok=NO;
    @try {
        int64_t copied=0;
        for(;;) {
            if(Cancelled(cancelled,error))return NO;
            ssize_t n=readBytes(buffer.mutableBytes,buffer.length);
            if(n<0||n>size-copied)return Fail(error,EIO,@"Could not read an original Big Sur boot asset.");
            if(!n)break;
            if(!WriteBuffer(fd,buffer.bytes,(size_t)n))return Fail(error,errno,@"Could not write a Big Sur boot asset to the library drive.");
            copied+=n;
        }
        if(copied!=size)return Fail(error,EIO,@"An original Big Sur boot asset is truncated.");
        ok=YES;return YES;
    } @finally {
        close(fd);
        if(!ok)unlink(url.fileSystemRepresentation);
    }
}

static BOOL CopyAsset(NSURL *source,NSURL *destination,BOOL (^cancelled)(void),NSError **error) {
    int input=open(source.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
    struct stat info;
    @try {
        if(input<0||fstat(input,&info)||!S_ISREG(info.st_mode))
            return Fail(error,EINVAL,@"The staged Big Sur System is missing its original kernel collection.");
        return WriteAsset(destination,info.st_size,^ssize_t(void *buffer,size_t capacity){
            ssize_t n;do n=read(input,buffer,capacity);while(n<0&&errno==EINTR);
            return n;
        },cancelled,error);
    } @finally {if(input>=0)close(input);}
}

BOOL VIMCopyOriginalBootMembers(NSURL *zip,NSDictionary<NSString *,NSURL *> *destinations,
    BOOL (^cancelled)(void),NSError **error) {
    NSMutableSet *remaining=[NSMutableSet setWithArray:destinations.allKeys];
    __block NSError *issue=nil;
    BOOL valid=Visit(zip,^BOOL(NSString *name,struct archive *reader,struct archive_entry *entry) {
        if(Cancelled(cancelled,&issue))return NO;
        NSURL *destination=destinations[name];
        if(!destination)return YES;
        if(![remaining containsObject:name]||archive_entry_filetype(entry)!=S_IFREG)
            return Fail(&issue,EINVAL,@"An original boot asset is ambiguous or not a regular file.");
        if(!WriteAsset(destination,archive_entry_size(entry),^ssize_t(void *buffer,size_t capacity){
            return archive_read_data(reader,buffer,capacity);
        },cancelled,&issue))return NO;
        [remaining removeObject:name];return YES;
    },error);
    if(!valid){if(error&&issue)*error=issue;return NO;}
    return !remaining.count||Fail(error,ENOENT,@"An original boot asset is missing.");
}

NSData *VIMBigSurStageBootAssets(NSURL *zip,NSURL *system,NSURL *preboot,NSURL *recovery,
    NSUUID *dataUUID,NSString *build,BOOL (^cancelled)(void),NSError **error) {
    NSDictionary *installed=[NSDictionary dictionaryWithContentsOfURL:[system URLByAppendingPathComponent:SystemVersion]];
    if(!dataUUID||!build.length||![installed[@"ProductBuildVersion"] isEqual:build]) {
        Fail(error,EINVAL,@"The staged Big Sur System does not match the original installer build.");return nil;
    }
    NSArray *members=VIMBigSurArchiveMembers(zip,error);
    NSDictionary *plan=members?VIMBigSurBootAssetPlan(members,error):nil;
    if(!plan)return nil;
    NSURL *pre=[preboot URLByAppendingPathComponent:dataUUID.UUIDString],*rec=[recovery URLByAppendingPathComponent:dataUUID.UUIDString];
    NSMutableDictionary<NSString *,NSMutableArray<NSURL *> *> *destinations=[NSMutableDictionary dictionary];
    for(NSString *role in @[@"Preboot",@"Recovery"]) {
        NSDictionary *mapping=plan[role];NSURL *root=[role isEqual:@"Preboot"]?pre:rec;
        for(NSString *relative in mapping) {
            NSString *name=mapping[relative];
            if(!destinations[name])destinations[name]=[NSMutableArray array];
            [destinations[name] addObject:[root URLByAppendingPathComponent:relative]];
        }
    }
    // The tiny build manifest is also needed for a strict build check.
    __block NSData *digest=nil;
    __block BOOL correctBuild=NO;
    __block NSError *streamError=nil;
    BOOL valid=Visit(zip,^BOOL(NSString *name,struct archive *reader,struct archive_entry *entry) {
        if(Cancelled(cancelled,&streamError))return NO;
        if(!destinations[name])return YES;
        if([name isEqual:plan[@"IntelRootHashMember"]]) {
            NSData *root=ReadMember(reader,entry,&streamError);
            if(!root||!(digest=VIMBigSurIntelRootHash(root,&streamError)))return NO;
        } else if([name isEqual:@"AssetData/boot/SystemVersion.plist"]) {
            NSData *version=ReadMember(reader,entry,&streamError);
            if(!version)return NO;
            id plist=[NSPropertyListSerialization propertyListWithData:version options:0 format:NULL error:&streamError];
            correctBuild=[plist isKindOfClass:NSDictionary.class]&&[plist[@"ProductBuildVersion"] isEqual:build];
            if(!correctBuild)return Fail(&streamError,EINVAL,@"Big Sur boot assets do not match the staged System build.");
        }
        return YES;
    },error);
    if(!valid){if(error&&streamError)*error=streamError;return nil;}
    if(!digest||!correctBuild) {Fail(error,EINVAL,@"The original Big Sur boot manifests are incomplete.");return nil;}
    valid=Visit(zip,^BOOL(NSString *name,struct archive *reader,struct archive_entry *entry) {
        if(Cancelled(cancelled,&streamError))return NO;
        NSArray<NSURL *> *paths=destinations[name];
        if(!paths)return YES;
        if(archive_entry_filetype(entry)!=S_IFREG)return Fail(&streamError,EINVAL,@"A Big Sur boot asset is not a regular file.");
        BOOL patch=[name isEqual:VIMIntelRecoveryPatchMember];
        NSURL *output=paths.firstObject;
        NSURL *stream=patch?[output URLByAppendingPathExtension:@"patch"]:output;
        if(!WriteAsset(stream,archive_entry_size(entry),^ssize_t(void *buffer,size_t capacity){
            return archive_read_data(reader,buffer,capacity);
        },cancelled,&streamError))return NO;
        if(patch) {
            BOOL decoded=VIMDecodeEmptySourceRecoveryPatch(stream,output,cancelled,&streamError);
            int removed=unlink(stream.fileSystemRepresentation);
            if(!decoded)return NO;
            if(removed)return Fail(&streamError,errno,@"Could not remove the staged Recovery patch.");
        }
        for(NSUInteger i=1;i<paths.count;i++)if(!CopyAsset(paths.firstObject,paths[i],cancelled,&streamError))return NO;
        [destinations removeObjectForKey:name];return YES;
    },error);
    if(!valid){if(error&&streamError)*error=streamError;return nil;}
    if(destinations.count) {Fail(error,EINVAL,@"A Big Sur boot asset was not extracted.");return nil;}
    for(NSString *relative in @[KernelCollection,[KernelCollection stringByAppendingString:@".elides"]])
        if(!CopyAsset([system URLByAppendingPathComponent:relative],
            [pre URLByAppendingPathComponent:[@"boot/" stringByAppendingString:relative]],cancelled,error))return nil;
    NSURL *prefs=[pre URLByAppendingPathComponent:@"Library/Preferences/SystemConfiguration/com.apple.Boot.plist"];
    NSData *data=[NSPropertyListSerialization dataWithPropertyList:@{@"Kernel Flags":@""} format:NSPropertyListXMLFormat_v1_0 options:0 error:error];
    NSURL *label=[pre URLByAppendingPathComponent:@"System/Library/CoreServices/.disk_label.contentDetails"];
    return data&&Directory(prefs.URLByDeletingLastPathComponent,error)&&
        [data writeToURL:prefs options:NSDataWritingWithoutOverwriting error:error]&&
        [[@"Macintosh HD" dataUsingEncoding:NSUTF8StringEncoding] writeToURL:label options:NSDataWritingWithoutOverwriting error:error]?digest:nil;
}
