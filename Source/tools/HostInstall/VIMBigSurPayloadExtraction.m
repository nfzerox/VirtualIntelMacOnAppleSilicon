#import "VIMBigSurArchive.h"
#import "VIMBigSurFixups.h"
#include <AppleArchive/AppleArchive.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static NSError *ExtractionError(NSInteger code,NSString *message) {
    return [NSError errorWithDomain:@"VirtualIntelMac.BigSurExtraction" code:code
        userInfo:@{NSLocalizedDescriptionKey:message}];
}

typedef struct {
    __unsafe_unretained BOOL (^cancelled)(void);
    __unsafe_unretained void (^progress)(uint64_t,uint64_t);
    uint64_t total,last;
} CompressionFeed;

static bool CountCompressionInput(__unused uint64_t input,uint64_t output,void *opaque) {
    CompressionFeed *feed=opaque;
    feed->total=output;
    return !feed->cancelled||!feed->cancelled();
}

static bool CompressionProgress(__unused uint64_t input,uint64_t output,void *opaque) {
    CompressionFeed *feed=opaque;
    if(feed->cancelled&&feed->cancelled())return false;
    if(feed->progress&&output>feed->last&&(output-feed->last>=256*1024||output==feed->total)) {
        feed->last=output;
        feed->progress(output,feed->total);
    }
    return true;
}

static BOOL RestoreCompression(NSURL *zip,NSURL *system,NSURL *paths,BOOL (^cancelled)(void),
    void (^progress)(uint64_t,uint64_t),NSError **error) {
    // The public AppleArchive extraction stream has no check-and-fix
    // operation. Use the system encoder for Apple's LZVN and inline
    // compression instead of substituting a modern default representation.
    NSTask *task=[NSTask new];NSPipe *input=[NSPipe pipe];
    task.executableURL=[NSURL fileURLWithPath:@"/usr/bin/aa"];
    NSArray *arguments=@[@"check-and-fix",@"-d",system.path,@"-include-type",@"f",@"-exclude-field",@"uid,gid,mod,acl,flg"];
    if(paths)arguments=[arguments arrayByAddingObjectsFromArray:@[@"-include-path-list",paths.path]];
    task.standardInput=input.fileHandleForReading;
    int output=input.fileHandleForWriting.fileDescriptor;
    int flags=fcntl(output,F_GETFL);
    if(flags<0||fcntl(output,F_SETFL,flags|O_NONBLOCK)<0||fcntl(output,F_SETNOSIGPIPE,1)<0) {
        if(error)*error=ExtractionError(errno,@"Could not prepare the Big Sur compression stream.");return NO;
    }
    dispatch_semaphore_t exited=dispatch_semaphore_create(0);
    task.terminationHandler=^(__unused NSTask *finished){dispatch_semaphore_signal(exited);};
    NSURL *node=[system URLByAppendingPathComponent:@"System/Library/Templates/Data/private/var/db/dslocal/nodes/Default"];
    struct stat info;mode_t mode=0;BOOL opened=NO,decoded=NO,interrupted=NO;
    CompressionFeed feed={.cancelled=cancelled,.progress=progress};
    NSMutableDictionary<NSString *,NSNumber *> *writable=[NSMutableDictionary dictionary];
    NSMutableArray<NSArray<NSString *> *> *aliases=[NSMutableArray array];
    NSString *exclude=nil;
    NSSet<NSString *> *selected=nil;
    if(paths) {
        NSString *list=[NSString stringWithContentsOfURL:paths encoding:NSUTF8StringEncoding error:error];
        if(!list)return NO;
        selected=[NSSet setWithArray:[list componentsSeparatedByString:@"\n"]];
    }
    int stagedRoot=open(system.fileSystemRepresentation,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(stagedRoot<0) {
        if(error)*error=ExtractionError(errno,@"Could not open the staged System volume.");return NO;
    }
    if(!lstat(node.fileSystemRepresentation,&info)&&S_ISDIR(info.st_mode)&&!(info.st_mode&S_IXUSR)) {
        mode=info.st_mode&07777;
        if(chmod(node.fileSystemRepresentation,mode|S_IXUSR)) {
            close(stagedRoot);
            if(error)*error=ExtractionError(errno,@"Could not open the Big Sur template for compression.");return NO;
        }
        opened=YES;
    }
    @try {
        __block NSString *blocked=nil,*original=nil;
        __block int linkError=0;
        BOOL openedFiles=VIMBigSurVisitFixups(zip,^BOOL(NSString *path,VIMBigSurFixup entry,NSString *owner) {
            if(owner&&!paths) {
                // check-and-fix may replace the canonical file instead of
                // editing the inode. Reattach every alias to the final
                // compressed inode, not to the uncompressed predecessor.
                if([path rangeOfCharacterFromSet:NSCharacterSet.newlineCharacterSet].location!=NSNotFound) {
                    blocked=path;original=owner;linkError=EINVAL;return NO;
                }
                if(!VIMBigSurDetachFixupHardLink(stagedRoot,owner,path)) {
                    blocked=path;original=owner;linkError=errno?:EIO;return NO;
                }
                [aliases addObject:@[path,owner]];
                return YES;
            }
            if(!S_ISREG(entry.mode)||(selected&&![selected containsObject:path]))return YES;
            // Check-and-fix also opens uncompressed regular files to load
            // their attributes. Apple's empty /.file has no read permission.
            mode_t access=S_IRUSR|(entry.compressed?S_IWUSR:0);
            if((entry.mode&access)==access)return YES;
            NSURL *file=[system URLByAppendingPathComponent:path];struct stat observed;
            if(lstat(file.fileSystemRepresentation,&observed)||!S_ISREG(observed.st_mode)) {blocked=path;return NO;}
            mode_t current=observed.st_mode&07777;
            if((current&access)!=access) {
                if(chmod(file.fileSystemRepresentation,current|access)){blocked=path;return NO;}
                writable[path]=@(current);
            }
            return YES;
        },CountCompressionInput,&feed,error);
        if(!openedFiles) {
            if(blocked&&error)*error=linkError?ExtractionError(linkError,[NSString stringWithFormat:
                @"Could not prepare the original Apple hard link %@ from %@: %s.",blocked,original,strerror(linkError)]):
                ExtractionError(EACCES,[NSString stringWithFormat:@"Could not open a staged System file for compression: %@.",blocked]);
            return NO;
        }
        if(aliases.count) {
            NSMutableString *lines=[NSMutableString string];
            for(NSArray<NSString *> *alias in aliases)[lines appendFormat:@"%@\n",alias[0]];
            char *pattern=strdup([NSTemporaryDirectory() stringByAppendingPathComponent:@"VIM-Apple-links.XXXXXX"].fileSystemRepresentation);
            int fd=pattern?mkstemp(pattern):-1;
            if(fd>=0)exclude=[NSFileManager.defaultManager stringWithFileSystemRepresentation:pattern length:strlen(pattern)];
            free(pattern);
            if(fd<0){if(error)*error=ExtractionError(errno,@"Could not prepare the Apple archive link list.");return NO;}
            NSFileHandle *file=[[NSFileHandle alloc] initWithFileDescriptor:fd closeOnDealloc:YES];
            BOOL written=[file writeData:[lines dataUsingEncoding:NSUTF8StringEncoding] error:error];
            [file closeFile];
            if(!written)return NO;
            arguments=[arguments arrayByAddingObjectsFromArray:@[@"-exclude-path-list",exclude]];
        }
        task.arguments=arguments;
        if(![task launchAndReturnError:error])return NO;
        [input.fileHandleForReading closeFile];
        @try {
            if(progress)progress(0,feed.total);
            decoded=VIMBigSurDecodeFixupManifest(zip,output,CompressionProgress,&feed,error);
        } @finally {
            [input.fileHandleForWriting closeFile];
            // Do not return to the mount owner until the tool has stopped
            // touching this unpublished System volume.
            while(dispatch_semaphore_wait(exited,dispatch_time(DISPATCH_TIME_NOW,500*NSEC_PER_MSEC))) {
                if(!interrupted&&(!decoded||(cancelled&&cancelled()))) {
                    interrupted=YES;if(task.running)[task terminate];
                }
            }
        }
        if(decoded&&!interrupted&&task.terminationReason==NSTaskTerminationReasonExit&&task.terminationStatus<=1) {
            for(NSArray<NSString *> *alias in aliases) @autoreleasepool {
                if(cancelled&&cancelled()) {
                    decoded=NO;if(error)*error=ExtractionError(ECANCELED,@"Installation cancelled.");break;
                }
                if(!VIMBigSurStageFixupHardLink(stagedRoot,alias[1],alias[0])) {
                    int issue=errno?:EIO;
                    decoded=NO;
                    if(error)*error=ExtractionError(issue,[NSString stringWithFormat:
                        @"Could not restore the original Apple hard link %@ from %@: %s.",alias[0],alias[1],strerror(issue)]);
                    break;
                }
            }
        }
    } @finally {
        if(exclude)unlink(exclude.fileSystemRepresentation);
        close(stagedRoot);
        BOOL restored=YES;
        for(NSString *path in writable) @autoreleasepool {
            NSURL *file=[system URLByAppendingPathComponent:path];
            if(chmod(file.fileSystemRepresentation,[writable[path] unsignedShortValue]))restored=NO;
        }
        if(opened&&chmod(node.fileSystemRepresentation,mode))restored=NO;
        if(!restored&&decoded) {
            decoded=NO;
            if(error)*error=ExtractionError(errno,@"Could not restore the original Big Sur template mode.");
        }
    }
    if(!decoded)return NO;
    if(!interrupted&&task.terminationReason==NSTaskTerminationReasonExit&&
        (task.terminationStatus==0||task.terminationStatus==1))return YES;
    if(error)*error=ExtractionError(interrupted?ECANCELED:EIO,interrupted?@"Installation cancelled.":
        @"Apple Archive could not restore the Big Sur compression format.");
    return NO;
}

BOOL VIMBigSurRestoreCompression(NSURL *zip,NSURL *system,BOOL (^cancelled)(void),
    void (^progress)(uint64_t,uint64_t),NSError **error) {
    return RestoreCompression(zip,system,nil,cancelled,progress,error);
}

BOOL VIMBigSurRepairCompression(NSURL *zip,NSURL *system,NSURL *paths,BOOL (^cancelled)(void),NSError **error) {
    return RestoreCompression(zip,system,paths,cancelled,nil,error);
}

BOOL VIMBigSurTrimCompressedFork(NSURL *file,NSError **error) {
    const char *path=file.fileSystemRepresentation;
    struct stat status;
    int problem=0,fd=-1,forkFD=-1;
    BOOL widened=NO,unhidden=NO;
    if(lstat(path,&status))problem=errno;
    else if(!S_ISREG(status.st_mode)||!(status.st_flags&UF_COMPRESSED))problem=EBADMSG;
    mode_t mode=problem?0:status.st_mode&07777;
    if(!problem) {
        mode_t access=S_IRUSR|S_IWUSR;
        if((mode&access)!=access) {
            if(chmod(path,mode|access))problem=errno;
            else widened=YES;
        }
    }
    if(!problem) {
        fd=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
        if(fd<0||fchflags(fd,status.st_flags&~UF_COMPRESSED))problem=errno;
        else unhidden=YES;
    }
    if(!problem) {
        NSString *fork=[file.path stringByAppendingString:@"/..namedfork/rsrc"];
        struct stat resource;
        forkFD=open(fork.fileSystemRepresentation,O_RDWR|O_NOFOLLOW|O_CLOEXEC);
        if(forkFD<0||fstat(forkFD,&resource))problem=errno;
        else if(resource.st_size<=0)problem=EBADMSG;
        else {
            uint64_t length=resource.st_size,allocated=(uint64_t)resource.st_blocks*512;
            uint64_t expected=(length+4095)&~UINT64_C(4095);
            if(allocated>expected) {
                // APFS's compressor can leave a block past EOF. Truncating
                // to the existing size is a no-op. Remove and restore the
                // last byte to release the tail through the native allocator
                // without rewriting the compressed stream or its metadata.
                off_t last=resource.st_size-1;uint8_t byte;
                if(pread(forkFD,&byte,1,last)!=1)problem=errno?:EIO;
                else if(ftruncate(forkFD,last))problem=errno;
                else if(pwrite(forkFD,&byte,1,last)!=1)problem=errno?:EIO;
                else if(fsync(forkFD)||fstat(forkFD,&resource))problem=errno;
                else if((uint64_t)resource.st_size!=length||(uint64_t)resource.st_blocks*512!=expected)problem=EBADMSG;
            }
        }
    }
    if(forkFD>=0)close(forkFD);
    if(unhidden&&fchflags(fd,status.st_flags)&&!problem)problem=errno;
    if(widened&&(fd>=0?fchmod(fd,mode):chmod(path,mode))&&!problem)problem=errno;
    if(fd>=0)close(fd);
    if(problem&&error)*error=ExtractionError(problem,[NSString stringWithFormat:
        @"Could not trim the extra allocation for Big Sur’s compressed %@ resource.",file.lastPathComponent]);
    return !problem;
}

static int DeferProtectedLink(NSMutableArray<NSDictionary *> *links,const char *path,AAHeader header) {
    const AAFieldKey type={.skey="TYP"},link={.skey="LNK"},mtime={.skey="MTM"},
        acl={.skey="ACL"},xattrs={.skey="XAT"},created={.skey="CTM"},backup={.skey="BTM"};
    uint64_t t=0;char destination[4096];size_t length=0;
    if(AAHeaderGetFieldUIntWithKey(header,type,&t)!=1||t!='L'||
        AAHeaderGetFieldStringWithKey(header,link,sizeof(destination),destination,&length)!=1||
        length>=sizeof(destination)||strlen(destination)!=length||destination[0]!='/')return 0;
    // Some original parents are already read-only when a later chunk adds
    // these links. Materialize them after extraction with directory-relative,
    // no-follow calls, widening and restoring only their private parents.
    // Do not silently omit any other metadata from the archive.
    if(AAHeaderGetKeyIndex(header,acl)>=0||AAHeaderGetKeyIndex(header,xattrs)>=0||
        AAHeaderGetKeyIndex(header,created)>=0||AAHeaderGetKeyIndex(header,backup)>=0)return 0;
    struct timespec stamp={0};
    int hasTime=AAHeaderGetFieldTimespecWithKey(header,mtime,&stamp);
    NSString *relative=@(path),*target=@(destination);
    if(!relative.length||!target.length||hasTime<0)return -1;
    @synchronized(links) {
        [links addObject:@{@"Path":relative,@"Target":target,@"Time":hasTime?[NSData dataWithBytes:&stamp length:sizeof(stamp)]:NSData.data}];
    }
    return 1;
}

static BOOL StageProtectedLink(int root,NSDictionary *link) {
    const char *path=[link[@"Path"] fileSystemRepresentation],*destination=[link[@"Target"] fileSystemRepresentation];
    char relative[4096];size_t size=strlen(path);
    if(!size||size>=sizeof(relative)||path[0]=='/')return NO;
    memcpy(relative,path,size+1);
    int parent=dup(root);
    if(parent<0)return NO;
    char *component=relative;
    while(true) {
        char *slash=strchr(component,'/');if(slash)*slash='\0';
        if(!*component||!strcmp(component,".")||!strcmp(component,"..")){close(parent);return NO;}
        if(!slash)break;
        int child=openat(parent,component,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        close(parent);parent=child;
        if(parent<0){fprintf(stderr,"Could not open Big Sur link parent %s: %s.\n",component,strerror(errno));return NO;}
        component=slash+1;
    }
    struct stat info;
    BOOL ok=NO,opened=NO;
    if(!fstatat(parent,component,&info,AT_SYMLINK_NOFOLLOW)) {
        char original[4096];size_t length=strlen(destination);ssize_t count=readlinkat(parent,component,original,sizeof(original));
        ok=count==(ssize_t)length&&!memcmp(original,destination,length);
        if(!ok&&count>=0)errno=EEXIST;
    } else if(errno==ENOENT&&!fstat(parent,&info)) {
        // The original timezone directory is read-only (0555). The archive
        // has already restored that mode, so open it only for the link write.
        mode_t mode=info.st_mode&07777;
        if(!(mode&S_IWUSR))opened=!fchmod(parent,mode|S_IWUSR);
        if((mode&S_IWUSR)||opened)ok=!symlinkat(destination,parent,component);
        int problem=errno;
        if(opened&&fchmod(parent,mode)){ok=NO;problem=errno;}
        if(!ok)errno=problem;
    }
    if(ok&&[link[@"Time"] length]) {
        struct timespec stamp[2]={{.tv_nsec=UTIME_OMIT},{0,0}};
        [link[@"Time"] getBytes:&stamp[1] length:sizeof(stamp[1])];
        ok=!fstatat(parent,component,&info,AT_SYMLINK_NOFOLLOW)&&S_ISLNK(info.st_mode);
        if(ok&&(info.st_mtimespec.tv_sec!=stamp[1].tv_sec||info.st_mtimespec.tv_nsec!=stamp[1].tv_nsec))
            ok=!utimensat(parent,component,stamp,AT_SYMLINK_NOFOLLOW);
    }
    if(!ok)fprintf(stderr,"Could not safely stage Big Sur link %s: %s.\n",path,strerror(errno));
    close(parent);
    return ok;
}

static int StageAttributes(void *context,AAEntryMessage message,const char *path,void *data) {
    if(message==AA_ENTRY_MESSAGE_EXTRACT_BEGIN&&path&&data) {
        int staged=DeferProtectedLink((__bridge NSMutableArray *)context,path,data);
        if(staged)return staged;
    }
    if(message==AA_ENTRY_MESSAGE_EXTRACT_ATTRIBUTES&&data) {
        AAEntryAttributes *attributes=data;
        // Keep private staging directories traversable/writable until every
        // chunk and compression/boot-asset phase finishes. Applying a final
        // mode such as0600 here can block queued metadata or the next chunk.
        // The detached catalog pass restores ALL original guest modes and
        // ownership from fixup.manifest; do not apply them through host VFS.
        attributes->UID=attributes->GID=attributes->MOD=0;
        // Keep the unprivileged compression flag. Clearing FLG entirely also
        // disables AppleArchive's streaming filesystem compressor, causing a
        // full uncompressed write and another read/rewrite in check-and-fix.
        // Protected guest flags are still deferred to detached finalization.
        attributes->flg=attributes->FLG?(attributes->flg&UF_COMPRESSED):0;
        attributes->FLG=attributes->flg!=0;
    }
    if(message==AA_ENTRY_MESSAGE_EXTRACT_XAT&&data) {
        // The host VFS reserves Cryptex alternate links even for root. The
        // original value is restored from fixup.manifest in the detached
        // guest catalog, alongside rootless metadata, before seal checking.
        // Keep every other archive attribute on the staged object.
        AAEntryXATBlob blob=data;
        for(uint32_t i=AAEntryXATBlobGetEntryCount(blob);i>0;i--) {
            char name[256];size_t length=0;
            if(AAEntryXATBlobGetEntry(blob,i-1,0,NULL,&length,0,NULL,NULL)<0)return -1;
            if(length>=sizeof(name))continue;
            if(AAEntryXATBlobGetEntry(blob,i-1,sizeof(name),name,NULL,0,NULL,NULL)<0)return -1;
            if(!strcmp(name,"com.apple.fs.altlink")&&AAEntryXATBlobRemoveEntry(blob,i-1)<0)return -1;
            if(!strcmp(name,"com.apple.rootless")) {
                // Retain the bytes on System and copied Data templates. The
                // detached catalog pass restores this equal-length name.
                size_t size=0;
                if(AAEntryXATBlobGetEntry(blob,i-1,0,NULL,NULL,0,NULL,&size)<0)return -1;
                NSMutableData *value=[NSMutableData dataWithLength:size];
                if(AAEntryXATBlobGetEntry(blob,i-1,0,NULL,NULL,size,value.mutableBytes,NULL)<0||
                    AAEntryXATBlobSetEntry(blob,i-1,"com.apple.rootlesr",value.bytes,value.length)<0)return -1;
            }
        }
    }
    if(message==AA_ENTRY_MESSAGE_EXTRACT_FAIL) {
        fprintf(stderr,"Apple Archive could not extract %s.\n",path);return -1;
    }
    return 0;
}

static BOOL Extract(int input,NSURL *root) {
    int directory=open(root.fileSystemRepresentation,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(directory<0){fprintf(stderr,"Could not open Apple Archive destination: %s.\n",strerror(errno));return NO;}
    AAByteStream source=AAFileStreamOpenWithFD(input,0);
    if(!source){fprintf(stderr,"Could not open Apple Archive input: %s.\n",strerror(errno));close(directory);return NO;}
    AAArchiveStream archive=AADecodeArchiveInputStreamOpen(source,NULL,NULL,0,0);
    NSMutableArray<NSDictionary *> *links=[NSMutableArray array];
    // The original Intel seal describes dense file streams. On modern APFS,
    // default Apple Archive extraction replaces aligned zero data with holes.
    // Preserve those original zero blocks so the signed inode and physical
    // file extent layouts agree when the detached snapshot is rebuilt.
    AAArchiveStream sink=archive?AAExtractArchiveOutputStreamOpen(root.fileSystemRepresentation,(__bridge void *)links,StageAttributes,
        AA_FLAG_EXTRACT_NO_AUTO_SPARSE,0):NULL;
    if(!archive||!sink)fprintf(stderr,"Could not open Apple Archive %s stream: %s.\n",archive?"extraction":"decoding",strerror(errno));
    ssize_t processed=sink?AAArchiveStreamProcess(archive,sink,NULL,NULL,0,0):-1;
    BOOL ok=sink&&processed>=0;
    if(sink&&processed<0)fprintf(stderr,"Apple Archive processing failed (%zd): %s.\n",processed,strerror(errno));
    int closed=sink?AAArchiveStreamClose(sink):0;
    if(closed){fprintf(stderr,"Apple Archive extraction finalization failed (%d): %s.\n",closed,strerror(errno));ok=NO;}
    closed=archive?AAArchiveStreamClose(archive):0;
    if(closed){fprintf(stderr,"Apple Archive decoding finalization failed (%d).\n",closed);ok=NO;}
    if(AAByteStreamClose(source)){fprintf(stderr,"Apple Archive input finalization failed.\n");ok=NO;}
    // BEGIN callbacks can precede queued directory writes. Materialize the
    // collected links only after AppleArchive has finished those writes.
    for(NSDictionary *link in links)if(ok&&!StageProtectedLink(directory,link))ok=NO;
    close(directory);
    return ok;
}

BOOL VIMBigSurExtractPayloadMember(NSURL *zip,NSString *member,NSURL *root,
    VIMArchiveProgress progress,void *context,NSError **error) {
    int io[2];
    if(pipe(io)) {
        if(error)*error=ExtractionError(errno,@"Could not open the Big Sur extraction stream.");return NO;
    }
    int input=io[0],output=io[1];
    if(fcntl(output,F_SETNOSIGPIPE,1)<0||fcntl(input,F_SETFD,FD_CLOEXEC)<0||fcntl(output,F_SETFD,FD_CLOEXEC)<0) {
        int problem=errno;close(input);close(output);
        if(error)*error=ExtractionError(problem,@"Could not configure the Big Sur extraction stream.");return NO;
    }
    __block BOOL extracted=NO;
    dispatch_group_t reader=dispatch_group_create();
    dispatch_group_async(reader,dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{ @autoreleasepool {
        extracted=Extract(input,root);close(input);
    } });
    NSError *decodeError=nil;BOOL decoded=NO;
    @try {decoded=VIMBigSurDecodePayloadMember(zip,member,output,progress,context,&decodeError);}
    @finally {close(output);dispatch_group_wait(reader,DISPATCH_TIME_FOREVER);}
    if(decoded&&extracted)return YES;
    if(!decoded&&decodeError)fprintf(stderr,"%s\n",decodeError.localizedDescription.UTF8String);
    if(error)*error=decodeError.code==ECANCELED?decodeError:!extracted?
        ExtractionError(EIO,[NSString stringWithFormat:@"Could not extract Big Sur System member %@.",member.lastPathComponent]):decodeError;
    return NO;
}

typedef struct {VIMArchiveProgress fn;void *context;uint64_t base,decodedBase,decoded;} ChunkProgress;

static bool StageProgress(uint64_t input,uint64_t output,void *context) {
    ChunkProgress *state=context;state->decoded=output;
    return !state->fn||state->fn(state->base+input,state->decodedBase+output,state->context);
}

static BOOL VerifyCache(NSURL *root,NSString *suffix,const char magic[16]) {
    NSURL *file=[root URLByAppendingPathComponent:[@"System/Library/dyld/dyld_shared_cache_" stringByAppendingString:suffix]];
    int fd=open(file.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
    struct stat info;char bytes[16];
    BOOL valid=fd>=0&&!fstat(fd,&info)&&S_ISREG(info.st_mode)&&read(fd,bytes,sizeof(bytes))==sizeof(bytes)&&!memcmp(bytes,magic,sizeof(bytes));
    if(fd>=0)close(fd);
    return valid;
}

BOOL VIMBigSurStageSystemPayload(NSURL *zip,NSDictionary *inventory,NSURL *root,VIMAssistantCacheLayout layout,
    VIMArchiveProgress progress,void *context,NSError **error) {
    NSArray<NSDictionary *> *chunks=inventory[@"Chunks"];
    NSURL *template=[root URLByAppendingPathComponent:@"System/Library/Templates/Data/private/var/db/dslocal/nodes/Default"];
    const char *path=template.fileSystemRepresentation;
    mode_t original=0;BOOL opened=NO,ok=chunks.count>0;
    ChunkProgress state={.fn=progress,.context=context};
    @try {
        for(NSDictionary *chunk in chunks) {
            struct stat info;
            if(!opened&&!lstat(path,&info)&&S_ISDIR(info.st_mode)&&!(info.st_mode&S_IXUSR)) {
                original=info.st_mode&07777;
                if(chmod(path,original|S_IXUSR)) {
                    if(error)*error=ExtractionError(errno,@"Could not open the Big Sur directory-service template for extraction.");
                    return NO;
                }
                opened=YES;
            }
            if(!VIMBigSurExtractPayloadMember(zip,chunk[@"Member"],root,StageProgress,&state,error)){ok=NO;break;}
            state.base+=[chunk[@"EncodedLength"] unsignedLongLongValue];
            state.decodedBase+=state.decoded;
        }
    } @finally {
        if(opened&&chmod(path,original)&&ok) {
            if(error)*error=ExtractionError(errno,@"Could not restore the original Big Sur template mode.");ok=NO;
        }
    }
    if(!ok)return NO;
    NSDictionary *version=[NSDictionary dictionaryWithContentsOfURL:[root URLByAppendingPathComponent:@"System/Library/CoreServices/SystemVersion.plist"]];
    if([version[@"ProductBuildVersion"] isEqual:inventory[@"Build"]]) {
        if(layout==VIMAssistantCachesInline&&VerifyCache(root,@"x86_64","dyld_v1  x86_64")&&
           VerifyCache(root,@"x86_64h","dyld_v1 x86_64h"))return YES;
        if(layout==VIMAssistantCachesIntelCryptex) {
            // Apple mounts the Intel caches from the authenticated Cryptex.
            // The worker verifies those files separately in Preboot.
            NSURL *directory=[root URLByAppendingPathComponent:@"System/Library/dyld" isDirectory:YES];
            struct stat info;
            BOOL inlineCaches=NO;
            for(NSString *arch in @[@"x86_64",@"x86_64h"]) {
                NSURL *path=[directory URLByAppendingPathComponent:[@"dyld_shared_cache_" stringByAppendingString:arch]];
                if(!lstat(path.fileSystemRepresentation,&info)||errno!=ENOENT)inlineCaches=YES;
            }
            if(!inlineCaches)return YES;
        }
    }
    if(error)*error=ExtractionError(EBADMSG,@"The extracted System or original Intel shared-cache layout does not match the installer.");
    return NO;
}
